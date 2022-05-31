#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/gpio.h>
#include "hardware.h"
#include "ili9341.h"
#include "ice40.h"
#include "rp2040.h"
#include "fpga_test.h"
#include "pax_gfx.h"

extern const uint8_t fpga_selftest_bin_start[] asm("_binary_fpga_selftest_bin_start");
extern const uint8_t fpga_selftest_bin_end[] asm("_binary_fpga_selftest_bin_end");


static const char *TAG = "fpga_test";

/* SPI commands */
#define SPI_CMD_NOP1                0x00
#define SPI_CMD_SOC_MSG             0x10
#define SPI_CMD_REG_ACCESS          0xf0
#define SPI_CMD_LOOPBACK            0xf1
#define SPI_CMD_LCD_PASSTHROUGH     0xf2
#define SPI_CMD_BUTTON_REPORT       0xf4
#define SPI_CMD_IRQ_ACK             0xfd
#define SPI_CMD_RESP_ACK            0xfe
#define SPI_CMD_NOP2                0xff

/* Messages to self-test SoC */
#define SOC_CMD_PING                0x00
#define SOC_CMD_PING_PARAM          0xc0ffee
#define SOC_CMD_PING_RESP           0xcafebabe

#define SOC_CMD_RGB_STATE_SET       0x10
#define SOC_CMD_IRQN_SET            0x11
#define SOC_CMD_LCD_RGB_CYCLE_SET   0x12
#define SOC_CMD_PMOD_CYCLE_SET      0x13
#define SOC_CMD_LCD_PASSTHROUGH_SET 0x14

#define SOC_CMD_PSRAM_TEST          0x20
#define SOC_CMD_UART_LOOPBACK_TEST  0x21
#define SOC_CMD_PMOD_OPEN_TEST      0x22
#define SOC_CMD_PMOD_PLUG_TEST      0x23
#define SOC_CMD_LCD_INIT_TEST       0x24

#define SOC_CMD_LCD_CHECK_MODE      0x30

#define SOC_RESP_OK                 0x00000000


/* SoC commands */

static bool soc_message(ICE40* ice40, uint8_t cmd, uint32_t param, uint32_t *resp, TickType_t ticks_to_wait) {
    esp_err_t res;
    uint8_t data_tx[6];
    uint8_t data_rx[6];

    /* Default delay */
    ticks_to_wait /= 10;    /* We do 10 retries */
    if (!ticks_to_wait)
        ticks_to_wait = pdMS_TO_TICKS(50);

    /* Prepare message */
    data_tx[0] = SPI_CMD_SOC_MSG;
    data_tx[1] = cmd;
    data_tx[2] = (param >> 16) & 0xff;
    data_tx[3] = (param >>  8) & 0xff;
    data_tx[4] = (param      ) & 0xff;

    /* Send message to PicoRV */
    res = ice40_send_turbo(ice40, data_tx, 5);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "SoC message TX failed");
        return false;
    }

    /* Poll until we get a response */
    data_tx[0] = SPI_CMD_RESP_ACK;

    for (int i=0; i<10; i++) {
        /* Poll */
        res = ice40_transaction(ice40, data_tx, 6, data_rx, 6);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "SoC response RX failed");
            return false;
        }

        /* Was response valid ? */
        if (data_rx[1] & 0x80)
            break;

        /* Wait before retry */
        vTaskDelay(ticks_to_wait);
    }

    if (!(data_rx[1] & 0x80)) {
        ESP_LOGE(TAG, "SoC response RX timeout");
        return false;
    }

    /* Report response */
    if (resp) {
        *resp = 0;
        for (int i=0; i<4; i++)
            *resp = (*resp << 8) | data_rx[2+i];
    }

    return true;
}


/* Test routines */

static bool test_bitstream_load(ICE40* ice40, uint32_t *rc) {
    esp_err_t res;

    res = ice40_load_bitstream(ice40, fpga_selftest_bin_start, fpga_selftest_bin_end - fpga_selftest_bin_start);
    if (res != ESP_OK) {
        *rc = res;
        return false;
    }

    *rc = 0;
    return true;
}

