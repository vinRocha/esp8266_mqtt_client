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