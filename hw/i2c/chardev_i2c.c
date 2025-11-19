/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SPDX-FileCopyrightText: Copyright (c) 2025 9elements GmbH
 * SPDX-FileContributor: Christian Grönke <christian.groenke@9elements.com>
 */

/*
 * I2C device that sends/receives to/from a chardev
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "block/aio.h"
#include "chardev/char-fe.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/i2c/i2c.h"

#define TYPE_CHARDEV_I2C_DEVICE "chardev-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(ChardevI2CDevice, CHARDEV_I2C_DEVICE)

enum chardev_i2c_state {
    CHARDEV_I2C_STATE_IDLE,
    CHARDEV_I2C_STATE_START_SEND,
    CHARDEV_I2C_STATE_SEND_BYTE,
};

typedef struct ChardevI2CDevice {
    I2CSlave parent_obj;
    I2CBus *bus;

    /* Properties */
    CharBackend chardev;

    enum chardev_i2c_state state;
    QEMUBH *bh;
} ChardevI2CDevice;

static void chardev_i2c_bh(void *opaque)
{
    ChardevI2CDevice *state = opaque;

    switch (state->state) {
    case CHARDEV_I2C_STATE_IDLE:
        /* dummy/example code */
        return;

    case CHARDEV_I2C_STATE_START_SEND:
        /* dummy/example code */
        if (i2c_start_send_async(state->bus, 0x00)) {
            goto release_bus;
        }

        state->state = CHARDEV_I2C_STATE_SEND_BYTE;

        return;

    case CHARDEV_I2C_STATE_SEND_BYTE:
        if (i2c_send_async(state->bus, 0x00)) {
            break;
        }

        return;
    }

    i2c_end_transfer(state->bus);
release_bus:
    i2c_bus_release(state->bus);

    state->state = CHARDEV_I2C_STATE_IDLE;
}

static int chardev_i2c_event(I2CSlave *s, enum i2c_event event)
{
    ChardevI2CDevice *state = CHARDEV_I2C_DEVICE(s);

    switch (event) {
    case I2C_START_RECV:
        break;

    case I2C_START_SEND_ASYNC:
    case I2C_START_SEND:
        break;

    case I2C_FINISH:
        state->state = CHARDEV_I2C_STATE_START_SEND;
        i2c_bus_master(state->bus, state->bh);

        break;

    case I2C_NACK:
        break;

    default:
        return -1;
    }

    return 0;
}

static int chardev_i2c_send(I2CSlave *s, uint8_t data)
{
    //ChardevI2CDevice *state = CHARDEV_I2C_DEVICE(s);

    return 0;
}

static void chardev_i2c_realize(DeviceState *dev, Error **errp)
{
    ChardevI2CDevice *state = CHARDEV_I2C_DEVICE(dev);
    BusState *bus = qdev_get_parent_bus(dev);

    state->bus = I2C_BUS(bus);
    state->bh = qemu_bh_new(chardev_i2c_bh, state);
}

static Property chardev_i2c_properties[] = {
    DEFINE_PROP_CHR("chardev", ChardevI2CDevice, chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static void chardev_i2c_class_init(ObjectClass *oc, void *data)
{
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = chardev_i2c_realize;

    sc->event = chardev_i2c_event;
    sc->send = chardev_i2c_send;

    device_class_set_props(dc, chardev_i2c_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo i2c_echo = {
    .name = TYPE_CHARDEV_I2C_DEVICE,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(ChardevI2CDevice),
    .class_init = chardev_i2c_class_init,
};

static void register_types(void)
{
    type_register_static(&i2c_echo);
}

type_init(register_types);
