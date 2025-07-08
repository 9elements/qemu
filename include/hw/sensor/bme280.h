// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BME280_H
#define BME280_H

#include "hw/i2c/i2c.h"

#define TYPE_BME280 "bme280"
#define BME280(obj) OBJECT_CHECK(BME280State, (obj), TYPE_BME280)

// Total number of internal registers (covers calibration, config, data)
#define NUM_REGISTERS   0xFF

// Register addresses (partial, relevant ones)
#define BME280_REG_CALIB00    0x88
#define BME280_REG_ID         0xD0
#define BME280_REG_RESET      0xE0
#define BME280_REG_CTRL_HUM   0xF2
#define BME280_REG_STATUS     0xF3
#define BME280_REG_CTRL_MEAS  0xF4
#define BME280_REG_CONFIG     0xF5
#define BME280_REG_PRESS_MSB  0xF7
#define BME280_REG_TEMP_MSB   0xFA
#define BME280_REG_HUM_MSB    0xFD

typedef struct BME280State {
    I2CSlave i2c;                  // Inherits from I2CSlave
    uint8_t regs[NUM_REGISTERS];  // Internal register array
    uint8_t len;                  // Number of bytes received
    uint8_t pointer;              // Register pointer for read/write
} BME280State;

#endif // BME280_H
