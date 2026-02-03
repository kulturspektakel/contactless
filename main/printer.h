#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "logmessage.pb.h"

typedef enum {
  PRINTER_DISCONNECTED,
  PRINTER_SCANNING,
  PRINTER_CONNECTING,
  PRINTER_CONNECTED
} printer_status_t;

typedef struct {
  LogMessage_Order_CartItem items[9];
  int item_count;
  LogMessage_Order_PaymentMethod payment_method;
  int total;
} PrintJob;

extern printer_status_t printer_status;
extern QueueHandle_t print_queue;

void printer(void* params);
void init_print_queue(void);
void submit_print_job(LogMessage_Order_PaymentMethod payment);
void printer_start_scan(int retries);
