SHELL=/usr/bin/sh

CFLAGS = -g -Wall -I. -I./coreMQTT/source/include -I./coreMQTT/source/interface
CXXFLAGS = $(CFLAGS)

all: app

#coreMQTT library
CORE_MQTT = \
	core_mqtt.o \
	core_mqtt_state.o \
	core_mqtt_serializer.o

core_mq%.o: ./coreMQTT/source/core_mq%.c
	$(CC) $(CFLAGS) -c $< -o $@

#Transport Interface
OBJS = \
	serial.o \
	transport_esp8266.o \

#Transport test app
test: $(OBJS) test_transport.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

#Main mqtt client app
app: $(CORE_MQTT) $(OBJS) main.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

.PHONY: clean
clean:
	@$(RM) *.o
	@for item in $$(list_executables); do $(RM) $$item; done;
