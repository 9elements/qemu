/*
* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SPDX-FileCopyrightText: Copyright (c) 2025 9elements GmbH
 * SPDX-FileContributor: Christian Grönke <christian.groenke@9elements.com>
 */

#ifndef QEMU_CHARDEV_I2C_H
#define QEMU_CHARDEV_I2C_H

#include <stdint.h>

#define TYPE_CHARDEV_I2C_DEVICE "chardev-i2c"

OBJECT_DECLARE_SIMPLE_TYPE(ChardevI2CDevice, CHARDEV_I2C_DEVICE)

#define CHARDEV_I2C_MAGIC   0xCD
#define CHARDEV_I2C_VERSION 0x01
#define CHARDEV_I2C_DFT_BUF_SIZE UINT8_MAX

typedef struct msg_hdr
{
    uint8_t magic;
    uint8_t version;
    uint16_t len;
    uint8_t src_addr;
    uint8_t dst_addr;
} msg_hdr;

enum chardev_i2c_remote {
    CHARDEV_I2C_REMOTE_IDLE,
    CHARDEV_I2C_REMOTE_START_SEND,
    CHARDEV_I2C_REMOTE_SEND_BYTE,
};

typedef struct ChardevI2CDevice {
    I2CSlave parent_obj;
    I2CBus *bus;

    /* Properties */
    CharBackend chardev;
    uint16_t max_xmit_size;

    /* Device handling */
    enum chardev_i2c_remote remote;
    QEMUBH *bh;

    /* Buffer Queues */
    bool        tx_active;
    uint8_t     *tx_buf;
    uint16_t    tx_buf_len;

    bool        rx_active;
    uint8_t     *rx_buf;
    uint16_t    rx_buf_len;
} ChardevI2CDevice;

#endif //QEMU_CHARDEV_I2C_H
