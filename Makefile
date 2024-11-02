SHELL=/usr/bin/sh

CXXFLAGS = -g -Wall -I.

OBJS = \
	serial.o \
	transport_esp8266.o \
	main.o

all: test
test: $(OBJS)
	$(CXX) -o $@ $^

.PHONY: clean
clean:
	@$(RM) *.o
	@for item in $$(list_executables); do $(RM) $$item; done;
