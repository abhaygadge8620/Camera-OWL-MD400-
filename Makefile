CC := gcc

CFLAGS := -std=c11 -O2 -Wall -Wextra -I. -I./OWL_MD860 -I"./UART API"
LDLIBS := -lws2_32 -lkernel32 -lpaho-mqtt3c

TARGET := bin/wcsbtncam.exe

OWL_SRCS := $(wildcard OWL_MD860/*.c)
OWL_NAMES := $(notdir $(basename $(OWL_SRCS)))

OBJS := build/main.o \
	build/camera_command_router.o \
	build/led_status_router.o \
	$(addprefix build/owl_,$(addsuffix .o,$(OWL_NAMES))) \
	build/uart_config_ini.o \
	build/uart_uart_protocol.o \
	build/uart_uart_win.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) | bin
	$(CC) $(OBJS) -o "$@" $(LDLIBS)

build/main.o: main.c | build
	$(CC) $(CFLAGS) -c "$<" -o "$@"

build/camera_command_router.o: camera_command_router.c camera_command_router.h | build
	$(CC) $(CFLAGS) -c "$<" -o "$@"

build/led_status_router.o: led_status_router.c led_status_router.h | build
	$(CC) $(CFLAGS) -c "$<" -o "$@"

build/owl_%.o: OWL_MD860/%.c | build
	$(CC) $(CFLAGS) -c "$<" -o "$@"

build/uart_%.o: UART\ API/%.c | build
	$(CC) $(CFLAGS) -c "$<" -o "$@"

build:
	mkdir -p build

bin:
	mkdir -p bin

clean:
	rm -rf build bin
