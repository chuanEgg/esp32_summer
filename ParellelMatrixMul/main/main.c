#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>

#define MATRIX_SIZE 4

static const char *TAG = "matrix_multiply";

int M1[MATRIX_SIZE][MATRIX_SIZE];
int M2[MATRIX_SIZE][MATRIX_SIZE];
int M3[MATRIX_SIZE][MATRIX_SIZE];
int sum = 0;
int current_row = 0;

static SemaphoreHandle_t row_mutex;
static SemaphoreHandle_t sum_mutex;
static SemaphoreHandle_t done_count_mul;
static SemaphoreHandle_t done_count_sum;

void multiply_task(void *arg)
{
  int core_id = esp_cpu_get_core_id();
  int64_t start_time, end_time, duration = 0;
  int row;
  while (true)
  {
    start_time = esp_timer_get_time();
    xSemaphoreTake(row_mutex, portMAX_DELAY);
    if (current_row >= MATRIX_SIZE)
    {
      xSemaphoreGive(row_mutex);
      break;
    }
    row = current_row++;
    xSemaphoreGive(row_mutex);
    for (int col = 0; col < MATRIX_SIZE; col++)
    {
      int result = 0;
      for (int k = 0; k < MATRIX_SIZE; k++)
      {
        result += M1[row][k] * M2[k][col];
      }
      M3[row][col] += result;
    }
    end_time = esp_timer_get_time();
    duration = end_time - start_time;
    ESP_LOGI(TAG, "core%d: row%d in %lld", core_id, row, duration);
  }
  xSemaphoreGive(done_count_mul);
  vTaskDelete(NULL);
}

void sum_task(void *arg)
{
  int core_id = esp_cpu_get_core_id();
  int64_t start_time, end_time, duration = 0;
  int row;
  while (true)
  {
    start_time = esp_timer_get_time();
    xSemaphoreTake(row_mutex, portMAX_DELAY);
    if (current_row >= MATRIX_SIZE)
    {
      xSemaphoreGive(row_mutex);
      break;
    }
    row = current_row++;
    xSemaphoreGive(row_mutex);
    xSemaphoreTake(sum_mutex, portMAX_DELAY);
    for (int col = 0; col < MATRIX_SIZE; col++)
    {
      sum += M3[row][col];
    }
    xSemaphoreGive(sum_mutex);
    end_time = esp_timer_get_time();
    duration = end_time - start_time;
    ESP_LOGI(TAG, "core%d: row%d in %lld", core_id, row, duration);
  }
  xSemaphoreGive(done_count_sum);
  vTaskDelete(NULL);
}

void app_main(void)
{
  srand(42);
  for (int i = 0; i < MATRIX_SIZE; i++)
  {
    for (int j = 0; j < MATRIX_SIZE; j++)
    {
      M1[i][j] = rand() % 10;
      M2[i][j] = rand() % 10;
      M3[i][j] = 0;
    }
  }
  int64_t start_time = esp_timer_get_time();
  row_mutex = xSemaphoreCreateMutex();
  sum_mutex = xSemaphoreCreateMutex();
  done_count_mul = xSemaphoreCreateCounting(2, 0);
  done_count_sum = xSemaphoreCreateCounting(2, 0);

  xTaskCreatePinnedToCore(multiply_task, "multiply_task_A", 4096, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(multiply_task, "multiply_task_B", 4096, NULL, 5, NULL, 1);

  xSemaphoreTake(done_count_mul, portMAX_DELAY);
  xSemaphoreTake(done_count_mul, portMAX_DELAY);
  int64_t end_time = esp_timer_get_time();
  ESP_LOGI(TAG, "Multiplication done in %lld", end_time - start_time);
  for (int i = 0; i < MATRIX_SIZE; i++)
  {
    for (int j = 0; j < MATRIX_SIZE; j++)
    {
      ESP_LOGI(TAG, "M3[%d][%d] = %d", i, j, M3[i][j]);
    }
  }
  current_row = 0;
  start_time = esp_timer_get_time();
  xTaskCreatePinnedToCore(sum_task, "sum_task_A", 4096, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(sum_task, "sum_task_B", 4096, NULL, 5, NULL, 1);

  xSemaphoreTake(done_count_sum, portMAX_DELAY);
  xSemaphoreTake(done_count_sum, portMAX_DELAY);
  end_time = esp_timer_get_time();
  ESP_LOGI(TAG, "Sum done in %lld", end_time - start_time);
  ESP_LOGI(TAG, "Total sum = %d", sum);
}