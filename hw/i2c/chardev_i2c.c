/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SPDX-FileCopyrightText: Copyright (c) 2025 9elements GmbH
 * SPDX-FileContributor: Christian Grönke <christian.groenke@9elements.com>
 */

/*
 * I2C device that sends/receives to/from a chardev
 *
 * The code is intended to work with master/slave capable controllers like aspeeds_i2c_bus.
 *
 * (1) local master -> send() -> qemu_chr_fe_write_all(<chardev>, <data>, <len>) -> remote slave
 *
 *     local master -> chardev_i2c_event(I2C_FINISH) -\
 * (2) remote master -> chardev_i2c_bh(CHARDEV_I2C_STATE_START_SEND) -> i2c_[start_]send_async() -> local slave
 *
 * TODO
 * - Need to send master/slave signals (START/ACK/END) else we can not consume on the other side. We can probably
 *   treat them as frames rather than control flow for/from the other side
 * - Revisit naming. We mainly intent to serve MCTP like traffic where one side (master) only sends. The send/recv
 *   cycle of a normal I2C sensor does work differently.
 * - Optimize send() code: Buffer bytes until buffer is full or I2C_FINISH is received. Should we handle ASYNC?
 * - Close/error conditions. We need to consider async. conditions.
 *     - check: qemu_chr_fe_set_handlers (eg. vhost-user-base.c:vub_device_realize() + vhost-user-base.c:vub_event()
 * - What if we are blocked on a receive and in the meantime the bus is owned by someone else?
 *
 * NOTE
 * - qemu_chr_fe_[write/read]_all sends full messages (not partial)
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "block/aio.h"
#include "chardev/char-fe.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/i2c/i2c.h"
#include "trace.h"

#include "hw/i2c/chardev_i2c.h"

static void chardev_i2c_bh(void *opaque)
{
    ChardevI2CDevice *state = opaque;

    switch (state->remote) {
    case CHARDEV_I2C_REMOTE_IDLE:
        trace_chardev_i2c_bh(DEVICE(&state->parent_obj)->canonical_path, "CHARDEV_I2C_REMOTE_IDLE");
        /* dummy/example code */
        return;

    case CHARDEV_I2C_REMOTE_START_SEND:
        /* sanity check */
        if (state->rx_active && state->rx_buf_len > 0)
        {
            trace_chardev_i2c_bh_error(DEVICE(&state->parent_obj)->canonical_path,
                                        "rx_active already set in START_SEND");
        }

        state->rx_active = true;
        state->rx_buf_len = 0;

        /*
         * The code block below will try to read the header first and then all data.
         * We will validate the data later. This will always consume a full message (hdr + body).
         * Except an error occurred.
         */

        msg_hdr hdr;

        int rc = qemu_chr_fe_read_all(&state->chardev, (uint8_t*)&hdr, sizeof(hdr));
        trace_chardev_i2c_chardev_read(DEVICE(&state->parent_obj)->canonical_path,
                                        "header", sizeof(hdr), rc);
        if (rc != sizeof(hdr) || hdr.magic != CHARDEV_I2C_MAGIC || hdr.version != CHARDEV_I2C_VERSION)
        {
            trace_chardev_i2c_bh_error(DEVICE(&state->parent_obj)->canonical_path,
                                        "header read failed or invalid magic/version");
            goto release_bus;
        }

        rc = qemu_chr_fe_read_all(&state->chardev, state->rx_buf, hdr.len);
        trace_chardev_i2c_chardev_read(DEVICE(&state->parent_obj)->canonical_path,
                                        "data", hdr.len, rc);
        if (rc != hdr.len)
        {
            trace_chardev_i2c_bh_error(DEVICE(&state->parent_obj)->canonical_path,
                                        "data read failed");
            goto release_bus;
        }

        state->rx_buf_len = hdr.len;

        if (i2c_start_send_async(state->bus, hdr.dst_addr))
        {
            trace_chardev_i2c_bh_error(DEVICE(&state->parent_obj)->canonical_path,
                                        "i2c_start_send_async failed");
            goto release_bus;
        }

        state->remote = CHARDEV_I2C_REMOTE_SEND_BYTE;

        trace_chardev_i2c_bh(DEVICE(&state->parent_obj)->canonical_path, "CHARDEV_I2C_REMOTE_START_SEND");
        return;

    case CHARDEV_I2C_REMOTE_SEND_BYTE:
        trace_chardev_i2c_bh(DEVICE(&state->parent_obj)->canonical_path, "CHARDEV_I2C_REMOTE_SEND_BYTE");
        for (int i = 0; i < state->rx_buf_len; i++)
        {
            if (i2c_send_async(state->bus, state->rx_buf[i]))
            {
                trace_chardev_i2c_bh_error(DEVICE(&state->parent_obj)->canonical_path,
                                            "i2c_send_async failed");
                goto release_bus;
            }
        }

        return;
    }

    i2c_end_transfer(state->bus);
release_bus:
    i2c_bus_release(state->bus);

    state->rx_active = false;
    state->rx_buf_len = 0;

    state->remote = CHARDEV_I2C_REMOTE_IDLE;
}