static bool test_spi_loopback_one(ICE40* ice40) {
    esp_err_t res;
    uint8_t data_tx[257];
    uint8_t data_rx[258];

    /* Generate pseudo random sequence */
    data_tx[1] = 1;
    for (int i = 2; i < 257; i++)
        data_tx[i] = (data_tx[i-1] << 1) ^ ((data_tx[i-1] & 0x80) ? 0x1d : 0x00);

    /* Send 256 bytes at high speed with echo command */
    data_tx[0] = SPI_CMD_LOOPBACK;

    res = ice40_send_turbo(ice40, data_tx, 257);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "SPI loopback transaction 1 failed (Turbo TX)");
        return false;
    }

    /* Execute full duplex transaction with next 128 bytes */
    res = ice40_transaction(ice40, data_tx, 257, data_rx, 257);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "SPI loopback transaction 2 failed (Full Duplex)");
        return false;
    }

    /* Validate response present */
    if ((data_rx[1] & 0x80) == 0) {
        ESP_LOGE(TAG, "SPI loopback transaction 2 reports no response available\n");
        return false;
    }

    /* Validate RX data (only 254 byte got read) */
    if (memcmp(&data_rx[2], &data_tx[1], 254)) {
        ESP_LOGE(TAG, "SPI loopback transaction 1->2 integrity fail:\n");
        for (int i = 0; i < 254; i++)
            printf("%02X%c", data_rx[i], ((i&0xf)==0xf) ? '\n' : ' ');
        printf("\n");
        return false;
    }

    /* Read two responses and ack them */
    for (int t = 0; t < 2; t++) {
        /* Receive half duplex */
        res = ice40_receive(ice40, data_rx, 258);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "SPI loopback transaction 3.%d failed (Half Duplex RX)", t);
            return false;
        }

        /* Short acknowledge command */
        data_tx[0] = SPI_CMD_RESP_ACK;

        res = ice40_send_turbo(ice40, data_tx, 1);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "SPI loopback transaction 4.%d failed (Turbo ACK)", t);
            return false;
        }

        /* Validate response present */
        if ((data_rx[1] & 0x80) == 0) {
            ESP_LOGE(TAG, "SPI loopback transaction 3.%d reports no response available\n", t);
            return false;
        }

        /* Validate RX data (only 254 byte got read) */
        if (memcmp(&data_rx[2], &data_tx[1], 254)) {
            ESP_LOGE(TAG, "SPI loopback transaction %d->3.%d integrity fail:\n", 1+t, t);
            for (int i = 0; i < 254; i++)
                printf("%02X%c", data_rx[i], ((i&0xf)==0xf) ? '\n' : ' ');
            printf("\n");
            return false;
        }
    }

    /* Check there is no more responses pending */
    data_tx[0] = SPI_CMD_NOP2;

    res = ice40_transaction(ice40, data_tx, 2, data_rx, 2);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "SPI loopback transaction 5 failed (Full Duplex)");
        return false;
    }

    if ((data_rx[1] & 0x80) != 0) {
        ESP_LOGE(TAG, "SPI loopback transaction 5 reports response available\n");
        return false;
    }

    return true;
}

static bool test_spi_loopback(ICE40* ice40, uint32_t *rc) {
    int i;

    /* Run test 256 times */
    for (i=0; i<256; i++) {
        if (!test_spi_loopback_one(ice40))
            break;
    }

    /* Failure ? */
    if (i != 256) {
        *rc = i + 1;
        return false;
    }

    /* OK ! */
    *rc = 0;
    return true;
}

static bool test_soc_loopback(ICE40 *ice40, uint32_t *rc) {
    /* Execute command */
    if (!soc_message(ice40, SOC_CMD_PING, SOC_CMD_PING_PARAM, rc, 0)) {
        *rc = -1;
        return false;
    }

    /* Check response */
    if (*rc != SOC_CMD_PING_RESP)
        return false;

    /* Success */
    *rc = 0;
    return true;
}

static bool test_uart_loopback(ICE40* ice40, uint32_t *rc) {
    /* Enable loopback mode of RP2040 */
    rp2040_set_fpga_loopback(get_rp2040(), true, true);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Execute command */
    if (!soc_message(ice40, SOC_CMD_UART_LOOPBACK_TEST, 0, rc, 0)) {
        *rc = -1;
        return false;
    }

    /* Disable loopback mode of RP2040 */
    rp2040_set_fpga_loopback(get_rp2040(), true, false);

    /* Check response */
    return *rc == SOC_RESP_OK;
}

static bool test_psram(ICE40* ice40, uint32_t *rc) {
    /* Execute command */
    if (!soc_message(ice40, SOC_CMD_PSRAM_TEST, 0, rc, pdMS_TO_TICKS(1000))) {
        *rc = -1;
        return false;
    }

    /* Check response */
    return *rc == SOC_RESP_OK;
}

