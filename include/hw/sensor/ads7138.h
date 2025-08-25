#ifndef ADS7138_H
#define ADS7138_H

#include "hw/i2c/i2c.h"

#define TYPE_ADS7138 "ads7138"
#define ADS7138(obj) OBJECT_CHECK(ADS7138State, (obj), TYPE_ADS7138)

#define ADS7138_NUM_REGS 0xEC  // Max register used is 0xEB, align to 0xEC

typedef struct {
    I2CSlave i2c;
    uint8_t regs[ADS7138_NUM_REGS];
    uint8_t pointer;
    uint8_t len;
} ADS7138State;

#endif // ADS7138_H
