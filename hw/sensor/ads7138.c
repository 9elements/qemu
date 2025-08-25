#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/sensor/ads7138.h"

static void ads7138_reset(DeviceState *dev)
{
    ADS7138State *s = ADS7138(dev);
    memset(s->regs, 0, sizeof(s->regs));

    // Datasheet-defined reset values
    s->regs[0x00] = 0x81;  // SYSTEM_STATUS
    s->regs[0x20] = 0xF0;  // HYSTERESIS_CH0
    s->regs[0x21] = 0xFF;  // HIGH_TH_CH0
    s->regs[0x24] = 0xF0;
    s->regs[0x25] = 0xFF;
    s->regs[0x28] = 0xF0;
    s->regs[0x29] = 0xFF;
    s->regs[0x2C] = 0xF0;
    s->regs[0x2D] = 0xFF;
    s->regs[0x30] = 0xF0;
    s->regs[0x31] = 0xFF;
    s->regs[0x34] = 0xF0;
    s->regs[0x35] = 0xFF;
    s->regs[0x38] = 0xF0;
    s->regs[0x39] = 0xFF;
    s->regs[0x3C] = 0xF0;
    s->regs[0x3D] = 0xFF;

    for (int i = 0x80; i <= 0x8F; ++i) s->regs[i] = 0xFF;

    // GPOx Trigger defaults
    for (int i = 0xC3; i <= 0xD1; i += 2) s->regs[i] = 0x02;

    s->pointer = 0;
    s->len = 0;
}

static uint8_t ads7138_read(ADS7138State *s, uint8_t reg)
{
    if (reg >= ADS7138_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "ADS7138: Read out-of-bounds: 0x%02x\n", reg);
        return 0xFF;
    }
    return s->regs[reg];
}

static void ads7138_write(ADS7138State *s, uint8_t reg, uint8_t val)
{
    if (reg >= ADS7138_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "ADS7138: Write out-of-bounds: 0x%02x\n", reg);
        return;
    }

    s->regs[reg] = val;  // All writable for now. You can restrict if needed.
}

static uint8_t ads7138_rx(I2CSlave *i2c)
{
    ADS7138State *s = ADS7138(i2c);
    if (s->len == 1) {
        return ads7138_read(s, s->pointer++);
    }
    return 0xFF;
}

static int ads7138_tx(I2CSlave *i2c, uint8_t data)
{
    ADS7138State *s = ADS7138(i2c);
    if (s->len == 0) {
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        ads7138_write(s, s->pointer++, data);
    }
    return 0;
}

static int ads7138_event(I2CSlave *i2c, enum i2c_event event)
{
    ADS7138State *s = ADS7138(i2c);
    switch (event) {
        case I2C_START_SEND:
            s->pointer = 0xFF;
            s->len = 0;
            break;
        case I2C_START_RECV:
            if (s->len != 1) {
                qemu_log_mask(LOG_GUEST_ERROR, "ADS7138: Invalid read sequence\n");
            }
            break;
        default:
            break;
    }
    return 0;
}

static const VMStateDescription vmstate_ads7138 = {
    .name = "ADS7138",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, ADS7138State),
        VMSTATE_UINT8(pointer, ADS7138State),
        VMSTATE_UINT8_ARRAY(regs, ADS7138State, ADS7138_NUM_REGS),
        VMSTATE_I2C_SLAVE(i2c, ADS7138State),
        VMSTATE_END_OF_LIST()
    }
};

static void ads7138_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->reset = ads7138_reset;
    dc->vmsd = &vmstate_ads7138;
    k->recv = ads7138_rx;
    k->send = ads7138_tx;
    k->event = ads7138_event;
}

static const TypeInfo ads7138_info = {
    .name          = TYPE_ADS7138,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(ADS7138State),
    .class_init    = ads7138_class_init,
};

static void ads7138_register_types(void)
{
    type_register_static(&ads7138_info);
}

type_init(ads7138_register_types);
