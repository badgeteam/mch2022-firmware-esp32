/*
 * fpga_util.c
 *
 * Collection of utilities to interact with bitstreams loaded
 * on the iCE40 on the MCH badge and using the "standard" stuff
 * defined for the badge.
 *
 * Copyright (C) 2022  Sylvain Munaut
 */

#include <stdbool.h>
#include <stdint.h>

#include "ice40.h"

#include "fpga_util.h"

/* ---------------------------------------------------------------------------
 * Wishbone bridge
 * ------------------------------------------------------------------------ */

struct fpga_wb_cmdbuf {
    bool       done;
    uint8_t   *buf;
    int        len;
    int        used;
    int        rd_cnt;
    uint32_t **rd_ptr;
};

struct fpga_wb_cmdbuf *
fpga_wb_alloc(int n)
{
    struct fpga_wb_cmdbuf *cb;

    if (n > 511)
        return NULL;

    cb = calloc(1, sizeof(struct fpga_wb_cmdbuf));

    cb->len  = 1 + (n * 8 * sizeof(uint32_t));

    cb->buf = malloc(cb->len);
    cb->rd_ptr = malloc(64 * sizeof(uint32_t*));

    cb->buf[0] = 0xf0;
    cb->used = 1;

    return cb;
}

void
fpga_wb_free(struct fpga_wb_cmdbuf *cb)
{
    if (!cb)
        return;

    free(cb->buf);
    free(cb->rd_ptr);
    free(cb);
}

bool
fpga_wb_queue_write(struct fpga_wb_cmdbuf *cb,
                    int dev, uint32_t addr, uint32_t val)
{
    // Limits
    if (cb->done)
        return false;
    if ((cb->len - cb->used) < 8)
        return false;

    // Dev sel & Mode (Write, Re-Address)
    cb->buf[cb->used++] = 0xc0 | (dev & 0xf);

    // Address
    cb->buf[cb->used++] = (addr >> 18) & 0xff;
    cb->buf[cb->used++] = (addr >> 10) & 0xff;
    cb->buf[cb->used++] = (addr >>  2) & 0xff;

    // Data
    cb->buf[cb->used++] = (val >> 24) & 0xff;
    cb->buf[cb->used++] = (val >> 16) & 0xff;
    cb->buf[cb->used++] = (val >>  8) & 0xff;
    cb->buf[cb->used++] = (val      ) & 0xff;

    // Done
    return true;
}

bool
fpga_wb_queue_read(struct fpga_wb_cmdbuf *cb,
                   int dev, uint32_t addr, uint32_t *val)
{
    // Limits
    if (cb->done)
        return false;
    if ((cb->len - cb->used) < 8)
        return false;
    if (cb->rd_cnt >= 64)
        return false;

    // Dev sel & Mode (Write, Re-Address)
    cb->buf[cb->used++] = 0x40 | (dev & 0xf);

    // Address
    cb->buf[cb->used++] = (addr >> 18) & 0xff;
    cb->buf[cb->used++] = (addr >> 10) & 0xff;
    cb->buf[cb->used++] = (addr >>  2) & 0xff;

    // Data
    cb->buf[cb->used++] = 0x00;
    cb->buf[cb->used++] = 0x00;
    cb->buf[cb->used++] = 0x00;
    cb->buf[cb->used++] = 0x00;
    cb->rd_ptr[cb->rd_cnt++] = val;

    // Done
    return true;
}

bool
fpga_wb_queue_write_burst(struct fpga_wb_cmdbuf *cb,
                          int dev, uint32_t addr, const uint32_t *val, int n, bool inc)
{
    // Limits
    if (cb->done)
        return false;
    if ((cb->len - cb->used) < (4 * (n+1)))
        return false;

    // Dev sel & Mode (Write, Burst)
    cb->buf[cb->used++] = 0x80 | (inc ? 0x20 : 0x00) | (dev & 0xf);

    // Address
    cb->buf[cb->used++] = (addr >> 18) & 0xff;
    cb->buf[cb->used++] = (addr >> 10) & 0xff;
    cb->buf[cb->used++] = (addr >>  2) & 0xff;

    // Data
    while (n--) {
        cb->buf[cb->used++] = (*val >> 24) & 0xff;
        cb->buf[cb->used++] = (*val >> 16) & 0xff;
        cb->buf[cb->used++] = (*val >>  8) & 0xff;
        cb->buf[cb->used++] = (*val      ) & 0xff;
        val++;
    }

    // Done (for good !)
    cb->done = true;
    return true;
}

bool
fpga_wb_queue_read_burst(struct fpga_wb_cmdbuf *cb,
                         int dev, uint32_t addr, uint32_t *val, int n, bool inc)
{
    // Limits
    if (cb->done)
        return false;
    if ((cb->len - cb->used) < (4 * (n+1)))
        return false;
    if ((cb->rd_cnt + n) > 64)
        return false;

    // Dev sel & Mode (Write, Burst)
    cb->buf[cb->used++] = (inc ? 0x20 : 0x00) | (dev & 0xf);

    // Address
    cb->buf[cb->used++] = (addr >> 18) & 0xff;
    cb->buf[cb->used++] = (addr >> 10) & 0xff;
    cb->buf[cb->used++] = (addr >>  2) & 0xff;

    // Data
    while (n--) {
        cb->buf[cb->used++] = 0x00;
        cb->buf[cb->used++] = 0x00;
        cb->buf[cb->used++] = 0x00;
        cb->buf[cb->used++] = 0x00;
        cb->rd_ptr[cb->rd_cnt++] = val;
        val++;
    }

    // Done (for good !)
    cb->done = true;
    return true;
}

bool
fpga_wb_exec(struct fpga_wb_cmdbuf *cb, ICE40* ice40)
{
    esp_err_t res;
    int l;

    // Execute transmit transaction with the request
    res = ice40_send(ice40, cb->buf, cb->used);
    if (res != ESP_OK)
        return false;

    // If there was no read, nothing else to do
    if (!cb->rd_cnt)
        return true;

    // Execute a half duplex transaction to get the read data back
    l = 2 + (cb->rd_cnt * 4);
    cb->buf[0] = 0xfe; // RESP_ACK

    res = ice40_transaction(ice40, cb->buf, l, cb->buf, l);
    if (res != ESP_OK)
        return false;

    // Fill data to requester
    for (int i=0; i<cb->rd_cnt; i++) {
        *(cb->rd_ptr[i]) = (
            (cb->buf[4*i+2] << 24) |
            (cb->buf[4*i+3] << 16) |
            (cb->buf[4*i+4] <<  8) |
            (cb->buf[4*i+5])
        );
    }

    return true;
}
