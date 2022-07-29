#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdbool.h>

bool create_dir(const char* path);
bool install_app(xQueueHandle button_queue, const char* type_slug, bool to_sd_card, char* data_app_info, size_t size_app_info, cJSON* json_app_info);
