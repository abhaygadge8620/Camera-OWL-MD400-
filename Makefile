CC ?= gcc

CPPFLAGS += -I. -I./OWL_MD860 -I./UART\ API
CFLAGS += -std=c11 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

PAHO_CFLAGS := $(shell pkg-config --cflags paho-mqtt3c 2>/dev/null)
PAHO_LIBS := $(shell pkg-config --libs paho-mqtt3c 2>/dev/null)
CPPFLAGS += $(PAHO_CFLAGS)
LDLIBS += $(if $(strip $(PAHO_LIBS)),$(PAHO_LIBS),-lpaho-mqtt3c)

TARGET := bin/wcsbtncam

OWL_SRCS := $(wildcard OWL_MD860/*.c)
OWL_NAMES := $(notdir $(basename $(OWL_SRCS)))

OBJS := \
	build/main.o \
	build/platform_compat.o \
	build/camera_command_router.o \
	build/led_status_router.o \
	$(addprefix build/owl_,$(addsuffix .o,$(OWL_NAMES))) \
	build/uart_config_ini.o \
	build/uart_uart_protocol.o \
	build/uart_uart_win.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) | bin
	$(CC) $(CFLAGS) $(OBJS) -o "$@" $(LDFLAGS) $(LDLIBS)

build/main.o: main.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/platform_compat.o: platform_compat.c platform_compat.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/camera_command_router.o: camera_command_router.c camera_command_router.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/led_status_router.o: led_status_router.c led_status_router.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/owl_%.o: OWL_MD860/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/uart_config_ini.o: UART\ API/config_ini.c UART\ API/config_ini.h UART\ API/controls.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/uart_uart_protocol.o: UART\ API/uart_protocol.c UART\ API/uart_protocol.h UART\ API/uart_win.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/uart_uart_win.o: UART\ API/uart_win.c UART\ API/uart_win.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build:
	mkdir -p build

bin:
	mkdir -p bin

clean:
	rm -rf build bin
