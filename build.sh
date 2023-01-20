#!/bin/bash

ARDUINO_RUNTIME=~/.arduino15
ARDUINO_HOME=/usr/share/arduino
INO_FILE=ESPEInk_ESP8266.ino
LIBRARY_PATH=~/Arduino/libraries
BOARD_DEFINITION="esp8266:esp8266:nodemcuv2:xtal=80,vt=flash,exception=disabled,stacksmash=disabled,ssl=all,mmu=3232,non32xfer=fast,eesz=4M2M,led=2,ip=lm2f,dbg=Disabled,lvl=None____,wipe=none,baud=115200"
TOOL_VERSION=3.1.0-gcc10.3-e5f9fec
FS_VERSION=${FS_VERSION}
IDE_VERSION=10819

# prepare
mkdir -p /tmp/ESP8266/cache

# build
arduino-builder \
	-dump-prefs \
	-hardware ${ARDUINO_HOME}/hardware \
	-hardware ${ARDUINO_RUNTIME}/packages \
	-tools ${ARDUINO_HOME}/hardware/tools/avr \
	-tools ${ARDUINO_RUNTIME}/packages \
	-libraries ${LIBRARY_PATH} \
	-fqbn=${BOARD_DEFINITION} \
	-ide-version=${IDE_VERSION} \
	-build-path /tmp/ESP8266 \
	-warnings=all \
	-build-cache /tmp/ESP8266/cache \
	-prefs=build.warn_data_percentage=75 \
	-prefs=runtime.tools.mkspiffs.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/mkspiffs/${TOOL_VERSION} \
	-prefs=runtime.tools.mkspiffs-${TOOL_VERSION}.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/mkspiffs/${TOOL_VERSION} \
	-prefs=runtime.tools.xtensa-lx106-elf-gcc.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/xtensa-lx106-elf-gcc/${TOOL_VERSION} \
	-prefs=runtime.tools.xtensa-lx106-elf-gcc-${TOOL_VERSION}.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/xtensa-lx106-elf-gcc/${TOOL_VERSION} \
	-prefs=runtime.tools.python3.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/python3/3.7.2-post1 \
	-prefs=runtime.tools.python3-3.7.2-post1.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/python3/3.7.2-post1 \
	-prefs=runtime.tools.mklittlefs.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/mklittlefs/${FS_VERSION} \
	-prefs=runtime.tools.mklittlefs-${FS_VERSION}.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/mklittlefs/${FS_VERSION} \
	-verbose \
	${INO_FILE}

arduino-builder \
	-compile \
	-hardware ${ARDUINO_HOME}/hardware \
	-hardware ${ARDUINO_RUNTIME}/packages \
	-tools ${ARDUINO_HOME}/hardware/tools/avr \
	-tools ${ARDUINO_RUNTIME}/packages \
	-libraries ${LIBRARY_PATH} \
	-fqbn=${BOARD_DEFINITION} \
	-ide-version=${IDE_VERSION} \
	-build-path /tmp/ESP8266 \
	-warnings=all \
	-build-cache /tmp/ESP8266/cache \
	-prefs=build.warn_data_percentage=75 \
	-prefs=runtime.tools.mkspiffs.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/mkspiffs/${TOOL_VERSION} \
	-prefs=runtime.tools.mkspiffs-${TOOL_VERSION}.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/mkspiffs/${TOOL_VERSION} \
	-prefs=runtime.tools.xtensa-lx106-elf-gcc.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/xtensa-lx106-elf-gcc/${TOOL_VERSION} \
	-prefs=runtime.tools.xtensa-lx106-elf-gcc-${TOOL_VERSION}.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/xtensa-lx106-elf-gcc/${TOOL_VERSION} \
	-prefs=runtime.tools.python3.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/python3/3.7.2-post1 \
	-prefs=runtime.tools.python3-3.7.2-post1.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/python3/3.7.2-post1 \
	-prefs=runtime.tools.mklittlefs.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/mklittlefs/${FS_VERSION} \
	-prefs=runtime.tools.mklittlefs-${FS_VERSION}.path=${ARDUINO_RUNTIME}/packages/esp8266/tools/mklittlefs/${FS_VERSION} \
	-verbose \
	${INO_FILE}

# upload to local firmware server "volker"
echo
echo "uploading to firmware server..."
scp /tmp/ESP8266/ESPEInk_ESP8266.ino.bin volker:

echo
echo "compressing image..."
gzip -f /tmp/ESP8266/ESPEInk_ESP8266.ino.bin
