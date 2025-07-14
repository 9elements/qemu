// SPDX-License-Identifier: GPL-2.0-or-later

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"

#define TYPE_MAX5978 "max5978"
#define MAX5978(obj) OBJECT_CHECK(MAX5978State, (obj), TYPE_MAX5978)

#define MAX5978_NUM_REGS 0xff

typedef struct {
    I2CSlave i2c;
    uint8_t regs[MAX5978_NUM_REGS];
    uint8_t pointer;
    uint8_t len;
} MAX5978State;

static void max5978_reset(DeviceState *dev)
{
    MAX5978State *s = MAX5978(dev);
    memset(s->regs, 0, sizeof(s->regs));  // Default all to 0

    // --- ADC Measurement Results ---
    s->regs[0x00] = 0x00;  // adc_cs_msb
    s->regs[0x01] = 0x00;  // adc_cs_lsb
    s->regs[0x02] = 0x00;  // adc_mon_msb
    s->regs[0x03] = 0x00;  // adc_mon_lsb

    // --- Reserved (0x04–0x07): Unknown Purpose (datasheet silent) ---
    s->regs[0x04] = 0x00;  // TODO
    s->regs[0x05] = 0x00;  // TODO
    s->regs[0x06] = 0x00;  // TODO
    s->regs[0x07] = 0x00;  // TODO

    // --- Current Min/Max Registers ---
    s->regs[0x08] = 0xFF;  // min_cs_msb
    s->regs[0x09] = 0x03;  // min_cs_lsb
    s->regs[0x0A] = 0x00;  // max_cs_msb
    s->regs[0x0B] = 0x00;  // max_cs_lsb

    // --- Voltage Min/Max Registers ---
    s->regs[0x0C] = 0xFF;  // min_mon_msb
    s->regs[0x0D] = 0x03;  // min_mon_lsb
    s->regs[0x0E] = 0x00;  // max_mon_msb
    s->regs[0x0F] = 0x00;  // max_mon_lsb

    // --- Alarm, Control, Range ---
    s->regs[0x10] = 0x00;  // TODO
    s->regs[0x11] = 0x00;  // TODO
    s->regs[0x12] = 0x00;  // TODO
    s->regs[0x13] = 0x00;  // TODO
    s->regs[0x14] = 0x00;  // TODO
    s->regs[0x15] = 0x00;  // TODO
    s->regs[0x16] = 0x00;  // TODO
    s->regs[0x17] = 0x00;  // TODO

    s->regs[0x18] = 0x00;  // mon_range
    s->regs[0x19] = 0x0F;  // cbuf_chx_store

    // --- UV/OV/OI Thresholds ---
    s->regs[0x1A] = 0x00;  // uv1th_msb
    s->regs[0x1B] = 0x00;  // uv1th_lsb
    s->regs[0x1C] = 0x00;  // uv2th_msb
    s->regs[0x1D] = 0x00;  // uv2th_lsb
    s->regs[0x1E] = 0xFF;  // ov1thr_msb
    s->regs[0x1F] = 0x03;  // ov1thr_lsb
    s->regs[0x20] = 0xFF;  // ov2thr_msb
    s->regs[0x21] = 0x03;  // ov2thr_lsb
    s->regs[0x22] = 0xFF;  // oithr_msb
    s->regs[0x23] = 0x03;  // oithr_lsb

    // --- Reserved / Status / Control ---
    for (int i = 0x24; i <= 0x2D; i++) {
        s->regs[i] = 0x00;  // TODO
    }

    // --- Fast Comparator DAC & Ratios ---
    s->regs[0x2E] = 0xBF;  // dac_fast

    s->regs[0x2F] = 0x00;  // TODO (undocumented)

    s->regs[0x30] = 0x0F;  // ifast2slow
    s->regs[0x31] = 0x00;  // status0
    s->regs[0x32] = 0x00;  // status1 – TODO: status inputs (read-only?)
    s->regs[0x33] = 0x03;  // status2
    s->regs[0x34] = 0x01;  // status3

    // --- Fault Registers (read/clear on read) ---
    s->regs[0x35] = 0x00;  // fault0: undervoltage
    s->regs[0x36] = 0x00;  // fault1: overvoltage
    s->regs[0x37] = 0x00;  // fault2: overcurrent

    // --- PG Delay, Force-On, Channel Control ---
    s->regs[0x38] = 0x00;  // pgdly
    s->regs[0x39] = 0x00;  // fokey
    s->regs[0x3A] = 0x00;  // foset
    s->regs[0x3B] = 0x00;  // chxen

    // --- Deglitching ---
    s->regs[0x3C] = 0x00;  // dgl_i
    s->regs[0x3D] = 0x00;  // dgl_uv
    s->regs[0x3E] = 0x00;  // dgl_ov

    // --- Circular Buffer Config ---
    s->regs[0x3F] = 0x0F;  // cbufrd_hibyonly
    s->regs[0x40] = 0x19;  // cbuf_dly_stop

    // --- Peak Hold/Reset ---
    s->regs[0x41] = 0x00;  // peak_log_rst
    s->regs[0x42] = 0x00;  // peak_log_hold

    // --- LED Behavior ---
    s->regs[0x43] = 0x0F;  // LED_flash
    s->regs[0x44] = 0x00;  // LED_ph_pu
    s->regs[0x45] = 0x00;  // LED_state – TODO: Read-only state?

    // --- Circular Buffer Base Addresses (Read-only) ---
    s->regs[0x46] = 0x00;  // cbuf_ba_v – voltage buffer address (read-only)
    s->regs[0x47] = 0x00;  // cbuf_ba_i – current buffer address (read-only)

    // --- ADC Measurement Results ---
    s->regs[0x00] = 0x1A;  // adc_cs_msb → Simulated ADC current value (e.g. 0x1A3 = 419)
    s->regs[0x01] = 0x03;  // adc_cs_lsb

    s->regs[0x02] = 0x2F;  // adc_mon_msb → Simulated ADC voltage value (e.g. 0x2F0 = 752)
    s->regs[0x03] = 0x00;  // adc_mon_lsb

    // --- Min/Max current values ---
    s->regs[0x08] = 0x1A;  // min_cs_msb → simulate min current = current value
    s->regs[0x09] = 0x03;  // min_cs_lsb
    s->regs[0x0A] = 0x1A;  // max_cs_msb → simulate max current = current value
    s->regs[0x0B] = 0x03;  // max_cs_lsb

    // --- Min/Max voltage values ---
    s->regs[0x0C] = 0x2F;  // min_mon_msb
    s->regs[0x0D] = 0x00;  // min_mon_lsb
    s->regs[0x0E] = 0x2F;  // max_mon_msb
    s->regs[0x0F] = 0x00;  // max_mon_lsb

    // --- Status & Fault Registers (Read-only / Read-Clear) ---
    s->regs[0x31] = 0x05;  // status1: MODE and ON high
    s->regs[0x33] = 0x08;  // status2: IRNG = mid-scale
    s->regs[0x34] = 0x10;  // status3: ALERT = asserted

    // --- Fault Statuses ---
    s->regs[0x35] = 0x00;  // fault0 (UV) → No UV fault
    s->regs[0x36] = 0x00;  // fault1 (OV) → No OV fault
    s->regs[0x37] = 0x00;  // fault2 (OI) → No OC fault

    // --- LED State & Circular Buffer Read Base ---
    s->regs[0x45] = 0x00;  // LED_state → All open
    s->regs[0x46] = 0x10;  // cbuf_ba_v → Assume voltage buffer starts at 0x10
    s->regs[0x47] = 0x30;  // cbuf_ba_i → Assume current buffer starts at 0x30


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
