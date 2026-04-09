CC ?= gcc

CPPFLAGS += -I. -I.. -I../UART\ API
CFLAGS += -std=c11 -O2 -Wall -Wextra -Wpedantic -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -pthread
LDFLAGS ?=
LDLIBS ?=

ifneq ($(shell pkg-config --exists libcjson && echo yes),)
CPPFLAGS += $(shell pkg-config --cflags libcjson)
LDLIBS += $(shell pkg-config --libs libcjson)
else ifneq ($(wildcard /usr/include/cjson/cJSON.h),)
CPPFLAGS += -I/usr/include/cjson
LDLIBS += -lcjson
endif

ifneq ($(shell pkg-config --exists paho-mqtt3c && echo yes),)
CPPFLAGS += $(shell pkg-config --cflags paho-mqtt3c)
LDLIBS += $(shell pkg-config --libs paho-mqtt3c)
else ifneq ($(wildcard /usr/include/MQTTClient.h),)
LDLIBS += -lpaho-mqtt3c
endif

TARGET := ./owl_md860_mcast
OBJS := \
	build/main.o \
	build/api2_mcast_win.o \
	build/button_led_rx.o \
	build/camera_iface.o \
	build/camera_mqtt.o \
	build/camera_params.o \
	build/ini_parser.o \
	build/joystick_rx.o \
	build/mqtt.o \
	build/tracker.o \
	build/platform_compat.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o "$@" $(LDFLAGS) $(LDLIBS)

build/main.o: main.c button_led_rx.h camera_iface.h joystick_rx.h ini_parser.h camera_params.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/api2_mcast_win.o: api2_mcast_win.c api2_mcast_win.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/button_led_rx.o: button_led_rx.c button_led_rx.h api2_mcast_win.h camera_iface.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/camera_iface.o: camera_iface.c camera_iface.h camera_mqtt.h camera_params.h tracker.h api2_mcast_win.h mqtt.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/camera_mqtt.o: camera_mqtt.c camera_mqtt.h camera_params.h mqtt.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/camera_params.o: camera_params.c camera_params.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/ini_parser.o: ini_parser.c ini_parser.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/joystick_rx.o: joystick_rx.c joystick_rx.h api2_mcast_win.h tracker.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/mqtt.o: mqtt.c mqtt.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/tracker.o: tracker.c tracker.h camera_iface.h api2_mcast_win.h mqtt.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build/platform_compat.o: ../platform_compat.c ../platform_compat.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

build:
	mkdir -p build

clean:
	rm -rf build "$(TARGET)"
