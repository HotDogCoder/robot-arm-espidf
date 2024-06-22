#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TRIGGER_PIN GPIO_NUM_4
#define ECHO_PIN GPIO_NUM_2

void ultrasonic_init(void)
{
    esp_rom_gpio_pad_select_gpio(TRIGGER_PIN);
    esp_rom_gpio_pad_select_gpio(ECHO_PIN);
    gpio_set_direction(TRIGGER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);
}

float ultrasonic_measure_distance(void)
{
    gpio_set_level(TRIGGER_PIN, 0);
    esp_rom_delay_us(2);
    gpio_set_level(TRIGGER_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIGGER_PIN, 0);

    uint32_t pulse_start = 0;
    uint32_t pulse_end = 0;

    while (gpio_get_level(ECHO_PIN) == 0)
    {
        pulse_start = esp_timer_get_time();
    }

    while (gpio_get_level(ECHO_PIN) == 1)
    {
        pulse_end = esp_timer_get_time();
    }

    uint32_t pulse_duration = pulse_end - pulse_start;
    float distance = pulse_duration * 0.034 / 2;

    return distance;
}

void app_main(void)
{
    ultrasonic_init();

    while (1)
    {
        float distance = ultrasonic_measure_distance();
        printf("Distance: %.2f cm\n", distance);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}