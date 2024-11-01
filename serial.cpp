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

#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include "serial.h"

//constants
char const *serialPortName = "/dev/ttyUSB0";

//global variables
static int serial_fd = 0;
static unsigned int bufferLen = 0;
static char *rxBuffer;
static char *txBuffer;
static unsigned int rxPos = 0;
static unsigned int txPos = 0;
static pthread_mutex_t rxBufferLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t txBufferLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t comThreads[2]; //rx and tx threads respectivelly
static int run = 0; //threads will run while run != 0

void *rxThread(void *args);
void *txThread(void *args);

xComPortHandle xSerialPortInitMinimal(unsigned long ulWantedBaud, unsigned portBASE_TYPE uxQueueLength) {

    int rc;
    serial_fd = open(serialPortName, O_RDWR);

    if (!serial_fd) {
        perror("Could not open '/dev/ttyUSB0'");
        exit(-1);
    }

    bufferLen = uxQueueLength;
    rxBuffer = (char*) malloc(bufferLen);
    assert(rxBuffer);
    txBuffer = (char*) malloc(bufferLen);
    assert(txBuffer);

    //these threads will only stop when run == 0;
    run = 1;
    rc = pthread_create(&comThreads[0], NULL, rxThread, NULL);
    assert(!rc);
    rc = pthread_create(&comThreads[1], NULL, txThread, NULL);
    assert(!rc);

    return NULL;
}

signed portBASE_TYPE xSerialGetChar(xComPortHandle pxPort, signed char *pcRxedChar, TickType_t xBlockTime) {

    if (!serial_fd) {
        errno = EBADF;
        perror("/dev/ttyUSB0 not ready. Did you call xSerialPortInitMinimal first?");
        exit(-1);
    }

    if (rxPos) {
        pthread_mutex_lock(&rxBufferLock);
        *pcRxedChar = *rxBuffer;
        for (unsigned int i = 0; i < rxPos - 1; i++) {
            rxBuffer[i] = rxBuffer[i+1];
        }
        rxPos--;
        pthread_mutex_unlock(&rxBufferLock);
        return 1;
    }
    else {
        return 0;
    }
}

signed portBASE_TYPE xSerialPutChar(xComPortHandle pxPort, signed char cOutChar, TickType_t xBlockTime) {

    if (!serial_fd) {
        errno = EBADF;
        perror("/dev/ttyUSB0 not ready. Did you call xSerialPortInitMinimal first?");
        exit(-1);
    }

    if (txPos < bufferLen) {
        pthread_mutex_lock(&txBufferLock);
        *(txBuffer + txPos) = cOutChar;
        txPos++;
        pthread_mutex_unlock(&txBufferLock);
        return 1;
    }
    else {
        return 0;
    }
}

void vSerialClose(xComPortHandle xPort) {

    int rc;

    if (serial_fd) {
        //stop threads.
        run = 0; rxPos = 0; txPos = 0;
        //rx thread may be blocked at read...
        //thus send some data. The esp8266 device will echo it unblocking the rxThread...
        write(serial_fd, txBuffer, 1);
        rc = pthread_join(comThreads[0], NULL);
        assert(!rc);
        rc = pthread_join(comThreads[1], NULL);
        assert(!rc);

        //free rx and tx buffers
        free(rxBuffer);
        free(txBuffer);
        bufferLen = 0;

        //close serial_fd
        rc = close(serial_fd);
        assert(!rc);
    }

    return;
}

void *rxThread(void *args) {
    char c;
    while (run) {
        if (read(serial_fd, &c, 1) == -1) { //read will block till there is something to read in serial device.
            perror("Error trying to read from serial device.");
            run = 0;
            break;
        }
check_rx_buffer:
        if (rxPos < bufferLen) {
            pthread_mutex_lock(&rxBufferLock);
            *(rxBuffer + rxPos) = c;
            rxPos++;
            pthread_mutex_unlock(&rxBufferLock);
        }
        else {
            sleep(1); //block, then check for available space in rxBuffer;
            goto check_rx_buffer;
        }
    }

    return NULL;
}

void *txThread(void *args) {
    char c;
    while (run) {
        if (txPos) { //bytes available to send?
            pthread_mutex_lock(&txBufferLock);
            c = *txBuffer;
            for (unsigned int i = 0; i < txPos - 1; i++) {
                txBuffer[i] = txBuffer[i+1];
            }
            txPos--;
            pthread_mutex_unlock(&txBufferLock);
            if (write(serial_fd, &c, 1) == -1) {
                perror("Error trying to write to serial device.");
                run = 0;
                break;
            }
        }
        else {
            sleep(1); //block, then check for bytes in tx_Buffer again.
        }
    }

    return NULL;
}