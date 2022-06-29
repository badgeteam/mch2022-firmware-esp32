/*
 * fpga_util.c
 *
 * Collection of utilities to interact with bitstreams loaded
 * on the iCE40 on the MCH badge and using the "standard" stuff
 * defined for the badge.
 *
 * Copyright (C) 2022  Sylvain Munaut
 * Copyritht (C) 2022  Frans Faase
 */

#include "fpga_util.h"

#include <driver/gpio.h>
#include <errno.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ice40.h"
#include "rp2040.h"

/* ---------------------------------------------------------------------------
 * FPGA IRQ
 * ------------------------------------------------------------------------ */

static SemaphoreHandle_t g_irq_trig;

static void IRAM_ATTR fpga_irq_handler(void *arg) {
    xSemaphoreGiveFromISR(g_irq_trig, NULL);
    portYIELD_FROM_ISR();
}

esp_err_t fpga_irq_setup(ICE40 *ice40) {
    esp_err_t res;

    // Setup semaphore
    g_irq_trig = xSemaphoreCreateBinary();

    // Install handler
    res = gpio_isr_handler_add(ice40->pin_int, fpga_irq_handler, NULL);
    if (res != ESP_OK) return res;

    // Configure GPIO
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_NEGEDGE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << ice40->pin_int,
        .pull_down_en = 0,
        .pull_up_en   = 1,
    };

    res = gpio_config(&io_conf);
    if (res != ESP_OK) return res;

    return ESP_OK;
}

void fpga_irq_cleanup(ICE40 *ice40) {
    // Reconfigure GPIO
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << ice40->pin_int,
        .pull_down_en = 0,
        .pull_up_en   = 1,
    };
    gpio_config(&io_conf);

    // Remove handler
    gpio_isr_handler_remove(ice40->pin_int);

    // Release semaphore
    vSemaphoreDelete(g_irq_trig);
}

bool fpga_irq_wait(TickType_t wait) { return xSemaphoreTake(g_irq_trig, wait) == pdTRUE; }

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

struct fpga_wb_cmdbuf *fpga_wb_alloc(int n) {
    struct fpga_wb_cmdbuf *cb;

    if (n > 511) return NULL;

    cb = calloc(1, sizeof(struct fpga_wb_cmdbuf));

    cb->len = 1 + (n * 8 * sizeof(uint32_t));

    cb->buf    = malloc(cb->len);
    cb->rd_ptr = malloc(64 * sizeof(uint32_t *));

    cb->buf[0] = SPI_CMD_WISHBONE;
    cb->used   = 1;

    return cb;
}

void fpga_wb_free(struct fpga_wb_cmdbuf *cb) {
    if (!cb) return;

    free(cb->buf);
    free(cb->rd_ptr);
    free(cb);
}

bool fpga_wb_queue_write(struct fpga_wb_cmdbuf *cb, int dev, uint32_t addr, uint32_t val) {
    // Limits
    if (cb->done) return false;
    if ((cb->len - cb->used) < 8) return false;

    // Dev sel & Mode (Write, Re-Address)
    cb->buf[cb->used++] = 0xc0 | (dev & 0xf);

    // Address
    cb->buf[cb->used++] = (addr >> 18) & 0xff;
    cb->buf[cb->used++] = (addr >> 10) & 0xff;
    cb->buf[cb->used++] = (addr >> 2) & 0xff;

    // Data
    cb->buf[cb->used++] = (val >> 24) & 0xff;
    cb->buf[cb->used++] = (val >> 16) & 0xff;
    cb->buf[cb->used++] = (val >> 8) & 0xff;
    cb->buf[cb->used++] = (val) &0xff;

    // Done
    return true;
}

bool fpga_wb_queue_read(struct fpga_wb_cmdbuf *cb, int dev, uint32_t addr, uint32_t *val) {
    // Limits
    if (cb->done) return false;
    if ((cb->len - cb->used) < 8) return false;
    if (cb->rd_cnt >= 64) return false;

    // Dev sel & Mode (Write, Re-Address)
    cb->buf[cb->used++] = 0x40 | (dev & 0xf);

    // Address
    cb->buf[cb->used++] = (addr >> 18) & 0xff;
    cb->buf[cb->used++] = (addr >> 10) & 0xff;
    cb->buf[cb->used++] = (addr >> 2) & 0xff;

    // Data
    cb->buf[cb->used++]      = 0x00;
    cb->buf[cb->used++]      = 0x00;
    cb->buf[cb->used++]      = 0x00;
    cb->buf[cb->used++]      = 0x00;
    cb->rd_ptr[cb->rd_cnt++] = val;

    // Done
    return true;
}

