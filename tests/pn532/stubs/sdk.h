#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
typedef int i2c_port_t;
typedef uint32_t TickType_t;
typedef void *QueueHandle_t;
typedef struct mock_i2c_command *i2c_cmd_handle_t;

typedef struct {
  int intr_type;
  int mode;
  uint64_t pin_bit_mask;
  int pull_down_en;
  int pull_up_en;
} gpio_config_t;

typedef struct {
  int mode;
  int sda_io_num;
  int sda_pullup_en;
  int scl_io_num;
  int scl_pullup_en;
  struct { int clk_speed; } master;
  int clk_flags;
} i2c_config_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_INTR_FLAG_EDGE 0
#define ESP_LOG_VERBOSE 0
#define ESP_LOG_ERROR 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_NEGEDGE 1
#define GPIO_MODE_OUTPUT 1
#define GPIO_MODE_INPUT 0
#define GPIO_PULLUP_DISABLE 0
#define I2C_MODE_MASTER 1
#define I2C_MASTER_ACK 0
#define I2C_MASTER_LAST_NACK 1
#define portTICK_PERIOD_MS 10
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms) / portTICK_PERIOD_MS)
#define IRAM_ATTR
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expression) ((void)(expression))
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOG_LEVEL(...) ((void)0)
#define ESP_LOG_BUFFER_HEX(...) ((void)0)
#define ESP_LOG_BUFFER_HEX_LEVEL(...) ((void)0)

i2c_cmd_handle_t i2c_cmd_link_create(void);
void i2c_cmd_link_delete(i2c_cmd_handle_t command);
esp_err_t i2c_master_start(i2c_cmd_handle_t command);
esp_err_t i2c_master_stop(i2c_cmd_handle_t command);
esp_err_t i2c_master_write_byte(i2c_cmd_handle_t command, uint8_t byte, bool ack);
esp_err_t i2c_master_read_byte(i2c_cmd_handle_t command, uint8_t *byte, int ack);
esp_err_t i2c_master_write(i2c_cmd_handle_t command, const uint8_t *bytes, size_t size, bool ack);
esp_err_t i2c_master_read(i2c_cmd_handle_t command, uint8_t *bytes, size_t size, int ack);
esp_err_t i2c_master_cmd_begin(i2c_port_t port, i2c_cmd_handle_t command, TickType_t timeout);
esp_err_t i2c_param_config(i2c_port_t port, const i2c_config_t *config);
esp_err_t i2c_driver_install(i2c_port_t port, int mode, int rx, int tx, int flags);
esp_err_t i2c_set_timeout(i2c_port_t port, int timeout);
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(int pin, int level);
int gpio_get_level(int pin);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(int pin, void (*handler)(void *), void *argument);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);
QueueHandle_t xQueueCreate(unsigned size, unsigned item_size);
void vQueueDelete(QueueHandle_t queue);
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout);
int xQueueSendFromISR(QueueHandle_t queue, const void *item, void *woken);
int xQueueReset(QueueHandle_t queue);
const char *esp_err_to_name(esp_err_t error);
