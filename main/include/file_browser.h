#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include "ili9341.h"
#include "pax_gfx.h"

void list_files_in_folder(const char* path);
void file_browser(xQueueHandle buttonQueue, pax_buf_t* pax_buffer, ILI9341* ili9341, const char* initial_path);