bool fpga_wb_queue_write_burst(struct fpga_wb_cmdbuf *cb, int dev, uint32_t addr, const uint32_t *val, int n, bool inc) {
    // Limits
    if (cb->done) return false;
    if ((cb->len - cb->used) < (4 * (n + 1))) return false;

    // Dev sel & Mode (Write, Burst)
    cb->buf[cb->used++] = 0x80 | (inc ? 0x20 : 0x00) | (dev & 0xf);

    // Address
    cb->buf[cb->used++] = (addr >> 18) & 0xff;
    cb->buf[cb->used++] = (addr >> 10) & 0xff;
    cb->buf[cb->used++] = (addr >> 2) & 0xff;

    // Data
    while (n--) {
        cb->buf[cb->used++] = (*val >> 24) & 0xff;
        cb->buf[cb->used++] = (*val >> 16) & 0xff;
        cb->buf[cb->used++] = (*val >> 8) & 0xff;
        cb->buf[cb->used++] = (*val) & 0xff;
        val++;
    }

    // Done (for good !)
    cb->done = true;
    return true;
}

bool fpga_wb_queue_read_burst(struct fpga_wb_cmdbuf *cb, int dev, uint32_t addr, uint32_t *val, int n, bool inc) {
    // Limits
    if (cb->done) return false;
    if ((cb->len - cb->used) < (4 * (n + 1))) return false;
    if ((cb->rd_cnt + n) > 64) return false;

    // Dev sel & Mode (Write, Burst)
    cb->buf[cb->used++] = (inc ? 0x20 : 0x00) | (dev & 0xf);

    // Address
    cb->buf[cb->used++] = (addr >> 18) & 0xff;
    cb->buf[cb->used++] = (addr >> 10) & 0xff;
    cb->buf[cb->used++] = (addr >> 2) & 0xff;

    // Data
    while (n--) {
        cb->buf[cb->used++]      = 0x00;
        cb->buf[cb->used++]      = 0x00;
        cb->buf[cb->used++]      = 0x00;
        cb->buf[cb->used++]      = 0x00;
        cb->rd_ptr[cb->rd_cnt++] = val;
        val++;
    }

    // Done (for good !)
    cb->done = true;
    return true;
}

bool fpga_wb_exec(struct fpga_wb_cmdbuf *cb, ICE40 *ice40) {
    esp_err_t res;
    int       l;

    // Execute transmit transaction with the request
    res = ice40_send(ice40, cb->buf, cb->used);
    if (res != ESP_OK) return false;

    // If there was no read, nothing else to do
    if (!cb->rd_cnt) return true;

    // Execute a half duplex transaction to get the read data back
    l          = 2 + (cb->rd_cnt * 4);
    cb->buf[0] = SPI_CMD_RESP_ACK;

    res = ice40_transaction(ice40, cb->buf, l, cb->buf, l);
    if (res != ESP_OK) return false;

    // Fill data to requester
    for (int i = 0; i < cb->rd_cnt; i++) {
        *(cb->rd_ptr[i]) = ((cb->buf[4 * i + 2] << 24) | (cb->buf[4 * i + 3] << 16) | (cb->buf[4 * i + 4] << 8) | (cb->buf[4 * i + 5]));
    }

    return true;
}

/* ---------------------------------------------------------------------------
 * Button reports
 * ------------------------------------------------------------------------ */

static uint16_t g_btn_state = 0;

void fpga_btn_reset(void) { g_btn_state = 0; }

