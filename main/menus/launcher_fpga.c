#include "launcher_fpga.h"

#include <cJSON.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs.h"
#include "appfs_wrapper.h"
#include "bootscreen.h"
#include "fpga_download.h"
#include "fpga_util.h"
#include "graphics_wrapper.h"
#include "hardware.h"
#include "ice40.h"
#include "ili9341.h"
#include "menu.h"
#include "metadata.h"
#include "pax_codecs.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "rtc_memory.h"
#include "system_wrapper.h"

extern const uint8_t bitstream_png_start[] asm("_binary_bitstream_png_start");
extern const uint8_t bitstream_png_end[] asm("_binary_bitstream_png_end");

static void start_fpga_app(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char* path) {
    const pax_font_t* font = pax_font_saira_regular;
    char              filename[128];
    snprintf(filename, sizeof(filename), "%s/bitstream.bin", path);
    FILE* fd = fopen(filename, "rb");
    if (fd == NULL) {
        pax_background(pax_buffer, 0xFFFFFF);
        pax_draw_text(pax_buffer, 0xFFFF0000, font, 18, 0, 0, "Failed to open file\n\nPress A or B to go back");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
        return;
    }
    size_t   bitstream_length = get_file_size(fd);
    uint8_t* bitstream        = load_file_to_ram(fd);
    ICE40*   ice40            = get_ice40();
    ili9341_deinit(ili9341);
    ili9341_select(ili9341, false);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    ili9341_select(ili9341, true);
    esp_err_t res = ice40_load_bitstream(ice40, bitstream, bitstream_length);
    free(bitstream);
    fclose(fd);
    if (res == ESP_OK) {
        fpga_irq_setup(ice40);
        fpga_host(button_queue, ice40, pax_buffer, ili9341, false, path);
        fpga_irq_cleanup(ice40);
        ice40_disable(ice40);
        ili9341_init(ili9341);
    } else {
        ice40_disable(ice40);
        ili9341_init(ili9341);
        pax_background(pax_buffer, 0xFFFFFF);
        pax_draw_text(pax_buffer, 0xFFFF0000, font, 18, 0, 0, "Failed to load bitstream\n\nPress A or B to go back");
        ili9341_write(ili9341, pax_buffer->buf);
        wait_for_button(button_queue);
    }
}

static bool populate_menu(menu_t* menu) {
    bool internal_result = populate_menu_from_path(menu, "/internal/apps/ice40", (void*) bitstream_png_start, bitstream_png_end - bitstream_png_start);
    bool sdcard_result   = populate_menu_from_path(menu, "/sd/apps/ice40", (void*) bitstream_png_start, bitstream_png_end - bitstream_png_start);
    return internal_result | sdcard_result;
}

static void show_app_details(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char* app_slug) {
    //char* title = NULL;
    //parse_metadata(metadata_file_path, &title, NULL, NULL, NULL, NULL);
    pax_simple_rect(pax_buffer, 0xFFFFFFFF, 0, pax_buffer->height - 20, pax_buffer->width, 20);
    pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, pax_buffer->height - 18, "🅰 start  🅱 back  🅴 uninstall");
    render_outline(pax_buffer, 0, 0, pax_buffer->width, pax_buffer->height - 20, 0xFF43b5a0, 0xFFFFFFFF);
    render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFFFFFFFF, 0xFF43b5a0, NULL, app_slug);
    ili9341_write(ili9341, pax_buffer->buf);
}

void menu_launcher_fpga(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341) {
    menu_t* menu = menu_alloc("FPGA apps", 34, 18);

    menu->fgColor           = 0xFF000000;
    menu->bgColor           = 0xFFFFFFFF;
    menu->bgTextColor       = 0xFFFFFFFF;
    menu->selectedItemColor = 0xFF491d88;
    menu->borderColor       = 0xFF43b5a0;
    menu->titleColor        = 0xFF491d88;
    menu->titleBgColor      = 0xFF43b5a0;
    menu->scrollbarBgColor  = 0xFFCCCCCC;
    menu->scrollbarFgColor  = 0xFF555555;

    pax_buf_t icon_bitstream;
    pax_decode_png_buf(&icon_bitstream, (void*) bitstream_png_start, bitstream_png_end - bitstream_png_start, PAX_BUF_32_8888ARGB, 0);
    menu_set_icon(menu, &icon_bitstream);

    bool empty = !populate_menu(menu);

    char* app_to_start = NULL;
    bool  render       = true;
    bool  render_help  = true;
    bool  quit         = false;
    while (!quit) {
        if (render_help) {
            const pax_font_t* font = pax_font_saira_regular;
            pax_background(pax_buffer, 0xFFFFFF);
            pax_noclip(pax_buffer);
            pax_draw_text(pax_buffer, 0xFF491d88, font, 18, 5, 240 - 18, "🅰 start  🅱 back  🅼 options");
            render_help = false;
        }

        if (render) {
            menu_render(pax_buffer, menu, 0, 0, 320, 220);
            if (empty) render_message(pax_buffer, "No FPGA bitstreams installed");
            ili9341_write(ili9341, pax_buffer->buf);
            render = false;
        }
        
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(button_queue, &buttonMessage, 16 / portTICK_PERIOD_MS) == pdTRUE) {
            if (buttonMessage.state) {
                switch (buttonMessage.input) {
                    case RP2040_INPUT_JOYSTICK_DOWN:
                        menu_navigate_next(menu);
                        render = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_UP:
                        menu_navigate_previous(menu);
                        render = true;
                        break;
                    case RP2040_INPUT_BUTTON_HOME:
                    case RP2040_INPUT_BUTTON_BACK:
                        quit = true;
                        break;
                    case RP2040_INPUT_BUTTON_ACCEPT:
                    case RP2040_INPUT_JOYSTICK_PRESS:
                    case RP2040_INPUT_BUTTON_SELECT:
                    case RP2040_INPUT_BUTTON_START:
                        app_to_start = (char*) menu_get_callback_args(menu, menu_get_position(menu));
                        break;
                    case RP2040_INPUT_BUTTON_MENU:
                        show_app_details(button_queue, pax_buffer, ili9341, (char*) menu_get_callback_args(menu, menu_get_position(menu)));
                        break;
                    default:
                        break;
                }
            }
        }

        if (app_to_start != NULL) {
            display_boot_screen(pax_buffer, ili9341, "Starting app...");
            start_fpga_app(button_queue, pax_buffer, ili9341, app_to_start);
            app_to_start = NULL;
            render       = true;
            render_help  = true;
        }
    }

    for (size_t index = 0; index < menu_get_length(menu); index++) {
        pax_buf_t* icon = menu_get_icon(menu, index);
        if (icon != NULL) pax_buf_destroy(icon);
        free(menu_get_callback_args(menu, index));
    }

    menu_free(menu);
    pax_buf_destroy(&icon_bitstream);
}
