CC ?= gcc
AR ?= ar

CPPFLAGS += -I.
CFLAGS += -std=c11 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L

TARGET := libuart_bridge.a
OBJS := build/config_ini.o build/uart_protocol.o build/uart_win.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) rcs "$@" $(OBJS)

build/config_ini.o: config_ini.c config_ini.h controls.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/uart_protocol.o: uart_protocol.c uart_protocol.h uart_win.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/uart_win.o: uart_win.c uart_win.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build:
	mkdir -p build

clean:
	rm -rf build "$(TARGET)"
