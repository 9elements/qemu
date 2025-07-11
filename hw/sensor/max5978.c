// SPDX-License-Identifier: GPL-2.0-or-later

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"

#define TYPE_MAX5978 "max5978"
#define MAX5978(obj) OBJECT_CHECK(MAX5978State, (obj), TYPE_MAX5978)

#define MAX5978_NUM_REGS 0x44

typedef struct {
    I2CSlave i2c;
    uint8_t regs[MAX5978_NUM_REGS];
    uint8_t pointer;
    uint8_t len;
} MAX5978State;

static void max5978_reset(DeviceState *dev)
{
    MAX5978State *s = MAX5978(dev);
    memset(s->regs, 0, sizeof(s->regs));

    // ADC Current (0x00-0x01): 0x1A3 (419)
    s->regs[0x00] = 0x1A;
    s->regs[0x01] = 0x03;

    // ADC Voltage (0x02-0x03): 0x2F0 (752)
    s->regs[0x02] = 0x2F;
    s->regs[0x03] = 0x00;

    // Min/Max current
    s->regs[0x08] = 0xFF; s->regs[0x09] = 0x03;  // min
    s->regs[0x0A] = 0x00; s->regs[0x0B] = 0x00;  // max

    // Min/Max voltage
    s->regs[0x0C] = 0xFF; s->regs[0x0D] = 0x03;
    s->regs[0x0E] = 0x00; s->regs[0x0F] = 0x00;

    s->pointer = 0;
    s->len = 0;
}

static uint8_t max5978_read(MAX5978State *s, uint8_t reg)
{
    if (reg >= MAX5978_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "MAX5978: Read out-of-bounds: 0x%02x\n", reg);
        return 0xFF;
    }
    return s->regs[reg];
}

static void max5978_write(MAX5978State *s, uint8_t reg, uint8_t val)
{
    if (reg >= MAX5978_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "MAX5978: Write out-of-bounds: 0x%02x\n", reg);
        return;
    }

    // Only allow writing to control, threshold, and reset registers
    if (reg >= 0x1A && reg <= 0x43) {
        s->regs[reg] = val;
    } else {
        qemu_log_mask(LOG_UNIMP, "MAX5978: Ignored write to read-only reg 0x%02x\n", reg);
    }
}

static uint8_t max5978_rx(I2CSlave *i2c)
{
    MAX5978State *s = MAX5978(i2c);
    if (s->len == 1) {
        return max5978_read(s, s->pointer++);
    }
    return 0xFF;
}

static int max5978_tx(I2CSlave *i2c, uint8_t data)
{
    MAX5978State *s = MAX5978(i2c);
    if (s->len == 0) {
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        max5978_write(s, s->pointer++, data);
    }
    return 0;
}

static int max5978_event(I2CSlave *i2c, enum i2c_event event)
{
    MAX5978State *s = MAX5978(i2c);
    switch (event) {
    case I2C_START_SEND:
        s->pointer = 0xFF;
        s->len = 0;
        break;
    case I2C_START_RECV:
        if (s->len != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "MAX5978: Invalid read sequence\n");
        }
        break;
    default:
        break;
    }
    return 0;
}

static const VMStateDescription vmstate_max5978 = {
    .name = "MAX5978",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, MAX5978State),
        VMSTATE_UINT8_ARRAY(regs, MAX5978State, MAX5978_NUM_REGS),
        VMSTATE_UINT8(pointer, MAX5978State),
        VMSTATE_I2C_SLAVE(i2c, MAX5978State),
        VMSTATE_END_OF_LIST()
    }
};

static void max5978_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->reset = max5978_reset;
    dc->vmsd = &vmstate_max5978;
    k->event = max5978_event;
    k->recv = max5978_rx;
    k->send = max5978_tx;
}


static const TypeInfo max5978_info = {
    .name          = TYPE_MAX5978,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MAX5978State),
    .class_init    = max5978_class_init,
};

static void max5978_register_types(void)
{
    type_register_static(&max5978_info);
}

type_init(max5978_register_types);
