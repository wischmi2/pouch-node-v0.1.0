# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Include app.overlay so w1/DS18B20 and I2C nodes are merged (board overlay is applied first)
set(EXTRA_DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble_gatt_example)

target_sources(app PRIVATE
  src/main.c
  src/credentials.c
  src/fatal_error.c
  src/fw_update.c
  src/sensors/sensors.c
  src/sensors/ph2_sensor.c
  src/sensors/temp_sensor.c
)
