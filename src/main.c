#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_PIN GPIO_NUM_2 // Define the GPIO pin for the onboard LED

void led_init(void)
{
    esp_rom_gpio_pad_select_gpio(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
}

void toggle_led_task(void *pvParameter)
{
    bool led_state = false;

    while (1)
    {
        // Toggle the LED state
        led_state = !led_state;
        gpio_set_level(LED_PIN, led_state);

        // Print the LED state
        printf("LED is %s\n", led_state ? "ON" : "OFF");

        // Delay for 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    led_init();

    // Create a task to toggle the LED
    xTaskCreate(&toggle_led_task, "toggle_led_task", 2048, NULL, 5, NULL);
}
