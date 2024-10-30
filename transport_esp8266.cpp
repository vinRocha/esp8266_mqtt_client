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

#include <cstdint>
#include <cstring>
#include "transport_esp8266.h"
#include "serial.h"

//This should be commented out in FreeRTOS port.
typedef uint16_t TickType_t;

//constants
int const BUFFER_LEN = 128;
unsigned long const BAUD_RATE = 115200;
TickType_t const RX_BLOCK = 0xff;
TickType_t const TX_BLOCK = 0x00;
TickType_t const NO_BLOCK = 0x00;
int const AT_REPLY_LEN = 9;

enum esp8266Status { //internal status
    AT_UNINITIALIZED = 0,
    AT_READY,
    DISCONNECTED,
    CONNECTED,
    ERROR
};

static char esp8266_status = COM_UNINITIALIZED;
static char at_cmd_response[AT_REPLY_LEN] = {0};

static esp8266Status check_AT(void);
static void send_AT_command(void);
static void recv_AT_reply(void);

esp8266Status check_AT(void) {
    //Clear RX buffer
    while (xSerialGetChar(NULL, at_cmd_response, NO_BLOCK));
    inline send_AT_command();
    inline recv_AT_reply();

    if(strcmp(at_cmd_response, "AT\r\nOK\r\n")) {
        return ERROR;
    }
    else {
        return AT_READY;
    }
}

void send_AT_command(void) {
    //Send AT command
    xSerialPutChar(NULL, 'A', TX_BLOCK);
    xSerialPutChar(NULL, 'T', TX_BLOCK);
    xSerialPutChar('\r');
    xSerialPutChar('\n');
}

void recv_AT_reply(void) {
    for (int i = 0; i < AT_REPLY_LEN, i++) {
        xSerialGetChar(NULL, at_cmd_response[i], RX_BLOCK);
    }
}

esp8266TransportStatus_t esp8266AT_Connect(const char *pHostName, uint16_t port) {

    char c = 0;

    if (esp8266_status == CONNECTED) {
        return ESP8266_TRANSPORT_SUCCESS;
    }

    if (*pHostName != '"' || port == 0) {
        return ESP8266_TRANSPORT_INVALID_PARAMETER
    }

    if (es8266_status == COM_UNINITIALIZED) {
        xSerialPortInitMinimal(BAUD_RATE, BUFFER_LEN);
        esp8266_status = COM_UNINITIALIZED + 1;
    }

    if (esp8266_status = check_AT() == ERROR) {
        return ESP8266_TRANSPORT_CONNECT_FAILURE;
    }

}

esp8266TransportStatus_t esp8266AT_Disconnect(void) {

}

int32_t esp8266AT_recv(NetworkContext_t *pNetworkContext,
                        void * pBuffer,
                        size_t bytesToRecv) {
    return 0;
}

int32_t esp8266AT_send(NetworkContext_t *pNetworkContext,
                        const void *pBuffer,
                        size_t bytesToSend) {
    return 0;
}
