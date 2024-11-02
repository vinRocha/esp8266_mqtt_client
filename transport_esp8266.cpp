/*
 * Copyright (C) 2024 silva.viniciusr@gmail.com,  all rights reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <string.h>
#include "transport_esp8266.h"
#include "serial.h"

//Below includes will change in FreeRTOS implementation
#include <cstdio>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <mqueue.h>
#include <pthread.h>
#define SLEEP usleep(200000)

/* As networking data and control data all comes from
 * same UART interface, rxThread will be responsible to
 * collect them all and populate in two different queues
 * accordingly. dataQueue and controlQueue. The transport
 * program shall consume data from these buffers.
 */
static pthread_t thread_id; //in FreeRTOS this will be an high priority task.
static void *rxThread(void *args);
static mqd_t controlQTx, controlQRx, dataQTx, dataQRx;
static char const *control_mq_name = "/esp8266_control";
static char const *data_mq_name = "/esp8266_data";

//constants
int const BUFFER_LEN = 128;
unsigned long const BAUD_RATE = 115200;
TickType_t const RX_BLOCK = 0xff;
TickType_t const TX_BLOCK = 0x00;
TickType_t const NO_BLOCK = 0x00;
int const AT_REPLY_LEN = 7;

enum transportStatus {
    AT_UNINITIALIZED = 0,
    MQUEUE_UNINITIALIZED,
    RX_THREAD_UNINITIALIZED,
    AT_READY,
    CONNECTED,
    ERROR
};

static char esp8266_status = AT_UNINITIALIZED;

static void check_AT(void);
static void start_TCP(const char *pHostName, const char *port);

esp8266TransportStatus_t esp8266AT_Connect(const char *pHostName, const char *port) {

    if (esp8266_status == CONNECTED) {
        return ESP8266_TRANSPORT_SUCCESS;
    }

    if (esp8266_status == AT_UNINITIALIZED) {
        xSerialPortInitMinimal(BAUD_RATE, BUFFER_LEN);
        esp8266_status = MQUEUE_UNINITIALIZED;
    }

    if (esp8266_status == MQUEUE_UNINITIALIZED) {
        controlQTx = mq_open(control_mq_name, O_WRONLY | O_CREAT | O_NONBLOCK, S_IRUSR | S_IWUSR, NULL);
        controlQRx = mq_open(control_mq_name, O_RDONLY);
        dataQTx = mq_open(data_mq_name, O_WRONLY | O_CREAT | O_NONBLOCK, S_IRUSR | S_IWUSR, NULL);
        dataQRx = mq_open(data_mq_name, O_RDONLY);
        if ((controlQTx == (mqd_t) -1) || (controlQRx == (mqd_t) -1) || 
            (dataQTx == (mqd_t) -1)    || (dataQRx == (mqd_t) -1)) {
            return ESP8266_TRANSPORT_CONNECT_FAILURE;
        }
        esp8266_status = RX_THREAD_UNINITIALIZED;
    }

    if (esp8266_status == RX_THREAD_UNINITIALIZED) {
        if (pthread_create(&thread_id, NULL, &rxThread, NULL)) {
            return ESP8266_TRANSPORT_CONNECT_FAILURE;
        }
        esp8266_status = AT_READY;
    }

    if (esp8266_status == AT_READY) {
        check_AT();
        if (esp8266_status == ERROR) {
            return ESP8266_TRANSPORT_CONNECT_FAILURE;
        }

        start_TCP(pHostName, port);
        if (esp8266_status == ERROR) {
            return ESP8266_TRANSPORT_CONNECT_FAILURE;
        }
        
        return ESP8266_TRANSPORT_SUCCESS;
    }

    return ESP8266_TRANSPORT_CONNECT_FAILURE;
}

esp8266TransportStatus_t esp8266AT_Disconnect(void) {
    esp8266_status = AT_UNINITIALIZED;
    pthread_join(thread_id, NULL);
    mq_close(controlQTx);
    mq_close(controlQRx);
    mq_close(dataQTx);
    mq_close(dataQRx);
    vSerialClose(NULL);
    if (errno) {
        perror("Error after closing message queues.");
    }
    return ESP8266_TRANSPORT_SUCCESS;
}

int32_t esp8266AT_recv(NetworkContext_t *pNetworkContext, void *pBuffer, size_t bytesToRecv) {
    return 0;
}

