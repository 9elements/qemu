// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BME280 temperature, pressure, and humidity sensor
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/i2c/i2c.h"
#include "hw/sensor/bme280.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "migration/vmstate.h"

static void bme280_reset(DeviceState *dev)
{
    BME280State *s = BME280(dev);
static const uint8_t default_regs[NUM_REGISTERS] = {
        // Device ID and basic registers
        [BME280_REG_ID]         = 0x60,
        [BME280_REG_STATUS]     = 0x00,
        [BME280_REG_CTRL_HUM]   = 0x00,
        [BME280_REG_CTRL_MEAS]  = 0x27,
        [BME280_REG_CONFIG]     = 0x00,

        // Data registers
        [BME280_REG_PRESS_MSB]      = 0x80,
        [BME280_REG_PRESS_MSB + 1]  = 0x00,
        [BME280_REG_PRESS_MSB + 2]  = 0x00,

        [BME280_REG_TEMP_MSB]       = 0x64,
        [BME280_REG_TEMP_MSB + 1]   = 0x00,
        [BME280_REG_TEMP_MSB + 2]   = 0x00,

        [BME280_REG_HUM_MSB]        = 0x33,
        [BME280_REG_HUM_MSB + 1]    = 0x33,

};

    memcpy(s->regs, default_regs, sizeof(s->regs));
    s->pointer = 0;
}

// simple bounded register read
static uint8_t bme280_read(BME280State *s, uint8_t reg)
{
    if (reg >= NUM_REGISTERS) {
        qemu_log_mask(LOG_GUEST_ERROR, "BME280: read reg 0x%02x out of bounds\n", reg);
        return 0xFF;
    }
    switch (reg) {
    case BME280_REG_ID:
    case BME280_REG_STATUS:
    case BME280_REG_CTRL_HUM:
    case BME280_REG_CTRL_MEAS:
    case BME280_REG_CONFIG:
    case BME280_REG_PRESS_MSB ... BME280_REG_HUM_MSB + 2:
    case BME280_REG_CALIB00 ... BME280_REG_CALIB00 + 24:
        return s->regs[reg];
    default:
        qemu_log_mask(LOG_UNIMP, "BME280: read reg 0x%02x unimplemented\n", reg);
        return 0xFF;
    }
}

static void bme280_write(BME280State *s, uint8_t reg, uint8_t val)
{
    if (reg >= NUM_REGISTERS) {
        qemu_log_mask(LOG_GUEST_ERROR, "BME280: write reg 0x%02x out of bounds\n", reg);
        return;
    }
    switch (reg) {
    case BME280_REG_RESET:
        if (val == 0xB6) {
            device_cold_reset(DEVICE(s));
        }
        break;
    case BME280_REG_CTRL_HUM:
    case BME280_REG_CTRL_MEAS:
    case BME280_REG_CONFIG:
        s->regs[reg] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "BME280: write reg 0x%02x unimplemented\n", reg);
        break;
    }
}

static uint8_t bme280_rx(I2CSlave *i2c)
{
    BME280State *s = BME280(i2c);
    if (s->len == 1) {
        return bme280_read(s, s->pointer++);
    }
    return 0xFF;
}

static int bme280_tx(I2CSlave *i2c, uint8_t data)
{
    BME280State *s = BME280(i2c);
    if (s->len == 0) {
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        bme280_write(s, s->pointer++, data);
    }
    return 0;
}

static int bme280_event(I2CSlave *i2c, enum i2c_event event)
{
    BME280State *s = BME280(i2c);
    switch (event) {
    case I2C_START_SEND:
        s->pointer = 0xFF;
        s->len = 0;
        break;
    case I2C_START_RECV:
        if (s->len != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "BME280: invalid recv sequence\n");
        }
        break;
    default:
        break;
    }
    return 0;
}

static const VMStateDescription vmstate_bme280 = {
    .name = "BME280",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, BME280State),
        VMSTATE_UINT8_ARRAY(regs, BME280State, NUM_REGISTERS),
        VMSTATE_UINT8(pointer, BME280State),
        VMSTATE_I2C_SLAVE(i2c, BME280State),
        VMSTATE_END_OF_LIST()
    }
};

static void bme280_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    k->event = bme280_event;
    k->recv  = bme280_rx;
    k->send  = bme280_tx;
    dc->reset = bme280_reset;
    dc->vmsd  = &vmstate_bme280;
}

static const TypeInfo bme280_info = {
    .name          = TYPE_BME280,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(BME280State),
    .class_init    = bme280_class_init,
};

static void bme280_register_types(void)
{
    type_register_static(&bme280_info);
}

type_init(bme280_register_types);
