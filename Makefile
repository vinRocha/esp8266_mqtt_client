SHELL=/usr/bin/sh

CXXFLAGS = -Wall -I.

OBJS = \
	serial.o \
	transport_esp8266.o \
	main.o

all: test
test: $(OBJS)
	@$(CXX) $(CXXFLAGS) $^ -o $@


%.o: %.cpp
	@$(CXX) -c $(CXXFLAGS) $<


.PHONY: clean
clean:
	@$(RM) *.o
	@for item in $$(list_executables); do $(RM) $$item; done;
