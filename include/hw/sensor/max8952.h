// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MAX8952_H
#define MAX8952_H

#include "hw/i2c/i2c.h"

#define TYPE_MAX8952 "max8952"
#define MAX8952(obj) OBJECT_CHECK(MAX8952State, (obj), TYPE_MAX8952)

#define MAX8952_NUM_REGS  0x0A  // 0x00 to 0x09

// Register Addresses
enum {
    MAX8952_REG_MODE0   = 0x00,
    MAX8952_REG_MODE1   = 0x01,
    MAX8952_REG_MODE2   = 0x02,
    MAX8952_REG_MODE3   = 0x03,
    MAX8952_REG_CONTROL = 0x04,
    MAX8952_REG_SYNC    = 0x05,
    MAX8952_REG_RAMP    = 0x06,
    MAX8952_REG_CHIPID1 = 0x08,
    MAX8952_REG_CHIPID2 = 0x09,
};

typedef struct MAX8952State {
    I2CSlave i2c;
    uint8_t regs[MAX8952_NUM_REGS];
    uint8_t len;
    uint8_t pointer;
} MAX8952State;

#endif // MAX8952_H
