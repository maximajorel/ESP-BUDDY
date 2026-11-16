#include "imu.h"

#include <Wire.h>

// Unshifted 7-bit address of the QMI8658 on this board.
#define QMI_ADDR 0x6B

#define REG_REVISION 0x01
#define REG_CTRL1 0x02
#define REG_CTRL2 0x03
#define REG_CTRL3 0x04
#define REG_CTRL5 0x06
#define REG_CTRL6 0x07
#define REG_CTRL7 0x08
#define REG_AX_L 0x35

// Accelerometer: 4 g full scale over a signed 16-bit range.
static const float ACCEL_SCALE = 4.0f / 32768.0f;
// Gyroscope: IMU_GYRO_FS_DPS full scale over a signed 16-bit range.
static const float GYRO_SCALE = IMU_GYRO_FS_DPS / 32768.0f;

static bool s_fake = false;
static float s_fakeAccel[3] = {0.0f, 0.0f, 1.0f};
static float s_fakeGyro[3] = {0.0f, 0.0f, 0.0f};

void imuSetFake(const float accel[3], const float gyro[3], bool enable) {
  for (int i = 0; i < 3; i++) {
    s_fakeAccel[i] = accel ? accel[i] : 0.0f;
    s_fakeGyro[i] = gyro ? gyro[i] : 0.0f;
  }
  s_fake = enable;
}

bool imuFakeEnabled() { return s_fake; }

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

  // CTRL1: oscillator on and address auto-increment for burst reads.
  uint8_t ctrl1 = readReg(REG_CTRL1);
  ctrl1 &= ~0x01;
  ctrl1 |= 0x40;
  writeReg(REG_CTRL1, ctrl1);

  // CTRL7: accelerometer and gyro on, high-speed clock, sync sampling off.
  writeReg(REG_CTRL7, 0x43);
  writeReg(REG_CTRL6, 0x00);

  // CTRL2: accelerometer ODR 896 Hz, full scale 4 g.
  writeReg(REG_CTRL2, (uint8_t)((0x01 << 4) | 0x04));

  // CTRL3: gyro ODR 896 Hz, full scale set by the GFS bits. Bits 6:4 hold the
  // scale: 010 = 64 dps, 011 = 128, 100 = 256, 101 = 512. The driver reports
  // IMU_GYRO_FS_DPS so the two cannot drift apart.
  if (IMU_GYRO_FS_DPS <= 64.0f) {
    writeReg(REG_CTRL3, (uint8_t)((0x02 << 4) | 0x04));
  } else if (IMU_GYRO_FS_DPS <= 128.0f) {
    writeReg(REG_CTRL3, (uint8_t)((0x03 << 4) | 0x04));
  } else if (IMU_GYRO_FS_DPS <= 256.0f) {
    writeReg(REG_CTRL3, (uint8_t)((0x04 << 4) | 0x04));
  } else {
    writeReg(REG_CTRL3, (uint8_t)((0x05 << 4) | 0x04));
  }

  // CTRL5: accelerometer low-pass filter on, mode 0.
  writeReg(REG_CTRL5, 0x01);

  return true;
}

void imuReadAccel(float &ax, float &ay, float &az) {
  ax = ay = az = 0.0f;

  if (s_fake) {
    ax = s_fakeAccel[0];
    ay = s_fakeAccel[1];
    az = s_fakeAccel[2];
    return;
  }

  Wire.beginTransmission(QMI_ADDR);
  Wire.write(REG_AX_L);
  if (Wire.endTransmission(false) != 0) return;
  if (Wire.requestFrom(QMI_ADDR, 6) != 6) return;

  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();

  ax = (int16_t)((b[1] << 8) | b[0]) * ACCEL_SCALE;
  ay = (int16_t)((b[3] << 8) | b[2]) * ACCEL_SCALE;
  az = (int16_t)((b[5] << 8) | b[4]) * ACCEL_SCALE;
}

// CTRL1 enables address auto-increment, so accelerometer and gyro come back in
// one 12-byte burst starting at AX_L.
void imuReadAll(float a[3], float g[3]) {
  a[0] = a[1] = a[2] = 0.0f;
  g[0] = g[1] = g[2] = 0.0f;

  if (s_fake) {
    for (int i = 0; i < 3; i++) {
      a[i] = s_fakeAccel[i];
      g[i] = s_fakeGyro[i];
    }
    return;
  }

  Wire.beginTransmission(QMI_ADDR);
  Wire.write(REG_AX_L);
  if (Wire.endTransmission(false) != 0) return;
  if (Wire.requestFrom(QMI_ADDR, 12) != 12) return;

  uint8_t b[12];
  for (int i = 0; i < 12; i++) b[i] = Wire.read();

  for (int axis = 0; axis < 3; axis++) {
    int16_t rawA = (int16_t)((b[axis * 2 + 1] << 8) | b[axis * 2]);
    int16_t rawG = (int16_t)((b[6 + axis * 2 + 1] << 8) | b[6 + axis * 2]);
    a[axis] = rawA * ACCEL_SCALE;
    g[axis] = rawG * GYRO_SCALE;
  }
}
