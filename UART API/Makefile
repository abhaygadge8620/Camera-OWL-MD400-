CC ?= gcc

CPPFLAGS += -I. -I.. -I../OWL_MD860
CFLAGS += -std=c11 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -pthread
LDFLAGS ?=
LDLIBS ?=

TARGET := uart_panel_bridge
OBJS := \
	build/main.o \
	build/config_ini.o \
	build/uart_protocol.o \
	build/uart_win.o \
	build/button_mcast.o \
	build/ini_parser.o \
	build/platform_compat.o \
	build/api2_mcast_win.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o "$@" $(LDFLAGS) $(LDLIBS)

build/main.o: main.c config_ini.h button_mcast.h uart_protocol.h uart_win.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/config_ini.o: config_ini.c config_ini.h controls.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/uart_protocol.o: uart_protocol.c uart_protocol.h uart_win.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/uart_win.o: uart_win.c uart_win.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/button_mcast.o: button_mcast.c button_mcast.h controls.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/ini_parser.o: ini_parser.c ini_parser.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/platform_compat.o: ../platform_compat.c ../platform_compat.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/api2_mcast_win.o: ../OWL_MD860/api2_mcast_win.c ../OWL_MD860/api2_mcast_win.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build:
	mkdir -p build

clean:
	rm -rf build "$(TARGET)"