static bool test_irq_n(ICE40* ice40, uint32_t *rc) {
    esp_err_t res;

    /* Set pin as input */
    res = gpio_set_direction(GPIO_INT_FPGA, GPIO_MODE_INPUT);
    if (res != ESP_OK) {
        *rc = 32;
        return false;
    }

    /* Assert interrupt line */
    if (!soc_message(ice40, SOC_CMD_IRQN_SET, 1, rc, 0)) {
        *rc = -1;
        return false;
    }

    if (*rc != SOC_RESP_OK)
        return false;

    /* Check level is 0 */
    if (gpio_get_level(GPIO_INT_FPGA) != 0) {
        *rc = 16;
        return false;
    }

    /* Release interrupt line */
    if (!soc_message(ice40, SOC_CMD_IRQN_SET, 0, rc, 0)) {
        *rc = -1;
        return false;
    }

    if (*rc != SOC_RESP_OK)
        return false;

    /* Check level is 1 */
    if (gpio_get_level(GPIO_INT_FPGA) != 1) {
        *rc = 16;
        return false;
    }

    return true;
}

static bool test_lcd_mode(ICE40* ice40, uint32_t *rc) {
    esp_err_t res;
    bool ok;

    /* Defaults */
    ok = true;
    *rc = 0;

    /* Check state is 0 */
    if (!soc_message(ice40, SOC_CMD_LCD_CHECK_MODE, 0, rc, 0)) {
        *rc = 16;
        return false;
    }

    if (*rc != SOC_RESP_OK)
        return false;

    /* Set LCD mode to 1 */
    res = gpio_set_level(GPIO_LCD_MODE, 1);
    if (res != ESP_OK) {
        *rc = 32;
        return false;
    }

    /* Check state is 1 */
    if (!soc_message(ice40, SOC_CMD_LCD_CHECK_MODE, 1, rc, 0)) {
        *rc = 17;
        ok = false;
    }

    if (*rc != SOC_RESP_OK)
        ok = false;

    /* Set LCD mode back to 0 */
    res = gpio_set_level(GPIO_LCD_MODE, 0);
    if (res != ESP_OK) {
        *rc = 33;
        return false;
    }

    /* All good */
    return ok;
}

static bool test_pmod_open(ICE40* ice40, uint32_t *rc) {
    /* Execute command */
    if (!soc_message(ice40, SOC_CMD_PMOD_OPEN_TEST, 0, rc, 0)) {
        *rc = -1;
        return false;
    }

    /* Check response */
    return *rc == SOC_RESP_OK;
}

static bool test_pmod_plug(ICE40* ice40, uint32_t *rc) {
    /* Execute command */
    if (!soc_message(ice40, SOC_CMD_PMOD_PLUG_TEST, 0, rc, 0)) {
        *rc = -1;
        return false;
    }

    /* Check response */
    return *rc == SOC_RESP_OK;
}

static bool test_lcd_init(ICE40* ice40, uint32_t *rc) {
    /* Execute command */
    if (!soc_message(ice40, SOC_CMD_LCD_INIT_TEST, 0, rc, 0)) {
        *rc = -1;
        return false;
    }

    /* Check response */
    return *rc == SOC_RESP_OK;
}


typedef bool (*test_fn)(ICE40 *ice40, uint32_t *rc);


