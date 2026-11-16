#include "imu.h"

#include <Wire.h>

#define QMI_ADDR 0x6B

#define REG_REVISION 0x01
#define REG_CTRL1 0x02
#define REG_CTRL2 0x03
#define REG_CTRL5 0x06
#define REG_CTRL6 0x07
#define REG_CTRL7 0x08
#define REG_AX_L 0x35

// 4 g full scale over a signed 16-bit range.
static float s_accelScale = 4.0f / 32768.0f;

static void writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(QMI_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

static uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(QMI_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(QMI_ADDR, 1) != 1) return 0;
  return Wire.read();
}

bool imuBegin() {
  Wire.begin(48, 47);  // SDA, SCL - fixed by the board layout
  Wire.setClock(400000);

  uint8_t rev = readReg(REG_REVISION);
  if (rev == 0x00 || rev == 0xFF) return false;

  // CTRL1: enable the 2 MHz oscillator, auto-increment addresses for burst
  // reads. Bit 0 low = oscillator on, bit 6 high = auto-increment.
  uint8_t ctrl1 = readReg(REG_CTRL1);
  ctrl1 &= ~0x01;
  ctrl1 |= 0x40;
  writeReg(REG_CTRL1, ctrl1);

  // CTRL7: accelerometer and gyro on, high-speed clock, sync sampling off.
  writeReg(REG_CTRL7, 0x43);

  // CTRL6: no motion-on-demand.
  writeReg(REG_CTRL6, 0x00);

  // CTRL2: ODR bits 0-3 stay at their fastest setting (0), full scale bits
  // 4-6 set to 4 g (0b001).
  uint8_t ctrl2 = readReg(REG_CTRL2);
  ctrl2 &= ~0x7F;
  ctrl2 |= (0x01 << 4);
  writeReg(REG_CTRL2, ctrl2);

  // CTRL5: accelerometer low-pass filter on, mode 0.
  writeReg(REG_CTRL5, 0x01);

  return true;
}

void imuReadAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(QMI_ADDR);
  Wire.write(REG_AX_L);
  if (Wire.endTransmission(false) != 0) {
    ax = ay = az = 0.0f;
    return;
  }
  if (Wire.requestFrom(QMI_ADDR, 6) != 6) {
    ax = ay = az = 0.0f;
    return;
  }
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();

  int16_t rawX = (int16_t)((b[1] << 8) | b[0]);
  int16_t rawY = (int16_t)((b[3] << 8) | b[2]);
  int16_t rawZ = (int16_t)((b[5] << 8) | b[4]);

  ax = rawX * s_accelScale;
  ay = rawY * s_accelScale;
  az = rawZ * s_accelScale;
}

// CTRL1 enables address auto-increment, so accelerometer and gyro come back in
// one 12-byte burst starting at AX_L.
void imuReadAll(float a[3], float g[3]) {
  a[0] = a[1] = a[2] = 0.0f;
  g[0] = g[1] = g[2] = 0.0f;

  Wire.beginTransmission(QMI_ADDR);
  Wire.write(REG_AX_L);
  if (Wire.endTransmission(false) != 0) return;
  if (Wire.requestFrom(QMI_ADDR, 12) != 12) return;

  uint8_t b[12];
  for (int i = 0; i < 12; i++) b[i] = Wire.read();

  // Gyro is configured for 64 dps full scale.
  const float gyroScale = 64.0f / 32768.0f;

  for (int axis = 0; axis < 3; axis++) {
    int16_t rawA = (int16_t)((b[axis * 2 + 1] << 8) | b[axis * 2]);
    int16_t rawG = (int16_t)((b[6 + axis * 2 + 1] << 8) | b[6 + axis * 2]);
    a[axis] = rawA * s_accelScale;
    g[axis] = rawG * gyroScale;
  }
}