bool fpga_btn_forward_events(ICE40 *ice40, xQueueHandle buttonQueue, esp_err_t *err) {
    rp2040_input_message_t buttonMessage;
    bool                   work_done = false;

    if (err) *err = ESP_OK;

    while (xQueueReceive(buttonQueue, &buttonMessage, 0) == pdTRUE) {
        uint8_t  pin      = buttonMessage.input;
        bool     value    = buttonMessage.state;
        uint16_t btn_mask = 0;

        switch (pin) {
            case RP2040_INPUT_JOYSTICK_DOWN:
                btn_mask = 1 << 0;
                break;
            case RP2040_INPUT_JOYSTICK_UP:
                btn_mask = 1 << 1;
                break;
            case RP2040_INPUT_JOYSTICK_LEFT:
                btn_mask = 1 << 2;
                break;
            case RP2040_INPUT_JOYSTICK_RIGHT:
                btn_mask = 1 << 3;
                break;
            case RP2040_INPUT_JOYSTICK_PRESS:
                btn_mask = 1 << 4;
                break;
            case RP2040_INPUT_BUTTON_HOME:
                btn_mask = 1 << 5;
                break;
            case RP2040_INPUT_BUTTON_MENU:
                btn_mask = 1 << 6;
                break;
            case RP2040_INPUT_BUTTON_SELECT:
                btn_mask = 1 << 7;
                break;
            case RP2040_INPUT_BUTTON_START:
                btn_mask = 1 << 8;
                break;
            case RP2040_INPUT_BUTTON_ACCEPT:
                btn_mask = 1 << 9;
                break;
            case RP2040_INPUT_BUTTON_BACK:
                btn_mask = 1 << 10;
            default:
                break;
        }

        if (btn_mask != 0) {
            work_done = true;

            if (value)
                g_btn_state |= btn_mask;
            else
                g_btn_state &= ~btn_mask;

            uint8_t spi_message[5] = {
                SPI_CMD_BUTTON_REPORT, g_btn_state >> 8, g_btn_state & 0xff, btn_mask >> 8, btn_mask & 0xff,
            };

            esp_err_t res = ice40_send(ice40, spi_message, 5);
            if (res != ESP_OK) {
                if (err) *err = res;
                return work_done;
            }
        }
    }

    return work_done;
}

/* ---------------------------------------------------------------------------
 * Request processing
 * ------------------------------------------------------------------------ */

struct req_entry {
    struct req_entry *next;

    uint32_t fid;
    FILE    *fh;
    void    *data;
    size_t   len;
    size_t   ofs;
};

struct req_entry *g_req_entries;

static void _fpga_req_delete_entry(uint32_t fid) {
    struct req_entry **re_ptr;
    struct req_entry  *re;

    // Scan entries for a matching one
    re_ptr = &g_req_entries;
    re     = *re_ptr;

    while (re) {
        // Match ?
        if (re->fid == fid) {
            // Remove from list
            *re_ptr = re->next;

            // Release
            if (re->fh) fclose(re->fh);
            free(re);

            // Done
            return;
        }

        // Next
        re_ptr = &re->next;
        re     = *re_ptr;
    }
}

static struct req_entry *_fpga_req_open_file(uint32_t fid, const char *path) {
    struct req_entry *re;
    FILE             *fh;

    // Open file
    fh = fopen(path, "rb");
    if (!fh) return NULL;

    // Alloc new entry
    re = calloc(1, sizeof(struct req_entry));
    if (!re) return NULL;

    // Add it to list
    re->next      = g_req_entries;
    g_req_entries = re;

    // Init fields
    re->fid = fid;
    re->fh  = fh;
    re->ofs = 0;

    fseek(fh, 0, SEEK_END);
    re->len = ftell(fh);
    fseek(fh, 0, SEEK_SET);

    // Done
    return re;
}

static struct req_entry *_fpga_req_get_file(const char *prefix, uint32_t fid) {
    struct req_entry *re;
    char              path[128];

    // Scan entries for a matching one
    for (re = g_req_entries; re; re = re->next)
        if (re->fid == fid) return re;

    // Nothing found, try to open file
    snprintf(path, sizeof(path), "%s/fpga_%08x.dat", prefix, fid);
    printf("FPGA read file '%s'\n", path);
    return _fpga_req_open_file(fid, path);
}

