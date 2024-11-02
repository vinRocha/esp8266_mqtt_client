#include <cstdio>
#include <iostream>
#include <unistd.h>
#include "transport_esp8266.h"

int main(int argc, char * argv[]) {

    esp8266TransportStatus_t rc;
    char const *address = "192.168.0.235";
    char const *port = "1883";

    rc = esp8266AT_Connect(address, port);
    std::cout << "rc: " << rc << std::endl;

    getchar();

    esp8266AT_Disconnect();

    return 0;
}