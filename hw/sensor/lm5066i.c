/*
 * QEMU LM5066I PMBus Hot Swap and Power Monitor Emulation
 *
 * Emulates TI LM5066I functionality: Hot Swap, Power Monitoring, Fault Logging
 *
 * Author: ChatGPT (OpenAI)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/pmbus_device.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/log.h"

#define TYPE_LM5066I "lm5066i"
#define LM5066I(obj) OBJECT_CHECK(LM5066IState, (obj), TYPE_LM5066I)

#define LM5066I_NUM_PAGES 1

#define LM5066I_MFR_ID          0x99
#define LM5066I_MFR_MODEL       0x9A
#define LM5066I_MFR_REVISION    0x9B
#define LM5066I_READ_EIN        0x86
#define LM5066I_READ_VIN        0x88
#define LM5066I_READ_IIN        0x89
#define LM5066I_READ_PIN        0x96
#define LM5066I_STATUS_INPUT    0x7C
#define LM5066I_BLACK_BOX_1     0xD0

// Defaults inspired by MAX34462 but adapted for LM5066I
#define DEFAULT_OP_ON                   0x80
#define DEFAULT_ON_OFF_CONFIG           0x1A
#define DEFAULT_VOUT_MODE               0x40
#define DEFAULT_TEMPERATURE             2500
#define DEFAULT_SCALE                   0x7FFF
#define DEFAULT_OV_LIMIT                14000  // 14V
#define DEFAULT_UV_LIMIT                10000  // 10V
#define DEFAULT_OC_LIMIT                3000   // 3A
#define DEFAULT_OT_LIMIT                8500   // 85C
#define DEFAULT_TEMPERATURE_WARN        8000   // 80C
#define DEFAULT_VIN                     12000  // mV
#define DEFAULT_IIN                     1500   // mA
#define DEFAULT_PIN                     18000  // mW
#define DEFAULT_EIN                     100000 // uWh
#define DEFAULT_STATUS_INPUT            0x00
#define DEFAULT_TEXT                    0x3130313031303130

typedef struct LM5066IState {
    PMBusDevice parent;

    uint16_t vin;
    uint16_t iin;
    uint16_t pin;
    uint32_t ein;  // Energy input accumulation
    uint8_t status_input;

    uint8_t blackbox[8];
} LM5066IState;

static uint8_t lm5066i_receive_byte(PMBusDevice *dev)
{
    LM5066IState *s = LM5066I(dev);

    switch (dev->code) {
    case LM5066I_READ_VIN:
        pmbus_send16(dev, s->vin);
        break;
    case LM5066I_READ_IIN:
        pmbus_send16(dev, s->iin);
        break;
    case LM5066I_READ_PIN:
        pmbus_send16(dev, s->pin);
        break;
    case LM5066I_READ_EIN:
        pmbus_send32(dev, s->ein);
        break;
    case LM5066I_STATUS_INPUT:
        pmbus_send8(dev, s->status_input);
        break;
    case LM5066I_MFR_ID:
        pmbus_send_string(dev, "TI");
        break;
    case LM5066I_MFR_MODEL:
        pmbus_send_string(dev, "LM5066I");
        break;
    case LM5066I_MFR_REVISION:
        pmbus_send_string(dev, "A");
        break;
    case LM5066I_BLACK_BOX_1:
        pmbus_send(dev, s->blackbox, sizeof(s->blackbox));
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "LM5066I: unsupported read 0x%02x\n", dev->code);
        break;
    }

    return 0xFF;
}

static int lm5066i_write_data(PMBusDevice *dev, const uint8_t *buf, uint8_t len)
{
    qemu_log_mask(LOG_GUEST_ERROR, "LM5066I: write attempt to code 0x%02x\n", buf[0]);
    dev->pages[0].status_cml |= PB_CML_FAULT_INVALID_CMD;
    return 0;
}

static void lm5066i_reset(Object *obj)
{
    LM5066IState *s = LM5066I(obj);
    PMBusDevice *dev = PMBUS_DEVICE(obj);

    dev->capability = 0x20;

    pmbus_page_config(dev, 0, dev->capability);

    PMBusPage *page = &dev->pages[0];
    page->operation = DEFAULT_OP_ON;
    page->on_off_config = DEFAULT_ON_OFF_CONFIG;
    page->vout_mode = DEFAULT_VOUT_MODE;
    page->revision = 0x11;

    page->read_vin  = s->vin  = DEFAULT_VIN;
    page->read_iin  = s->iin  = DEFAULT_IIN;
    page->read_pin  = s->pin  = DEFAULT_PIN;
    s->ein = DEFAULT_EIN;

    page->status_input = s->status_input = DEFAULT_STATUS_INPUT;
    page->status_word = 0x0000;
    page->status_cml = 0x00;
    page->status_mfr_specific = 0x00;

    page->vin_ov_fault_limit = DEFAULT_OV_LIMIT;
    page->vin_uv_fault_limit = DEFAULT_UV_LIMIT;
    page->iin_oc_fault_limit = DEFAULT_OC_LIMIT;
    page->ot_fault_limit = DEFAULT_OT_LIMIT;
    page->ot_warn_limit  = DEFAULT_TEMPERATURE_WARN;

    page->mfr_id     = "TI";
    page->mfr_model  = "LM5066I";
    page->mfr_revision = "A";

    memset(s->blackbox, 0xAB, sizeof(s->blackbox));
}

static void lm5066i_class_init(ObjectClass *klass, void *data)
{
    PMBusDeviceClass *k = PMBUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "TI LM5066I Hot Swap and Power Monitor";
    k->receive_byte = lm5066i_receive_byte;
    k->write_data = lm5066i_write_data;
    k->device_num_pages = LM5066I_NUM_PAGES;
    rc->phases.exit = lm5066i_reset;
}

static const TypeInfo lm5066i_info = {
    .name = TYPE_LM5066I,
    .parent = TYPE_PMBUS_DEVICE,
    .instance_size = sizeof(LM5066IState),
    .class_init = lm5066i_class_init,
    .instance_init = lm5066i_reset,
};

static void lm5066i_register_types(void)
{
    type_register_static(&lm5066i_info);
}

type_init(lm5066i_register_types)