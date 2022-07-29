#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>

#include "gui_element_header.h"
#include "hardware.h"
#include "pax_gfx.h"
#include "rp2040.h"

void test_adc(xQueueHandle button_queue) {
    pax_buf_t* pax_buffer = get_pax_buffer();
    bool       quit       = false;

    RP2040*           rp2040 = get_rp2040();
    const pax_font_t* font   = pax_font_sky_mono;

    while (!quit) {
        bool error = false;

        float vbat = 0;
        if (rp2040_read_vbat(rp2040, &vbat) != ESP_OK) {
            error = true;
        }

        float vusb = 0;
        if (rp2040_read_vusb(rp2040, &vusb) != ESP_OK) {
            error = true;
        }

        uint16_t raw_temperature = 0;
        if (rp2040_read_temp(rp2040, &raw_temperature) != ESP_OK) {
            error = true;
        }

        uint8_t charging = 0;
        if (rp2040_get_charging(rp2040, &charging) != ESP_OK) {
            error = true;
        }

        const float conversion_factor = 3.3f / (1 << 12);                     // 12-bit ADC with 3.3v vref
        float       vtemperature      = raw_temperature * conversion_factor;  // Inside of RP2040 chip
        float       temperature = 27 - (vtemperature - 0.706) / 0.001721;     // From https://raspberrypi.github.io/pico-sdk-doxygen/group__hardware__adc.html

        pax_noclip(pax_buffer);
        pax_background(pax_buffer, 0x325aa8);
        render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFF000000, 0xFFFFFFFF, NULL, "Analog inputs");
        char buffer[64];
        if (error) {
            pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 2, "Error, failed to read!");
        } else {
            snprintf(buffer, sizeof(buffer), "Batt. voltage %0.2f v", vbat);
            pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 2, buffer);
            snprintf(buffer, sizeof(buffer), "USB voltage   %0.2f v", vusb);
            pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 3, buffer);
            snprintf(buffer, sizeof(buffer), "Temperature   %0.2f *c", temperature);
            pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 4, buffer);
            snprintf(buffer, sizeof(buffer), "Charging      %s", charging ? "Yes" : "No");
            pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 5, buffer);
        }
        display_flush();

        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(button_queue, &buttonMessage, 250 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t pin   = buttonMessage.input;
            bool    value = buttonMessage.state;
            if (value) {
                switch (pin) {
                    case RP2040_INPUT_BUTTON_HOME:
                    case RP2040_INPUT_BUTTON_BACK:
                        quit = true;
                    default:
                        break;
                }
            }
        }
    }
}
