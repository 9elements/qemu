// SPDX-License-Identifier: GPL-2.0-or-later

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/sensor/max8952.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"

static void max8952_reset(DeviceState *dev)
{
    MAX8952State *s = MAX8952(dev);
    static const uint8_t default_regs[MAX8952_NUM_REGS] = {
        [0x00] = 0x3F, // MODE0
        [0x01] = 0x17, // MODE1
        [0x02] = 0x3F, // MODE2
        [0x03] = 0x21, // MODE3
        [0x04] = 0xE0, // CONTROL
        [0x05] = 0x00, // SYNC
        [0x06] = 0x01, // RAMP
        [0x08] = 0x20, // CHIP_ID1
        [0x09] = 0x1A, // CHIP_ID2
    };
    memcpy(s->regs, default_regs, sizeof(default_regs));
    s->pointer = 0;
    s->len = 0;
}

static uint8_t max8952_read(MAX8952State *s, uint8_t reg)
{
    if (reg >= MAX8952_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "MAX8952: Read out-of-bounds: 0x%02x\n", reg);
        return 0xFF;
    }
    return s->regs[reg];
}

static void max8952_write(MAX8952State *s, uint8_t reg, uint8_t val)
{
    if (reg >= MAX8952_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "MAX8952: Write out-of-bounds: 0x%02x\n", reg);
        return;
    }

    switch (reg) {
    case 0x00 ... 0x06:
        s->regs[reg] = val;
        break;
    case 0x08: case 0x09:
        qemu_log_mask(LOG_GUEST_ERROR, "MAX8952: Attempt to write read-only reg 0x%02x\n", reg);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "MAX8952: Unhandled write to reg 0x%02x\n", reg);
        break;
    }
}

static uint8_t max8952_rx(I2CSlave *i2c)
{
    MAX8952State *s = MAX8952(i2c);
    if (s->len == 1) {
        return max8952_read(s, s->pointer++);
    }
    return 0xFF;
}

static int max8952_tx(I2CSlave *i2c, uint8_t data)
{
    MAX8952State *s = MAX8952(i2c);
    if (s->len == 0) {
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        max8952_write(s, s->pointer++, data);
    }
    return 0;
}

static int max8952_event(I2CSlave *i2c, enum i2c_event event)
{
    MAX8952State *s = MAX8952(i2c);
    switch (event) {
    case I2C_START_SEND:
        s->pointer = 0xFF;
        s->len = 0;
        break;
    case I2C_START_RECV:
        if (s->len != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "MAX8952: Invalid read sequence\n");
        }
        break;
    default:
        break;
    }
    return 0;
}

static const VMStateDescription vmstate_max8952 = {
    .name = "MAX8952",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, MAX8952State),
        VMSTATE_UINT8_ARRAY(regs, MAX8952State, MAX8952_NUM_REGS),
        VMSTATE_UINT8(pointer, MAX8952State),
        VMSTATE_I2C_SLAVE(i2c, MAX8952State),
        VMSTATE_END_OF_LIST()
    }
};

static void max8952_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->reset = max8952_reset;
    dc->vmsd = &vmstate_max8952;
    k->event = max8952_event;
    k->recv = max8952_rx;
    k->send = max8952_tx;
}

static const TypeInfo max8952_info = {
    .name          = TYPE_MAX8952,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MAX8952State),
    .class_init    = max8952_class_init,
};

static void max8952_register_types(void)
{
    type_register_static(&max8952_info);
}

type_init(max8952_register_types);
