// QMI8658 accelerometer + gyroscope driver for the Waveshare ESP32-S3-LCD-1.47B.
//
// The dodge game steers with the gyroscope, so unlike the earlier particle
// simulator this driver configures the gyro full scale explicitly instead of
// trusting the chip default.
#pragma once

#include <Arduino.h>

// Returns false when the sensor does not answer, or answers with an
// implausible revision id.
bool imuBegin();

// Reads accelerometer in g (1.0 == Earth gravity). Values are the reaction
// force, so a board standing still and upright reports +1g on its up axis.
void imuReadAccel(float &ax, float &ay, float &az);

// Reads accelerometer (g) and gyroscope (degrees per second) in one burst.
void imuReadAll(float a[3], float g[3]);

// Replaces the sensor reading with a fixed value. This is how the game's
// steering can be driven and verified from the serial console without anyone
// physically tilting the board.
void imuSetFake(const float accel[3], const float gyro[3], bool enable);
bool imuFakeEnabled();

// Gyro full scale in degrees per second, as configured by imuBegin().
#define IMU_GYRO_FS_DPS 512.0f
