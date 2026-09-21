#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int i2c_port_t;
typedef uint32_t TickType_t;
typedef uint32_t EventBits_t;
typedef void *QueueHandle_t;
typedef void *EventGroupHandle_t;
typedef void *esp_http_client_handle_t;

#define I2C_NUM_0 0
#define portTICK_PERIOD_MS 10
#define BIT0 (1U << 0)
#define BIT1 (1U << 1)
#define BIT2 (1U << 2)
#define BIT3 (1U << 3)
#define BIT4 (1U << 4)
#define BIT5 (1U << 5)
#define BIT6 (1U << 6)
#define BIT7 (1U << 7)
#define BIT8 (1U << 8)
#define BIT9 (1U << 9)
#define BIT10 (1U << 10)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOG_BUFFER_HEX(...) ((void)0)
#define taskYIELD() ((void)0)

void vTaskDelay(TickType_t ticks);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
int64_t esp_timer_get_time(void);
int mbedtls_base64_encode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen);
int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen);
