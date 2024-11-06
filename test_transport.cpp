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

#include <cstdio>
#include <string>
#include <iostream>
#include <unistd.h>
#include "transport_esp8266.h"

const int buffer_len = 128;

int main(int argc, char * argv[]) {

    char c = 0;
    esp8266TransportStatus_t rc;
    const char *address = "192.168.0.235";
    const char *port = "1883";
    std::string line;

    rc = esp8266AT_Connect(address, port);
    std::cout << "esp8266AT_Connect: " << rc << std::endl;

    if (rc != ESP8266_TRANSPORT_SUCCESS) {
        return -1;
    }

    char buffer[buffer_len];
    int32_t bytes_sent;
    int32_t bytes_read;
    while (c != '1') {
        std::cout << "Enter bytes to send: " << std::endl;
        std::getline(std::cin, line);
        std::cout << "Got " << line.size() << " bytes to send." << std::endl;
        bytes_sent = esp8266AT_send(NULL, line.c_str(), line.size());
        std::cout << "Sent: " << bytes_sent << " bytes." << std::endl;
        bytes_read = esp8266AT_recv(NULL, buffer, buffer_len - 1);
        std::cout << "Read: " << bytes_read << " bytes." << std::endl;
        if (bytes_read) {
            buffer[bytes_read] = 0;
            std::cout << "Bytes read: " << buffer << std::endl;
        }
        std::cout << "Press enter to try again or 1 + enter to exit." << std::endl;
        c = getchar();
    }

    esp8266AT_Disconnect();

    return 0;
}
