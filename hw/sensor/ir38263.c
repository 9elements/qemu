/*
 * Infineon IR38263 PMBus Buck Regulator Emulator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/pmbus_device.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TYPE_IR38263 "ir38263"
#define IR38263(obj) OBJECT_CHECK(IR38263State, (obj), TYPE_IR38263)

typedef struct IR38263State {
    PMBusDevice parent;

    uint8_t operation;
    uint8_t on_off_config;
    uint8_t write_protect;
    uint8_t vout_mode;
    uint16_t vout_command;
    uint16_t vout_max;
    uint16_t vout_margin_high;
    uint16_t vout_margin_low;
    uint16_t vout_transition_rate;
    uint16_t vout_ov_fault_limit;
    uint16_t ot_fault_limit;
    uint16_t ot_warn_limit;
    uint16_t vin_ov_warn_limit;
    uint16_t vin_uv_warn_limit;
    uint16_t iin_oc_fault_limit;
    uint16_t ton_delay;
    uint16_t ton_rise;
    uint16_t toff_fall;
    uint8_t revision;

    // Telemetry (sensors)
    uint16_t read_vout;
    uint16_t read_iout;
    uint16_t read_pout;
    uint16_t read_vin;
    uint16_t read_iin;
    uint16_t read_pin;
    uint16_t read_temperature_1;
    uint16_t read_temperature_2;
    uint16_t read_temperature_3;
} IR38263State;

static uint8_t ir38263_receive_byte(PMBusDevice *pmdev)
{
    IR38263State *s = IR38263(pmdev);

    switch (pmdev->code) {
        case PMBUS_OPERATION:
            pmbus_send8(pmdev, s->operation); break;
        case PMBUS_ON_OFF_CONFIG:
            pmbus_send8(pmdev, s->on_off_config); break;
        case PMBUS_WRITE_PROTECT:
            pmbus_send8(pmdev, s->write_protect); break;
        case PMBUS_VOUT_MODE:
            pmbus_send8(pmdev, s->vout_mode); break;
        case PMBUS_VOUT_COMMAND:
            pmbus_send16(pmdev, s->vout_command); break;
        case PMBUS_VOUT_MAX:
            pmbus_send16(pmdev, s->vout_max); break;
        case PMBUS_VOUT_MARGIN_HIGH:
            pmbus_send16(pmdev, s->vout_margin_high); break;
        case PMBUS_VOUT_MARGIN_LOW:
            pmbus_send16(pmdev, s->vout_margin_low); break;
        case PMBUS_VOUT_OV_FAULT_LIMIT:
            pmbus_send16(pmdev, s->vout_ov_fault_limit); break;
        case PMBUS_TON_DELAY:
            pmbus_send16(pmdev, s->ton_delay); break;
        case PMBUS_TON_RISE:
            pmbus_send16(pmdev, s->ton_rise); break;
        case PMBUS_TOFF_FALL:
            pmbus_send16(pmdev, s->toff_fall); break;
        case PMBUS_OT_FAULT_LIMIT:
            pmbus_send16(pmdev, s->ot_fault_limit); break;
        case PMBUS_OT_WARN_LIMIT:
            pmbus_send16(pmdev, s->ot_warn_limit); break;
        case PMBUS_VIN_OV_WARN_LIMIT:
            pmbus_send16(pmdev, s->vin_ov_warn_limit); break;
        case PMBUS_VIN_UV_WARN_LIMIT:
            pmbus_send16(pmdev, s->vin_uv_warn_limit); break;
        case PMBUS_IIN_OC_FAULT_LIMIT:
            pmbus_send16(pmdev, s->iin_oc_fault_limit); break;
        case PMBUS_REVISION:
            pmbus_send8(pmdev, s->revision); break;

        // Telemetry readings
        case PMBUS_READ_VOUT:
            pmbus_send16(pmdev, s->read_vout); break;
        case PMBUS_READ_IOUT:
            pmbus_send16(pmdev, s->read_iout); break;
        case PMBUS_READ_POUT:
            pmbus_send16(pmdev, s->read_pout); break;
        case PMBUS_READ_VIN:
            pmbus_send16(pmdev, s->read_vin); break;
        case PMBUS_READ_IIN:
            pmbus_send16(pmdev, s->read_iin); break;
        case PMBUS_READ_PIN:
            pmbus_send16(pmdev, s->read_pin); break;
        case PMBUS_READ_TEMPERATURE_1:
            pmbus_send16(pmdev, s->read_temperature_1); break;
        case PMBUS_READ_TEMPERATURE_2:
            pmbus_send16(pmdev, s->read_temperature_2); break;
        case PMBUS_READ_TEMPERATURE_3:
            pmbus_send16(pmdev, s->read_temperature_3); break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported read 0x%02x\n",
                          __func__, pmdev->code);
            break;
    }

    return PMBUS_ERR_BYTE;
}

static int ir38263_write_data(PMBusDevice *pmdev, const uint8_t *buf, uint8_t len)
{
    IR38263State *s = IR38263(pmdev);
    if (len < 2) return -1;

    pmdev->code = buf[0];
    uint16_t val = buf[1] | (len > 2 ? buf[2] << 8 : 0);

    switch (pmdev->code) {
        case PMBUS_OPERATION:
            s->operation = buf[1]; break;
        case PMBUS_ON_OFF_CONFIG:
            s->on_off_config = buf[1]; break;
        case PMBUS_WRITE_PROTECT:
            s->write_protect = buf[1]; break;
        case PMBUS_VOUT_COMMAND:
            s->vout_command = val; break;
        case PMBUS_VOUT_MARGIN_HIGH:
            s->vout_margin_high = val; break;
        case PMBUS_VOUT_MARGIN_LOW:
            s->vout_margin_low = val; break;
        case PMBUS_VOUT_MAX:
            s->vout_max = val; break;
        case PMBUS_VOUT_TRANSITION_RATE:
            s->vout_transition_rate = val; break;
        case PMBUS_VOUT_OV_FAULT_LIMIT:
            s->vout_ov_fault_limit = val; break;
        case PMBUS_TON_DELAY:
            s->ton_delay = val; break;
        case PMBUS_TON_RISE:
            s->ton_rise = val; break;
        case PMBUS_TOFF_FALL:
            s->toff_fall = val; break;
        case PMBUS_OT_FAULT_LIMIT:
            s->ot_fault_limit = val; break;
        case PMBUS_OT_WARN_LIMIT:
            s->ot_warn_limit = val; break;
        case PMBUS_VIN_OV_WARN_LIMIT:
            s->vin_ov_warn_limit = val; break;
        case PMBUS_VIN_UV_WARN_LIMIT:
            s->vin_uv_warn_limit = val; break;
        case PMBUS_IIN_OC_FAULT_LIMIT:
            s->iin_oc_fault_limit = val; break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write 0x%02x\n",
                          __func__, pmdev->code);
            break;
    }
    return 0;
}


#define ISL_CAPABILITY_DEFAULT                 0x40
#define ISL_OPERATION_DEFAULT                  0x80
#define ISL_ON_OFF_CONFIG_DEFAULT              0x16
#define ISL_VOUT_MODE_DEFAULT                  0x40
#define ISL_VOUT_COMMAND_DEFAULT               0x0384
#define ISL_VOUT_MAX_DEFAULT                   0x08FC
#define ISL_VOUT_MARGIN_HIGH_DEFAULT           0x0640
#define ISL_VOUT_MARGIN_LOW_DEFAULT            0xFA
#define ISL_VOUT_TRANSITION_RATE_DEFAULT       0x64
#define ISL_VOUT_OV_FAULT_LIMIT_DEFAULT        0x076C
#define ISL_OT_FAULT_LIMIT_DEFAULT             0x7D
#define ISL_OT_WARN_LIMIT_DEFAULT              0x07D0
#define ISL_VIN_OV_WARN_LIMIT_DEFAULT          0x36B0
#define ISL_VIN_UV_WARN_LIMIT_DEFAULT          0x1F40
#define ISL_IIN_OC_FAULT_LIMIT_DEFAULT         0x32
#define ISL_TON_DELAY_DEFAULT                  0x14
#define ISL_TON_RISE_DEFAULT                   0x01F4
#define ISL_TOFF_FALL_DEFAULT                  0x01F4
#define ISL_REVISION_DEFAULT                   0x33
#define ISL_READ_VOUT_DEFAULT                  1000
#define ISL_READ_IOUT_DEFAULT                  40
#define ISL_READ_POUT_DEFAULT                  4
#define ISL_READ_TEMP_DEFAULT                  25
#define ISL_READ_VIN_DEFAULT                   1100
#define ISL_READ_IIN_DEFAULT                   40
#define ISL_READ_PIN_DEFAULT                   4

static void ir38263_init(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    IR38263State *s = IR38263(obj);

    pmbus_page_config(pmdev, 0, PB_HAS_VOUT | PB_HAS_IOUT |
                      PB_HAS_TEMPERATURE | PB_HAS_VOUT_MODE |
                      PB_HAS_VIN | PB_HAS_PIN | PB_HAS_IIN);

    // Config defaults
    s->operation = ISL_OPERATION_DEFAULT;
    s->on_off_config = ISL_ON_OFF_CONFIG_DEFAULT;
    s->write_protect = 0x00;
    s->vout_mode = ISL_VOUT_MODE_DEFAULT;
    s->vout_command = ISL_VOUT_COMMAND_DEFAULT;
    s->vout_max = ISL_VOUT_MAX_DEFAULT;
    s->vout_margin_high = ISL_VOUT_MARGIN_HIGH_DEFAULT;
    s->vout_margin_low = ISL_VOUT_MARGIN_LOW_DEFAULT;
    s->vout_transition_rate = ISL_VOUT_TRANSITION_RATE_DEFAULT;
    s->vout_ov_fault_limit = ISL_VOUT_OV_FAULT_LIMIT_DEFAULT;
    s->ot_fault_limit = ISL_OT_FAULT_LIMIT_DEFAULT;
    s->ot_warn_limit = ISL_OT_WARN_LIMIT_DEFAULT;
    s->vin_ov_warn_limit = ISL_VIN_OV_WARN_LIMIT_DEFAULT;
    s->vin_uv_warn_limit = ISL_VIN_UV_WARN_LIMIT_DEFAULT;
    s->iin_oc_fault_limit = ISL_IIN_OC_FAULT_LIMIT_DEFAULT;
    s->ton_delay = ISL_TON_DELAY_DEFAULT;
    s->ton_rise = ISL_TON_RISE_DEFAULT;
    s->toff_fall = ISL_TOFF_FALL_DEFAULT;
    s->revision = ISL_REVISION_DEFAULT;

    // Telemetry
    s->read_vout = ISL_READ_VOUT_DEFAULT;
    s->read_iout = ISL_READ_IOUT_DEFAULT;
    s->read_pout = ISL_READ_POUT_DEFAULT;
    s->read_vin = ISL_READ_VIN_DEFAULT;
    s->read_iin = ISL_READ_IIN_DEFAULT;
    s->read_pin = ISL_READ_PIN_DEFAULT;
    s->read_temperature_1 = ISL_READ_TEMP_DEFAULT;
    s->read_temperature_2 = ISL_READ_TEMP_DEFAULT;
    s->read_temperature_3 = ISL_READ_TEMP_DEFAULT;

    // Apply to PMBus page 0
    pmdev->pages[0].operation = s->operation;
    pmdev->pages[0].on_off_config = s->on_off_config;
    pmdev->pages[0].vout_mode = s->vout_mode;
    pmdev->pages[0].vout_command = s->vout_command;
    pmdev->pages[0].vout_max = s->vout_max;
    pmdev->pages[0].vout_margin_high = s->vout_margin_high;
    pmdev->pages[0].vout_margin_low = s->vout_margin_low;
    pmdev->pages[0].vout_transition_rate = s->vout_transition_rate;
    pmdev->pages[0].vout_ov_fault_limit = s->vout_ov_fault_limit;
    pmdev->pages[0].ot_fault_limit = s->ot_fault_limit;
    pmdev->pages[0].ot_warn_limit = s->ot_warn_limit;
    pmdev->pages[0].vin_ov_warn_limit = s->vin_ov_warn_limit;
    pmdev->pages[0].vin_uv_warn_limit = s->vin_uv_warn_limit;
    pmdev->pages[0].iin_oc_fault_limit = s->iin_oc_fault_limit;
    pmdev->pages[0].ton_delay = s->ton_delay;
    pmdev->pages[0].ton_rise = s->ton_rise;
    pmdev->pages[0].toff_fall = s->toff_fall;
    pmdev->pages[0].revision = s->revision;

    pmdev->pages[0].read_vout = s->read_vout;
    pmdev->pages[0].read_iout = s->read_iout;
    pmdev->pages[0].read_pout = s->read_pout;
    pmdev->pages[0].read_vin = s->read_vin;
    pmdev->pages[0].read_iin = s->read_iin;
    pmdev->pages[0].read_pin = s->read_pin;
    pmdev->pages[0].read_temperature_1 = s->read_temperature_1;
    pmdev->pages[0].read_temperature_2 = s->read_temperature_2;
    pmdev->pages[0].read_temperature_3 = s->read_temperature_3;
}


static void ir38263_class_init(ObjectClass *klass, void *data)
{
    PMBusDeviceClass *k = PMBUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Infineon IR38263 Buck Regulator (PMBus)";
    k->receive_byte = ir38263_receive_byte;
    k->write_data = ir38263_write_data;
    k->device_num_pages = 1;
}

static const TypeInfo ir38263_info = {
    .name = TYPE_IR38263,
    .parent = TYPE_PMBUS_DEVICE,
    .instance_size = sizeof(IR38263State),
    .instance_init = ir38263_init,
    .class_init = ir38263_class_init,
};

static void ir38263_register_types(void)
{
    type_register_static(&ir38263_info);
}

type_init(ir38263_register_types)
