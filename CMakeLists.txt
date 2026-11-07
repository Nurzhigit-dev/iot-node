cmake_minimum_required(VERSION 3.16)
# Firmware version: shown on the status topic and compared before an OTA update.
# Build another version without editing this file:
#   idf.py -B build-ota -DPROJECT_VER=1.0.1 build
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "1.0.0")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(iot-node)
