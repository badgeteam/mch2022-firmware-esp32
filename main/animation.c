#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <driver/gpio.h>
#include "pax_gfx.h"
#include "pax_codecs.h"
#include "ili9341.h"
#include "ws2812.h"
#include "hardware.h"

extern const uint8_t animation_frame_1_start[] asm("_binary_animation_frame_1_png_start");
extern const uint8_t animation_frame_1_end[] asm("_binary_animation_frame_1_png_end");
extern const uint8_t animation_frame_2_start[] asm("_binary_animation_frame_2_png_start");
extern const uint8_t animation_frame_2_end[] asm("_binary_animation_frame_2_png_end");
extern const uint8_t animation_frame_3_start[] asm("_binary_animation_frame_3_png_start");
extern const uint8_t animation_frame_3_end[] asm("_binary_animation_frame_3_png_end");
extern const uint8_t animation_frame_4_start[] asm("_binary_animation_frame_4_png_start");
extern const uint8_t animation_frame_4_end[] asm("_binary_animation_frame_4_png_end");
extern const uint8_t animation_frame_5_start[] asm("_binary_animation_frame_5_png_start");
extern const uint8_t animation_frame_5_end[] asm("_binary_animation_frame_5_png_end");
extern const uint8_t animation_frame_6_start[] asm("_binary_animation_frame_6_png_start");
extern const uint8_t animation_frame_6_end[] asm("_binary_animation_frame_6_png_end");
extern const uint8_t animation_frame_7_start[] asm("_binary_animation_frame_7_png_start");
extern const uint8_t animation_frame_7_end[] asm("_binary_animation_frame_7_png_end");
extern const uint8_t animation_frame_8_start[] asm("_binary_animation_frame_8_png_start");
extern const uint8_t animation_frame_8_end[] asm("_binary_animation_frame_8_png_end");
extern const uint8_t animation_frame_9_start[] asm("_binary_animation_frame_9_png_start");
extern const uint8_t animation_frame_9_end[] asm("_binary_animation_frame_9_png_end");
extern const uint8_t animation_frame_10_start[] asm("_binary_animation_frame_10_png_start");
extern const uint8_t animation_frame_10_end[] asm("_binary_animation_frame_10_png_end");
extern const uint8_t animation_frame_11_start[] asm("_binary_animation_frame_11_png_start");
extern const uint8_t animation_frame_11_end[] asm("_binary_animation_frame_11_png_end");
extern const uint8_t animation_frame_12_start[] asm("_binary_animation_frame_12_png_start");
extern const uint8_t animation_frame_12_end[] asm("_binary_animation_frame_12_png_end");
extern const uint8_t animation_frame_13_start[] asm("_binary_animation_frame_13_png_start");
extern const uint8_t animation_frame_13_end[] asm("_binary_animation_frame_13_png_end");
extern const uint8_t animation_frame_14_start[] asm("_binary_animation_frame_14_png_start");
extern const uint8_t animation_frame_14_end[] asm("_binary_animation_frame_14_png_end");
extern const uint8_t animation_frame_15_start[] asm("_binary_animation_frame_15_png_start");
extern const uint8_t animation_frame_15_end[] asm("_binary_animation_frame_15_png_end");
extern const uint8_t animation_frame_16_start[] asm("_binary_animation_frame_16_png_start");
extern const uint8_t animation_frame_16_end[] asm("_binary_animation_frame_16_png_end");
extern const uint8_t animation_frame_17_start[] asm("_binary_animation_frame_17_png_start");
extern const uint8_t animation_frame_17_end[] asm("_binary_animation_frame_17_png_end");
extern const uint8_t animation_frame_18_start[] asm("_binary_animation_frame_18_png_start");
extern const uint8_t animation_frame_18_end[] asm("_binary_animation_frame_18_png_end");
extern const uint8_t animation_frame_19_start[] asm("_binary_animation_frame_19_png_start");
extern const uint8_t animation_frame_19_end[] asm("_binary_animation_frame_19_png_end");
extern const uint8_t animation_frame_20_start[] asm("_binary_animation_frame_20_png_start");
extern const uint8_t animation_frame_20_end[] asm("_binary_animation_frame_20_png_end");
extern const uint8_t animation_frame_21_start[] asm("_binary_animation_frame_21_png_start");
extern const uint8_t animation_frame_21_end[] asm("_binary_animation_frame_21_png_end");
extern const uint8_t animation_frame_22_start[] asm("_binary_animation_frame_22_png_start");
extern const uint8_t animation_frame_22_end[] asm("_binary_animation_frame_22_png_end");
extern const uint8_t animation_frame_23_start[] asm("_binary_animation_frame_23_png_start");
extern const uint8_t animation_frame_23_end[] asm("_binary_animation_frame_23_png_end");
extern const uint8_t animation_frame_24_start[] asm("_binary_animation_frame_24_png_start");
extern const uint8_t animation_frame_24_end[] asm("_binary_animation_frame_24_png_end");
extern const uint8_t animation_frame_25_start[] asm("_binary_animation_frame_25_png_start");
extern const uint8_t animation_frame_25_end[] asm("_binary_animation_frame_25_png_end");
extern const uint8_t animation_frame_26_start[] asm("_binary_animation_frame_26_png_start");
extern const uint8_t animation_frame_26_end[] asm("_binary_animation_frame_26_png_end");
extern const uint8_t animation_frame_27_start[] asm("_binary_animation_frame_27_png_start");
extern const uint8_t animation_frame_27_end[] asm("_binary_animation_frame_27_png_end");
extern const uint8_t animation_frame_28_start[] asm("_binary_animation_frame_28_png_start");
extern const uint8_t animation_frame_28_end[] asm("_binary_animation_frame_28_png_end");

