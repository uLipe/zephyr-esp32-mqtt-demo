# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr-esp32-mqtt-demo)

FILE(GLOB app_sources main.c)
target_sources(app PRIVATE ${app_sources})