int32_t esp8266AT_send(NetworkContext_t *pNetworkContext, const void *pBuffer, size_t bytesToSend) {
    return 0;
}

void check_AT(void) {
    char at_cmd_response[AT_REPLY_LEN] = {0};

    //Send AT command
    xSerialPutChar(NULL, 'A', TX_BLOCK);
    xSerialPutChar(NULL, 'T', TX_BLOCK);
    xSerialPutChar(NULL, 'E', TX_BLOCK); //
    xSerialPutChar(NULL, '0', TX_BLOCK); // Disable echo
    xSerialPutChar(NULL, '\r', TX_BLOCK);

    SLEEP; //so serial interface has enough time to receive echo.
    //Clear echo
    while (xSerialGetChar(NULL, (signed char*) at_cmd_response, NO_BLOCK));

    //Complete the command
    xSerialPutChar(NULL, '\n', TX_BLOCK);

    SLEEP; //delay to receive response on serial interface
    for (int i = 0; i < AT_REPLY_LEN; i++) {
        xSerialGetChar(NULL, (signed char*) &at_cmd_response[i], RX_BLOCK);
    }

    if(strcmp(at_cmd_response, "\r\nOK\r\n")) {
        esp8266_status = ERROR;
    }
    else {
        esp8266_status = AT_READY;
    }
    return;
}

void start_TCP(const char *pHostName, const char *port) {

    char c;

    //AT Command header
    xSerialPutChar(NULL, 'A', TX_BLOCK);
    xSerialPutChar(NULL, 'T', TX_BLOCK);
    xSerialPutChar(NULL, '+', TX_BLOCK);
    xSerialPutChar(NULL, 'C', TX_BLOCK);
    xSerialPutChar(NULL, 'I', TX_BLOCK);
    xSerialPutChar(NULL, 'P', TX_BLOCK);
    xSerialPutChar(NULL, 'S', TX_BLOCK);
    xSerialPutChar(NULL, 'T', TX_BLOCK);
    xSerialPutChar(NULL, 'A', TX_BLOCK);
    xSerialPutChar(NULL, 'R', TX_BLOCK);
    xSerialPutChar(NULL, 'T', TX_BLOCK);
    xSerialPutChar(NULL, '=', TX_BLOCK);
    xSerialPutChar(NULL, '"', TX_BLOCK);
    xSerialPutChar(NULL, 'T', TX_BLOCK);
    xSerialPutChar(NULL, 'C', TX_BLOCK);
    xSerialPutChar(NULL, 'P', TX_BLOCK);
    xSerialPutChar(NULL, '"', TX_BLOCK);
    xSerialPutChar(NULL, ',', TX_BLOCK);
    xSerialPutChar(NULL, '"', TX_BLOCK);

    //Target IP
    for (int i = 0; *(pHostName + i); i++) {
        xSerialPutChar(NULL, *(pHostName + i), TX_BLOCK);
    }

    xSerialPutChar(NULL, '"', TX_BLOCK);
    xSerialPutChar(NULL, ',', TX_BLOCK);

    //Target TCP Port
    for (int i = 0; *(port + i); i++) {
        xSerialPutChar(NULL, *(port + i), TX_BLOCK);
    }
    xSerialPutChar(NULL, '\r', TX_BLOCK);
    xSerialPutChar(NULL, '\n', TX_BLOCK);

    SLEEP; //so esp8266 has enough time to reply us.
    xSerialGetChar(NULL, (signed char*) &c, NO_BLOCK); //C, if success

    if (c != 'C') {
        esp8266_status = ERROR;
    }
    else {
        esp8266_status = CONNECTED;
    }
    //Clear Rx buffer
    while (xSerialGetChar(NULL, (signed char*) &c, NO_BLOCK));
    return;
}

void *rxThread(void *args) {

    char c;

    //Block until we update esp8266_status in the main therad to AT_READY;
    while(esp8266_status == RX_THREAD_UNINITIALIZED);

    //Keep running till esp8266AT_Disconnect() is called;
    while(esp8266_status > RX_THREAD_UNINITIALIZED) {
        //if (xSerialGetChar(NULL, (signed char*) &c, RX_BLOCK)) {
        //    mq_send(controlQTx, &c, 1, 0);
        //}
    }

    return NULL;
}