const uint8_t* animation_frames[] = {
    animation_frame_1_start,
    animation_frame_2_start,
    animation_frame_3_start,
    animation_frame_4_start,
    animation_frame_5_start,
    animation_frame_6_start,
    animation_frame_7_start,
    animation_frame_8_start,
    animation_frame_9_start,
    animation_frame_10_start,
    animation_frame_11_start,
    animation_frame_12_start,
    animation_frame_13_start,
    animation_frame_14_start,
    animation_frame_15_start,
    animation_frame_16_start,
    animation_frame_17_start,
    animation_frame_18_start,
    animation_frame_19_start,
    animation_frame_20_start,
    animation_frame_21_start,
    animation_frame_22_start,
    animation_frame_23_start,
    animation_frame_24_start,
    animation_frame_25_start,
    animation_frame_26_start,
    animation_frame_27_start,
    animation_frame_28_start
};

const uint8_t* animation_frames_end[] = {
    animation_frame_1_end,
    animation_frame_2_end,
    animation_frame_3_end,
    animation_frame_4_end,
    animation_frame_5_end,
    animation_frame_6_end,
    animation_frame_7_end,
    animation_frame_8_end,
    animation_frame_9_end,
    animation_frame_10_end,
    animation_frame_11_end,
    animation_frame_12_end,
    animation_frame_13_end,
    animation_frame_14_end,
    animation_frame_15_end,
    animation_frame_16_end,
    animation_frame_17_end,
    animation_frame_18_end,
    animation_frame_19_end,
    animation_frame_20_end,
    animation_frame_21_end,
    animation_frame_22_end,
    animation_frame_23_end,
    animation_frame_24_end,
    animation_frame_25_end,
    animation_frame_26_end,
    animation_frame_27_end,
    animation_frame_28_end
};

void display_animation(pax_buf_t* pax_buffer, ILI9341* ili9341) {
    
    gpio_set_direction(GPIO_SD_PWR, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_SD_PWR, 1);
    ws2812_init(GPIO_LED_DATA);
    uint8_t led_data[15] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    ws2812_send_data(led_data, sizeof(led_data));
    
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xFFFFFF);
    
    for (uint8_t frame = 0; frame < 28; frame++) {
        pax_buf_t image;
        pax_decode_png_buf(&image, (void*) animation_frames[frame], animation_frames_end[frame] - animation_frames[frame], PAX_BUF_16_565RGB, 0);
        pax_draw_image(pax_buffer, &image, 0, 0);
        pax_buf_destroy(&image);
        ili9341_write(ili9341, pax_buffer->buf);
        uint8_t brightness = (frame > 14) ? (frame - 14) : (0);
        led_data[1] = brightness;
        led_data[3] = brightness;
        led_data[8] = brightness;
        led_data[9] = brightness / 2;
        led_data[10] = brightness / 2;
        led_data[14] = brightness;
        ws2812_send_data(led_data, sizeof(led_data));
    }
    
    for (uint8_t brightness = 14; brightness < 50; brightness++) {
        led_data[1] = brightness;
        led_data[3] = brightness;
        led_data[8] = brightness;
        led_data[9] = brightness / 2;
        led_data[10] = brightness / 2;
        led_data[14] = brightness;
        ws2812_send_data(led_data, sizeof(led_data));
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
