#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_wifi.h"  // Wi-Fi functions
#include "esp_log.h"   // Logging macros
#include "nvs_flash.h" // NVS storage (required for Wi-Fi credentials)
#include "driver/gpio.h"

#include <sys/socket.h> // Core socket functions
#include <netinet/in.h> // sockaddr_in struct and INADDR_ANY
#include <string.h>
#include <arpa/inet.h> // inet_pton and address manipulation

// #include "esp_timer.h"
#include <stdio.h>

// LED Configuration
#define BLINK_GPIO GPIO_NUM_2 // GPIO 2 is commonly used for built-in LED on ESP32 dev boards
static const char *TAG = "UDP_LED";
static uint8_t s_led_state = 0;

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
  {
    esp_wifi_connect();
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    printf("Wi-Fi disconnected. Retrying...\n");
    esp_wifi_connect();
  }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    printf("Got IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void wifi_init_sta()
{

  // Initialize NVS (Non-Volatile Storage) - required for WiFi
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  esp_netif_init(); // Initialize TCP/IP network interface (mandatory)
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta(); // Create default Wi-Fi station interface
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      NULL));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

  esp_wifi_init(&cfg); // Initialize Wi-Fi with default config

  wifi_config_t wifi_config = {
      .sta = {
          .ssid = "EggParty",       // Replace with actual SSID
          .password = "chuan940503" // Replace with actual password
      },
  };

  esp_wifi_set_mode(WIFI_MODE_STA);               // Set station mode
  esp_wifi_set_config(WIFI_IF_STA, &wifi_config); // Apply config to STA interface
  printf("Wi-Fi configuration set: SSID=%s, Password=%s\n",
         wifi_config.sta.ssid, wifi_config.sta.password);
  esp_wifi_start(); // Start Wi-Fi driver
}

char *extract_value(const char *msg, const char *key)
{
  // Build search pattern: "\"key\":"
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);

  const char *start = strstr(msg, pattern);
  if (!start)
    return NULL;

  start += strlen(pattern);

  // Skip whitespace and opening quote if present
  while (*start == ' ' || *start == '\"')
    start++;

  const char *end = start;
  // Value ends at quote, comma, or closing brace
  while (*end && *end != '\"' && *end != ',' && *end != '}')
    end++;

  int len = end - start;
  if (len <= 0)
    return NULL;

  char *value = (char *)malloc(len + 1);
  if (!value)
    return NULL;

  strncpy(value, start, len);
  value[len] = '\0';
  return value;
}

// LED Control Functions
static void blink_led(void)
{
  /* Set the GPIO level according to the state (LOW or HIGH)*/
  gpio_set_level(BLINK_GPIO, s_led_state);
  ESP_LOGI(TAG, "LED %s", s_led_state ? "ON" : "OFF");
}

static void configure_led(void)
{
  ESP_LOGI(TAG, "Configuring GPIO LED on pin %d", BLINK_GPIO);
  gpio_reset_pin(BLINK_GPIO);
  /* Set the GPIO as a push/pull output */
  gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
  /* Turn off LED initially */
  gpio_set_level(BLINK_GPIO, 0);
}

// Task to handle LED blinking
static void led_blink_task(void *pvParameter)
{
  int blink_count = *((int *)pvParameter);
  ESP_LOGI(TAG, "Starting LED blink sequence with %d blinks", blink_count);

  for (int i = 0; i < blink_count; i++)
  {
    s_led_state = 1;
    blink_led();
    vTaskDelay(500 / portTICK_PERIOD_MS); // On for 500ms

    s_led_state = 0;
    blink_led();
    vTaskDelay(500 / portTICK_PERIOD_MS); // Off for 500ms
  }

  ESP_LOGI(TAG, "LED blink sequence completed");
  vTaskDelete(NULL); // Delete this task
}

void app_main(void)
{
  wifi_init_sta();
  printf("Waiting for Wi-Fi connection...\n");
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
  printf("✅ Wi-Fi connected successfully!\n");

  // Configure LED
  configure_led();
  ESP_LOGI(TAG, "LED configured successfully!");

  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); // Create IPv4 UDP socket
  int broadcast = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
  {
    perror("setsockopt failed");
    return;
  }

  // bind to a port
  struct sockaddr_in listen_addr;
  listen_addr.sin_family = AF_INET;                // IPv4
  listen_addr.sin_port = htons(12345);             // Host-to-network byte order for port
  listen_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on any local IP

  // bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)); // Bind socket to port
  if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0)
  {
    perror("bind failed");
    return;
  }
  else
  {
    printf("Bind succuessfully\n");
  }
  while (true)
  {

    // receive UDP broadcast
    char rx_buffer[256];
    struct sockaddr_in source_addr; // Will hold sender's info
    socklen_t socklen = sizeof(source_addr);
    printf("Start waiting for UDP broadcast...\n");

    int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                       (struct sockaddr *)&source_addr, &socklen); // Blocking receive
    printf("len = %d\n", len);
    if (len > 0)
    {
      rx_buffer[len] = 0; // Null-terminate string for safety
      printf("Received: %s\n", rx_buffer);
    }
    char *target = extract_value(rx_buffer, "target");

    // instead of using ip for target, we should use some arbitrary string decided during flash
    if (target == NULL || (strcmp(target, "all") != 0 && strcmp(target, "ESP32_A") != 0))
    {
      printf("Target invalid. Skipping message.\n");
      if (target)
        free(target);
      continue; // Skip if target is not 'all'
    }
    // send ACK
    char *ack_ip = extract_value(rx_buffer, "ack_ip");
    if (ack_ip == NULL)
    {
      printf("ACK IP not found in received message. Skipping ACK.\n");
      if (target)
        free(target);
      continue;
    }
    char *cmd = extract_value(rx_buffer, "cmd");
    if (cmd == NULL)
    {
      printf("cmd not found in received message. Skipping ACK.\n");
      if (target)
        free(target);
      if (ack_ip)
        free(ack_ip);
      continue;
    }

    // Handle blink command
    if (strcmp(cmd, "blink") == 0)
    {
      ESP_LOGI(TAG, "Received blink command, starting LED blink sequence");
      static int blink_count = 3; // Blink 3 times
      // Create a task to handle blinking so it doesn't block UDP reception
      xTaskCreate(led_blink_task, "led_blink", 2048, &blink_count, 5, NULL);
    }
    else if (strcmp(cmd, "start") == 0)
    {
      ESP_LOGI(TAG, "Received start command");
      // Handle start command here if needed
    }
    else
    {
      ESP_LOGI(TAG, "Received unknown command: %s", cmd);
    }
    printf("ACK IP: %s\n", ack_ip);
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(3333); // Server ACK port
    if (inet_pton(AF_INET, ack_ip, &dest_addr.sin_addr) != 1)
    {
      printf("Invalid ACK IP address format. Skipping ACK.\n");
      if (target)
        free(target);
      if (ack_ip)
        free(ack_ip);
      if (cmd)
        free(cmd);
      continue;
    }

    char *ack_msg = "{ \"id\": \"ESP32_A\", \"status\": \"ack\" }";
    sendto(sock, ack_msg, strlen(ack_msg), 0,
           (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    // Free allocated memory
    if (target)
      free(target);
    if (ack_ip)
      free(ack_ip);
    if (cmd)
      free(cmd);
  }
}