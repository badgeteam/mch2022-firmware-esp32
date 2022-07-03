#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>

#include "hardware.h"
#include "ili9341.h"
#include "pax_gfx.h"
#include "rp2040.h"

void test_adc(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    bool quit = false;

    RP2040*           rp2040 = get_rp2040();
    const pax_font_t* font   = pax_font_saira_regular;

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

        /*uint16_t raw_temperature = 0;
        if (rp2040_read_temp(rp2040, &raw_temperature) != ESP_OK) {
            error = true;
        }

        uint8_t charging = 0;
        if (rp2040_get_charging(rp2040, &charging) != ESP_OK) {
            error = true;
        }*/

        // const float conversion_factor = 3.3f / (1 << 12); // 12-bit ADC with 3.3v vref
        // float vtemperature = raw_temperature * conversion_factor; // Inside of RP2040 chip
        // float temperature = 27 - (vtemperature - 0.706) / 0.001721; // From https://raspberrypi.github.io/pico-sdk-doxygen/group__hardware__adc.html

        pax_noclip(pax_buffer);
        pax_background(pax_buffer, 0x325aa8);
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 0, "Analog inputs");
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Battery voltage %f v", vbat);
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 1, buffer);
        snprintf(buffer, sizeof(buffer), "USB voltage     %f v", vusb);
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 2, buffer);
        /*snprintf(buffer, sizeof(buffer), "Temperature     %f *c", temperature);
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20*3, buffer);
        snprintf(buffer, sizeof(buffer), "Charging        %02X", charging);
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20*4, buffer);*/
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 5, (error ? "ERROR" : ""));
        ili9341_write(ili9341, pax_buffer->buf);

        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(buttonQueue, &buttonMessage, 250 / portTICK_PERIOD_MS) == pdTRUE) {
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
