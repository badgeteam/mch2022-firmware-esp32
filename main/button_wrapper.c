#include "button_wrapper.h"

xQueueHandle queue;

void button_handler(uint8_t pin, bool value) {
    button_message_t message;
    message.button = pin;
    message.state = value;
    xQueueSend(queue, &message, portMAX_DELAY);
}

void button_init(PCA9555* aPca9555, xQueueHandle aQueue) {
    queue = aQueue;
    pca9555_set_interrupt_handler(aPca9555, PCA9555_PIN_BTN_START, button_handler);
    pca9555_set_interrupt_handler(aPca9555, PCA9555_PIN_BTN_SELECT, button_handler);
    pca9555_set_interrupt_handler(aPca9555, PCA9555_PIN_BTN_MENU, button_handler);
    pca9555_set_interrupt_handler(aPca9555, PCA9555_PIN_BTN_HOME, button_handler);
    pca9555_set_interrupt_handler(aPca9555, PCA9555_PIN_BTN_JOY_LEFT, button_handler);
    pca9555_set_interrupt_handler(aPca9555, PCA9555_PIN_BTN_JOY_PRESS, button_handler);
    pca9555_set_interrupt_handler(aPca9555, PCA9555_PIN_BTN_JOY_DOWN, button_handler);
    pca9555_set_interrupt_handler(aPca9555, PCA9555_PIN_BTN_JOY_UP, button_handler);
    pca9555_set_interrupt_handler(aPca9555, PCA9555_PIN_BTN_JOY_RIGHT, button_handler);
    pca9555_set_interrupt_handler(aPca9555, PCA9555_PIN_BTN_BACK, button_handler);
    pca9555_set_interrupt_handler(aPca9555, PCA9555_PIN_BTN_ACCEPT, button_handler);
}
