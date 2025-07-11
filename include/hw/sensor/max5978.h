#ifndef MAX5978_H
#define MAX5978_H

#include "hw/i2c/i2c.h"

#define TYPE_MAX5978 "max5978"
#define MAX5978(obj) OBJECT_CHECK(MAX5978State, (obj), TYPE_MAX5978)

#define MAX5978_NUM_REGS  0x44  // Registers 0x00–0x43

typedef struct {
    I2CSlave i2c;
    uint8_t regs[MAX5978_NUM_REGS];
    uint8_t pointer;
    uint8_t len;
} MAX5978State;

#endif // MAX5978_H
