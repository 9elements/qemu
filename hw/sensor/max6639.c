// SPDX-License-Identifier: GPL-2.0-or-later

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"

#define TYPE_MAX6639 "max6639"
#define MAX6639(obj) OBJECT_CHECK(MAX6639State, (obj), TYPE_MAX6639)

#define MAX6639_NUM_REGS  0x14  // 0x00 to 0x13

typedef struct {
    I2CSlave i2c;
    uint8_t regs[MAX6639_NUM_REGS];
    uint8_t pointer;
    uint8_t len;
} MAX6639State;

static void max6639_reset(DeviceState *dev)
{
    MAX6639State *s = MAX6639(dev);
    memset(s->regs, 0, sizeof(s->regs));

    s->regs[0x00] = 30;      // Local Temp MSB = 30°C
    s->regs[0x01] = 42;      // Remote Temp MSB = 42°C
    s->regs[0x10] = 0;       // Local Temp LSB
    s->regs[0x11] = 0;       // Remote Temp LSB
    s->regs[0x02] = 0x00;    // Status clear
    s->regs[0x03] = 0x00;    // Config
    s->pointer = 0;
    s->len = 0;
}

static uint8_t max6639_read(MAX6639State *s, uint8_t reg)
{
    if (reg >= MAX6639_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "MAX6639: Read out-of-bounds: 0x%02x\n", reg);
        return 0xFF;
    }
    return s->regs[reg];
}

static void max6639_write(MAX6639State *s, uint8_t reg, uint8_t val)
{
    if (reg >= MAX6639_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "MAX6639: Write out-of-bounds: 0x%02x\n", reg);
        return;
    }

    // Writable register range from datasheet
    switch (reg) {
        case 0x03: case 0x04: case 0x05: case 0x06:
        case 0x07: case 0x08: case 0x09: case 0x0F:
        case 0x12:
            s->regs[reg] = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "MAX6639: Unhandled write to reg 0x%02x\n", reg);
            break;
    }
}

static uint8_t max6639_rx(I2CSlave *i2c)
{
    MAX6639State *s = MAX6639(i2c);
    if (s->len == 1) {
        return max6639_read(s, s->pointer++);
    }
    return 0xFF;
}

static int max6639_tx(I2CSlave *i2c, uint8_t data)
{
    MAX6639State *s = MAX6639(i2c);
    if (s->len == 0) {
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        max6639_write(s, s->pointer++, data);
    }
    return 0;
}

static int max6639_event(I2CSlave *i2c, enum i2c_event event)
{
    MAX6639State *s = MAX6639(i2c);
    switch (event) {
        case I2C_START_SEND:
            s->pointer = 0xFF;
            s->len = 0;
            break;
        case I2C_START_RECV:
            if (s->len != 1) {
                qemu_log_mask(LOG_GUEST_ERROR, "MAX6639: Invalid read sequence\n");
            }
            break;
        default:
            break;
    }
    return 0;
}

static const VMStateDescription vmstate_max6639 = {
    .name = "MAX6639",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, MAX6639State),
        VMSTATE_UINT8_ARRAY(regs, MAX6639State, MAX6639_NUM_REGS),
        VMSTATE_UINT8(pointer, MAX6639State),
        VMSTATE_I2C_SLAVE(i2c, MAX6639State),
        VMSTATE_END_OF_LIST()
    }
};

static void max6639_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->reset = max6639_reset;
    dc->vmsd = &vmstate_max6639;
    k->event = max6639_event;
    k->recv = max6639_rx;
    k->send = max6639_tx;
}

static const TypeInfo max6639_info = {
    .name          = TYPE_MAX6639,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MAX6639State),
    .class_init    = max6639_class_init,
};

static void max6639_register_types(void)
{
    type_register_static(&max6639_info);
}

type_init(max6639_register_types);