static bool wait_button(xQueueHandle buttonQueue) {
    rp2040_input_message_t buttonMessage = {0};

    while (1) {
        if (xQueueReceive(buttonQueue, &buttonMessage, 0) == pdTRUE) {
            if (buttonMessage.state) {
                switch(buttonMessage.input) {
                    case RP2040_INPUT_BUTTON_HOME:
                    case RP2040_INPUT_BUTTON_MENU:
                    case RP2040_INPUT_BUTTON_BACK:
                        return false;
                    case RP2040_INPUT_BUTTON_ACCEPT:
                        return true;
                    default:
                        break;
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static bool run_test(ICE40* ice40, pax_buf_t* pax_buffer, const pax_font_t *font, ILI9341* ili9341, int line,
         const char *test_name, test_fn fn) {
    bool rv;
    uint32_t rc;

    /* Test name */
    pax_draw_text(pax_buffer, 0xffffffff, font, 18, 0, 20*line, test_name);
    if (ili9341)
        ili9341_write(ili9341, pax_buffer->buf);

    /* Run the test */
    rv = fn(ice40, &rc);

    /* Display result */
    if (!rv) {
        /* Error */
        char buf[10];
        snprintf(buf, sizeof(buf), "%08x", rc);
        pax_draw_text(pax_buffer, 0xffff0000, font, 18, 200, 20*line, buf);
    } else {
        /* OK ! */
        pax_draw_text(pax_buffer, 0xff00ff00, font, 18, 200, 20*line, "      OK");
    }

    if (ili9341)
        ili9341_write(ili9341, pax_buffer->buf);

    /* Pass through the 'OK' status */
    return rv;
}

#define RUN_TEST(name, fn) do {\
    ok &= run_test(ice40, pax_buffer, font, ili9341, line++, name, fn); \
} while (0)

#define RUN_TEST_MANDATORY(name, fn) do {\
    if (!run_test(ice40, pax_buffer, font, ili9341, line++, name, fn)) { \
        pax_draw_text(pax_buffer, 0xffff0000, font, 18, 0, 20*line, "Aborted"); \
        ili9341_write(ili9341, pax_buffer->buf); \
        ok = false; \
        goto error; \
    } \
} while (0)

#define RUN_TEST_BLIND(name, fn) do {\
    ok &= run_test(ice40, pax_buffer, font, NULL, line++, name, fn); \
} while (0)


static void
run_all_tests(xQueueHandle buttonQueue, ICE40* ice40, pax_buf_t* pax_buffer, ILI9341* ili9341)
{
    const pax_font_t *font;
    int line = 0;
    bool ok = true;

    /* Screen init */
    font = pax_get_font("sky mono");

    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0x8060f0);
    ili9341_write(ili9341, pax_buffer->buf);

    /* Run mandatory tests */
    RUN_TEST_MANDATORY("Bitstream load", test_bitstream_load);
    RUN_TEST_MANDATORY("SPI loopback",   test_spi_loopback);
    RUN_TEST_MANDATORY("SoC loopback",   test_soc_loopback);

    /* Set indicator to "in-progress" */
    soc_message(ice40, SOC_CMD_RGB_STATE_SET, 1, NULL, 0);

    /* Run non-interactive tests */
    RUN_TEST("UART loopback",   test_uart_loopback);
    RUN_TEST("PSRAM",           test_psram);
    RUN_TEST("IRQ_n signal",    test_irq_n);
    RUN_TEST("LCD_MODE signal", test_lcd_mode);
    RUN_TEST("PMOD open",       test_pmod_open);

    /* Show instructions for interactive test */
    pax_draw_text(pax_buffer, 0xffc0c0c0, font, 9, 25, 20*line+ 0, "Insert PMOD plug");
    pax_draw_text(pax_buffer, 0xffc0c0c0, font, 9, 25, 20*line+10, "Then press button for interactive test");
    pax_draw_text(pax_buffer, 0xffc0c0c0, font, 9, 25, 20*line+20, " - Check LCD color bars");
    pax_draw_text(pax_buffer, 0xffc0c0c0, font, 9, 25, 20*line+30, " - Then LCD & RGB led color cycling");
    ili9341_write(ili9341, pax_buffer->buf);

    /* Wait for button */
    wait_button(buttonQueue);

    /* Clear the instructions from buffer */
    pax_draw_rect(pax_buffer, 0xff8060f0, 0, 20*line, 320, 240-20*line);

    /* Handover LCD to FPGA */
    ili9341_deinit(ili9341);

    /* Run interactive tests */
    RUN_TEST("PMOD plug", test_pmod_plug);
    RUN_TEST("LCD init",  test_lcd_init);

    /* Wait a second (for user to see color bars) */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Start LCD / RGB cycling */
    soc_message(ice40, SOC_CMD_LCD_RGB_CYCLE_SET, 1, NULL, 0);

    /* Wait for button */
    wait_button(buttonQueue);

    /* Stop LCD / RGB cycling */
    soc_message(ice40, SOC_CMD_LCD_RGB_CYCLE_SET, 0, NULL, 0);

    /* Take control of the LCD back and refresh screen */
    ili9341_init(ili9341);

error:
    /* Update indicator */
    soc_message(ice40, SOC_CMD_RGB_STATE_SET, ok ? 2 : 3, NULL, 0);

    /* Pass / Fail result on screen */
    if (ok)
        pax_draw_text(pax_buffer, 0xff00ff00, font, 36, 100, 20*line, "PASS");
    else
        pax_draw_text(pax_buffer, 0xffff0000, font, 36, 100, 20*line, "FAIL");

    ili9341_write(ili9341, pax_buffer->buf);

    /* Done, just wait for button */
    wait_button(buttonQueue);

    /* Cleanup */
    ice40_disable(ice40);

    return;
}

void fpga_test(xQueueHandle buttonQueue, ICE40* ice40, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    run_all_tests(buttonQueue, ice40, pax_buffer, ili9341);
}
