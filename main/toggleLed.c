#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define LED 2
#define RELAY 4

void init_led(void)
{
  gpio_pad_select_gpio(LED);
  gpio_set_direction(LED, GPIO_MODE_OUTPUT);
  gpio_pad_select_gpio(RELAY);
  gpio_set_direction(RELAY, GPIO_MODE_OUTPUT);
  gpio_pulldown_en(RELAY);
  gpio_pullup_dis(RELAY);
}

void toggle_led(bool is_on, int interval_ms)
{  
  // gpio_set_level(RELAY, is_on);
  // gpio_set_level(LED, is_on);
  gpio_set_level(RELAY, 1);
  gpio_set_level(LED, 1);
  vTaskDelay((interval_ms * 1000) / 100 * 15 / portTICK_PERIOD_MS);
  gpio_set_level(RELAY, 0);
  gpio_set_level(LED, 0);
}





