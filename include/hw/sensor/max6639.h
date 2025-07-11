// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MAX6639_H
#define MAX6639_H

#include "hw/i2c/i2c.h"

#define TYPE_MAX6639 "max6639"
#define MAX6639(obj) OBJECT_CHECK(MAX6639State, (obj), TYPE_MAX6639)

#define MAX6639_NUM_REGS  0x14

typedef struct {
    I2CSlave i2c;
    uint8_t regs[MAX6639_NUM_REGS];
    uint8_t pointer;
    uint8_t len;
} MAX6639State;

#endif