static int chardev_i2c_event(I2CSlave *s, enum i2c_event event)
{
    ChardevI2CDevice *state = CHARDEV_I2C_DEVICE(s);
    int ret = -1;

    switch (event) {
    case I2C_START_RECV:
        trace_chardev_i2c_event(DEVICE(s)->canonical_path, "I2C_START_RECV");
        /* what todo? */
        ret = 0;
        break;

    case I2C_START_SEND_ASYNC:
        trace_chardev_i2c_event(DEVICE(s)->canonical_path, "I2C_START_SEND_ASYNC");
        if (!state->tx_active)
        {
            state->tx_active = true;

			state->tx_buf[0] = (uint8_t)((s->address << 1) & 0xfe);
			state->tx_buf_len = 1;

            ret = 0;
        }

        break;

    case I2C_START_SEND:
        trace_chardev_i2c_event(DEVICE(s)->canonical_path, "I2C_START_SEND");
        if (!state->tx_active)
        {
            state->tx_active = true;

			state->tx_buf[0] = (uint8_t)((s->address << 1) & 0xfe);
			state->tx_buf_len = 1;

            ret = 0;
        }

        break;

    case I2C_FINISH:
        trace_chardev_i2c_event(DEVICE(s)->canonical_path, "I2C_FINISH");
        /* send package */
        if (state->tx_active && state->tx_buf_len > 0)
        {
            const uint16_t len = state->tx_buf_len;

            const msg_hdr hdr = {
                CHARDEV_I2C_MAGIC,
                CHARDEV_I2C_VERSION,
                len,
                0, /* normal i2c doesn't encode its own address anywhere... */
                s->address,
            };

            /* send header first */
            int rc = qemu_chr_fe_write_all(&state->chardev, (const uint8_t*)&hdr, sizeof(hdr));
            trace_chardev_i2c_chardev_write(DEVICE(s)->canonical_path,
                                             "header", sizeof(hdr), rc);
            if (rc == sizeof(hdr))
            {
                /* send burst data; account for the destination address and start at offset 1 */
                rc = qemu_chr_fe_write_all(&state->chardev, &state->tx_buf[0], len);
                trace_chardev_i2c_chardev_write(DEVICE(s)->canonical_path,
                                                 "data", len, rc);
                if (rc == len)
                {
                    ret = 0;
                } else
                {
                    trace_chardev_i2c_bh_error(DEVICE(s)->canonical_path,
                                                "chardev data write failed");
                }
            } else
            {
                trace_chardev_i2c_bh_error(DEVICE(s)->canonical_path,
                                            "chardev header write failed");
            }

            state->tx_buf_len = 0;
            state->tx_active = false;
        }

        /* allow the remote side to send data */
        state->remote = CHARDEV_I2C_REMOTE_START_SEND;
        i2c_bus_master(state->bus, state->bh);

        break;

    case I2C_NACK:
        trace_chardev_i2c_event(DEVICE(s)->canonical_path, "I2C_NACK");
        /* what todo? */
        ret = 0;
        break;

    default:
        trace_chardev_i2c_event(DEVICE(s)->canonical_path, "UNHANDLED");
        ret = -1;
    }

    return ret;
}

static int chardev_i2c_send(I2CSlave *s, uint8_t data)
{
    ChardevI2CDevice *state = CHARDEV_I2C_DEVICE(s);
    int ret = -1;

    if (state->tx_active &&
        state->tx_buf_len < state->max_xmit_size)
    {
        state->tx_buf[state->tx_buf_len++] = data;
        ret = 0;
    }

    trace_chardev_i2c_send(DEVICE(s)->canonical_path, data, state->tx_buf_len, ret);
    return ret;
}

static void chardev_i2c_realize(DeviceState *dev, Error **errp)
{
    ChardevI2CDevice *state = CHARDEV_I2C_DEVICE(dev);
    BusState *bus = qdev_get_parent_bus(dev);

    state->bus = I2C_BUS(bus);
    state->bh = qemu_bh_new(chardev_i2c_bh, state);

    state->tx_buf = g_malloc0(state->max_xmit_size);
    state->tx_buf_len = 0;
    state->tx_active = false;

    state->rx_buf = g_malloc0(state->max_xmit_size);
    state->rx_buf_len = 0;
    state->rx_active = false;
}

static void chardev_i2c_unrealize(DeviceState *dev)
{
    ChardevI2CDevice *state = CHARDEV_I2C_DEVICE(dev);

    qemu_bh_delete(state->bh);
    g_free(state->tx_buf);
    g_free(state->rx_buf);
}

static Property chardev_i2c_properties[] = {
    DEFINE_PROP_CHR("chardev", ChardevI2CDevice, chardev),
    DEFINE_PROP_UINT16("xmit_size", ChardevI2CDevice, max_xmit_size, CHARDEV_I2C_DFT_BUF_SIZE),
    DEFINE_PROP_END_OF_LIST(),
};

static void chardev_i2c_class_init(ObjectClass *oc, void *data)
{
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = chardev_i2c_realize;
    dc->unrealize = chardev_i2c_unrealize;

    sc->event = chardev_i2c_event;
    sc->send = chardev_i2c_send;

    device_class_set_props(dc, chardev_i2c_properties);
    //set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo chardev_i2c = {
    .name = TYPE_CHARDEV_I2C_DEVICE,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(ChardevI2CDevice),
    .class_init = chardev_i2c_class_init,
};

static void register_types(void)
{
    type_register_static(&chardev_i2c);
}

type_init(register_types);
