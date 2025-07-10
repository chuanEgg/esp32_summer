#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/timer.h"
#include "driver/gpio.h"

static const char *TAG = "bouncing_ball";

#define SPI_CLK_GPIO GPIO_NUM_18
#define SPI_MOSI_GPIO GPIO_NUM_23
#define SPI_CS_GPIO GPIO_NUM_5

#define FRAME_TIME_MS 100
#define INIT_POS_X 3
#define INIT_POS_Y 3
#define INIT_VEL_X -1
#define INIT_VEL_Y -1
#define END_POS_X 7
#define END_POS_Y 7

spi_device_handle_t max7219;

int8_t ball_x = INIT_POS_X;
int8_t ball_y = INIT_POS_Y;
int8_t vel_x = INIT_VEL_X;
int8_t vel_y = INIT_VEL_Y;
uint32_t frame_count = 0;

static void max7219_write(uint8_t reg, uint8_t val)
{
  uint16_t word = (reg << 8) | val;
  spi_transaction_t t = {
      .length = 16,
      .tx_buffer = &word,
  };
  esp_err_t ret = spi_device_transmit(max7219, &t);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "SPI transmit error: %d", ret);
  }
}

static void max7219_init(void)
{
  // decode mode off
  max7219_write(0x09, 0x00);
  // intensity (brightness)
  max7219_write(0x0A, 0x05);
  // scan limit: 0-7 rows
  max7219_write(0x0B, 0x07);
  // exit shutdown
  max7219_write(0x0C, 0x01);
  // display test
  max7219_write(0x0F, 0x00);
  // for (int row = 1; row <= 8; ++row)
  // {
  //   max7219_write(row, 0x00);
  // }
}

static void draw_frame(void)
{
  uint8_t buffer[8] = {0};
  // draw left wall at x=0
  for (int y = 0; y < 8; ++y)
  {
    buffer[y] |= 0x01;
  }
  // draw ball at (ball_x, ball_y)
  if (ball_x >= 1 && ball_x < 8 && ball_y >= 0 && ball_y < 8)
  {
    buffer[ball_y] |= (1 << ball_x);
  }
  // send rows: Digit1->row0 ... Digit8->row7
  for (int row = 0; row < 8; ++row)
  {
    max7219_write(row + 1, buffer[row]);
  }
}

static bool IRAM_ATTR timer_isr_callback(void *args)
{
  // clear interrupt
  timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
  timer_group_enable_alarm_in_isr(TIMER_GROUP_0, TIMER_0);

  draw_frame();
  frame_count++;

  // update position
  ball_x += vel_x;
  ball_y += vel_y;
  // bounce on vertical borders (x==1..7)
  if (ball_x >= 7 || ball_x <= 1)
    vel_x = -vel_x;
  // bounce on horizontal borders (y==0..7)
  if (ball_y >= 7 || ball_y <= 0)
    vel_y = -vel_y;

  // check end condition
  if (ball_x == END_POS_X && ball_y == END_POS_Y)
  {
    // turn off display
    for (int r = 1; r <= 8; ++r)
    {
      max7219_write(r, 0x00);
    }
    max7219_write(0x0C, 0x00); // enter shutdown mode
    // print frame count
    ESP_LOGI(TAG, "Frame count: %lu\n", frame_count);
    // disable further interrupts
    timer_disable_intr(TIMER_GROUP_0, TIMER_0);
  }
  return true; // keep alarm active
}

static void init_timer(void)
{
  timer_config_t config = {
      .alarm_en = true,
      .counter_en = false,
      .intr_type = TIMER_INTR_LEVEL,
      .counter_dir = TIMER_COUNT_UP,
      .auto_reload = true,
      .divider = 80, // 1us tick (80MHz/80)
  };
  timer_init(TIMER_GROUP_0, TIMER_0, &config);
  // set alarm value in ticks
  const double interval_us = FRAME_TIME_MS * 1000.0;
  timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, (uint64_t)interval_us);
  timer_enable_intr(TIMER_GROUP_0, TIMER_0);
  timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, timer_isr_callback, NULL, 0);
  timer_start(TIMER_GROUP_0, TIMER_0);
}

void app_main(void)
{
  esp_err_t ret;
  // 1) init SPI bus
  spi_bus_config_t buscfg = {
      .mosi_io_num = SPI_MOSI_GPIO,
      .miso_io_num = -1,
      .sclk_io_num = SPI_CLK_GPIO,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };
  ret = spi_bus_initialize(HSPI_HOST, &buscfg, 1);
  ESP_ERROR_CHECK(ret);
  // 2) attach MAX7219
  spi_device_interface_config_t devcfg = {
      .clock_speed_hz = 1 * 1000 * 1000,
      .mode = 0,
      .spics_io_num = SPI_CS_GPIO,
      .queue_size = 1,
  };
  ret = spi_bus_add_device(HSPI_HOST, &devcfg, &max7219);
  ESP_ERROR_CHECK(ret);

  // 3) init MAX7219
  max7219_init();

  // 4) init timer for frame updates
  init_timer();

  ESP_LOGI(TAG, "Bouncing ball started. FPS=%d", 1000 / FRAME_TIME_MS);
  // main task can idle or perform other work
  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}