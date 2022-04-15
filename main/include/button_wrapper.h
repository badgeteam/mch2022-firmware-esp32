#pragma once

#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "hardware.h"

typedef struct _button_message {
    uint8_t button;
    bool state;
} button_message_t;

void button_init(PCA9555* aPca9555, xQueueHandle aQueue);
