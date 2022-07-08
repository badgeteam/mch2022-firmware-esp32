#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdbool.h>

#include "ili9341.h"
#include "pax_gfx.h"

bool create_dir(const char* path);
bool install_app(xQueueHandle button_queue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char* type_slug, bool to_sd_card, char* data_app_info,
                 size_t size_app_info, cJSON* json_app_info);
