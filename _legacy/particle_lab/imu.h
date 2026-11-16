// Minimal QMI8658 accelerometer driver for the Waveshare ESP32-S3-LCD-1.47B.
//
// Only the accelerometer is configured: the simulator needs gravity, not
// rotation. Register addresses and the init sequence follow Waveshare's
// reference code for this board (I2C SDA=48, SCL=47, address 0x6B).

#pragma once

#include <Arduino.h>

// Returns false when the sensor does not answer, or answers with an
// implausible revision id.
bool imuBegin();

// Reads accelerometer in g (1.0 == Earth gravity). Values are the reaction
// force, so a board standing still and upright reports +1g on its up axis.
void imuReadAccel(float &ax, float &ay, float &az);