static ssize_t _fpga_req_fread(const char *prefix, uint32_t fid, void *buf, size_t nbyte, size_t ofs) {
    struct req_entry *re;

    // Get or create entry
    re = _fpga_req_get_file(prefix, fid);

#if 0
    printf("rd: %08x %p %6d %6d\n", fid, re, nbyte, ofs);
#endif

    // If not found, or past end, just fill
    if (!re || (ofs >= re->len)) {
        memset(buf, 0x00, nbyte);
        return 0;
    }

    // Deal with requests too large
    if ((ofs + nbyte) > re->len) {
        size_t l = re->len - ofs;
        memset(buf + l, 0x00, nbyte - l);
        nbyte = l;
    }

    // Is it a file
    if (re->fh) {
        // Seek ?
        if (ofs != re->ofs) fseek(re->fh, ofs, SEEK_SET);

        re->ofs = ofs + nbyte;

        // Read data
        nbyte = fread(buf, 1, nbyte, re->fh);
    }

    // Or a raw data block
    else if (re->data) {
        memcpy(buf, re->data + ofs, nbyte);
    }

    return nbyte;
}

void fpga_req_setup(void) { g_req_entries = NULL; }

void fpga_req_cleanup(void) {
    struct req_entry *re_cur, *re_nxt;

    re_cur = g_req_entries;

    while (re_cur) {
        re_nxt = re_cur->next;
        if (re_cur->fh) fclose(re_cur->fh);
        free(re_cur);
        re_cur = re_nxt;
    }

    g_req_entries = NULL;
}

int fpga_req_add_file_alias(uint32_t fid, const char *path) {
    struct req_entry *re;

    // Remove any previous entries
    _fpga_req_delete_entry(fid);

    // Open new one
    re = _fpga_req_open_file(fid, path);
    if (!re) return -ENOENT;

    return 0;
}

int fpga_req_add_file_data(uint32_t fid, void *data, size_t len) {
    struct req_entry *re;
    void             *buf;

    // Remove any previous entries
    _fpga_req_delete_entry(fid);

    // Alloc new entry
    buf = malloc(sizeof(struct req_entry) + len);
    if (!buf) return -ENOMEM;

    re = buf;
    memset(re, 0x00, sizeof(struct req_entry));

    // Add it to list
    re->next      = g_req_entries;
    g_req_entries = re;

    // Init fields
    re->fid  = fid;
    re->data = buf + sizeof(struct req_entry);
    re->len  = len;

    // Copy actual data
    memcpy(re->data, data, len);

    // Done
    return 0;
}

void fpga_req_del_file(uint32_t fid) { _fpga_req_delete_entry(fid); }

bool fpga_req_process(const char *prefix, ICE40 *ice40, TickType_t wait, esp_err_t *err) {
    esp_err_t res;
    uint8_t   buf[12];
    uint8_t   req;

    // Default is no error
    *err = ESP_OK;

    // If the FPGA isn't requesting anything ... we have nothing to do !
    if (!fpga_irq_wait(wait)) return false;

    // Poll status byte to see what's up
    buf[0] = SPI_CMD_NOP2;
    res    = ice40_transaction(ice40, buf, 2, buf, 2);
    if (res != ESP_OK) goto error;

    req = buf[1] & 0xf;

    // File requests
    if (req & SPI_REQ_FREAD) {
        uint32_t req_file_id;
        uint32_t req_offset;
        uint16_t req_length;
        uint8_t *buf_req;

        // Get file request: Command
        buf[0] = SPI_CMD_FREAD_GET;
        res    = ice40_send(ice40, buf, 1);
        if (res != ESP_OK) goto error;

        // Get file request: Response
        buf[0] = SPI_CMD_RESP_ACK;
        res    = ice40_transaction(ice40, buf, 12, buf, 12);
        if (res != ESP_OK) goto error;

        req_file_id = (buf[2] << 24) | (buf[3] << 16) | (buf[4] << 8) | buf[5];
        req_offset  = (buf[6] << 24) | (buf[7] << 16) | (buf[8] << 8) | buf[9];
        req_length  = ((buf[10] << 8) | buf[11]) + 1;

        // Get buffer
        buf_req = malloc(req_length + 1);

        // Load data from file
        _fpga_req_fread(prefix, req_file_id, &buf_req[1], req_length, req_offset);

        // Send data
        buf_req[0] = SPI_CMD_FREAD_PUT;
        res        = ice40_send(ice40, buf_req, req_length + 1);
        if (res != ESP_OK) {
            free(buf_req);
            goto error;
        }

        // Done with buffer
        free(buf_req);
    }

    // Done !
    return true;

error:
    if (err) *err = res;
    return false;
}
