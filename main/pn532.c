/**************************************************************************/
/*!
 @file     PN532.c
 @author   Luca Faccin
 @license  BSD (see license.txt)

 This is a port of the Adafruit PN532 Driver for the ESP32 using only the I2C Bus
 Driver for NXP's PN532 NFC/13.56MHz RFID Transceiver

 @section  HISTORY
 v 1.0		Basic port of the v 2.1 of the Adafruit PN532 Driver
 */
/**************************************************************************/

#include "pn532.h"
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#define TAG "PN532"

uint8_t pn532ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
uint8_t pn532response_firmwarevers[] = {0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03};
uint8_t SDA_PIN, SCL_PIN, RESET_PIN, IRQ_PIN;
i2c_port_t PN532_I2C_PORT;
uint8_t _uid[7];       // ISO14443A uid
uint8_t _uidLen;       // uid len
uint8_t _key[6];       // Mifare Classic key
uint8_t _inListedTag;  // Tg number of inlisted tag.

// IRQ Event handler
#define ESP_INTR_FLAG_DEFAULT 0
static QueueHandle_t IRQQueue = NULL;

// Uncomment these lines to enable debug output for PN532(SPI) and/or MIFARE related code
#define PN532_LOG_LEVEL ESP_LOG_VERBOSE
#define MIFARE_LEVEL ESP_LOG_VERBOSE
#define CONFIG_ENABLE_IRQ_ISR

#define PN532_PACKBUFFSIZ 64
uint8_t pn532_packetbuffer[PN532_PACKBUFFSIZ];
uint8_t ACK_PACKET[] = {0x0, 0x0, 0xFF, 0x0, 0xFF, 0x0};
uint8_t NACK_PACKET[] = {0x0, 0x0, 0xFF, 0xFF, 0x0, 0x0};
static bool pn532_needs_resync = false;

#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

// Def only
bool pn532_sam_configuration(void);

/**
 * Send the reset signal to PN532
 */
static void resetPN532() {
  pn532_needs_resync = false;
  gpio_set_level(RESET_PIN, 1);
  gpio_set_level(RESET_PIN, 0);
  vTaskDelay(400 / portTICK_PERIOD_MS);
  gpio_set_level(RESET_PIN, 1);
  vTaskDelay(
      10 / portTICK_PERIOD_MS
  );  // Small delay required before taking other actions after reset.
  //	 See timing diagram on page 209 of the datasheet, section 12.23.
}

/**************************************************************************/
/*!
 @brief  Writes a command to the PN532, automatically inserting the
 preamble and required frame details (checksum, len, etc.)

 @param  cmd       Pointer to the command buffer
 @param  cmdlen    Command length in bytes
 */
/**************************************************************************/
static bool pn532_write_frame(const uint8_t* frame, size_t length) {
  i2c_cmd_handle_t command = i2c_cmd_link_create();
  if (command == NULL) {
    return false;
  }
  bool ok = i2c_master_start(command) == ESP_OK &&
            i2c_master_write_byte(command, PN532_I2C_ADDRESS, true) == ESP_OK;
  for (size_t i = 0; ok && i < length; i++) {
    ok = i2c_master_write_byte(command, frame[i], true) == ESP_OK;
  }
  ok = ok && i2c_master_stop(command) == ESP_OK;
  if (ok) {
    ok = i2c_master_cmd_begin(
             PN532_I2C_PORT, command, pdMS_TO_TICKS(I2C_WRITE_TIMEOUT)
         ) == ESP_OK;
  }
  i2c_cmd_link_delete(command);
  return ok;
}

static bool writecommand(const uint8_t* cmd, uint8_t cmdlen) {
  if (cmdlen == 0 || cmdlen > PN532_PACKBUFFSIZ) {
    return false;
  }
  uint8_t frame[PN532_PACKBUFFSIZ + 8] = {0x00, 0x00, 0xFF};
  frame[3] = cmdlen + 1;
  frame[4] = (uint8_t)-frame[3];
  frame[5] = PN532_HOSTTOPN532;
  uint8_t checksum = PN532_HOSTTOPN532;
  for (size_t i = 0; i < cmdlen; i++) {
    frame[6 + i] = cmd[i];
    checksum += cmd[i];
  }
  frame[6 + cmdlen] = (uint8_t)-checksum;
  frame[7 + cmdlen] = PN532_POSTAMBLE;

  vTaskDelay(pdMS_TO_TICKS(10));
  return pn532_write_frame(frame, cmdlen + 8);
}

/**************************************************************************/
/*!
 @brief  Receive the interrupt generated from the IRQ PIN

 @param  ARG      						arguments for interrupt

 */
/**************************************************************************/
static void IRAM_ATTR IRQHandler(void* arg) {
  uint32_t gpio_num = (uint32_t)arg;
  xQueueSendFromISR(IRQQueue, &gpio_num, NULL);
}
/**************************************************************************/
/*!
 @brief  Setups the HW and the I2C Bus

 @param  sda      						GPIO PIN for the SDA signal
 @param  scl      						GPIO PIN for the SCL signal
 @param  reset     						GPIO PIN for the reset signal
 @param  irq      						GPIO PIN for the IRQ signal
 @param  i2c_port_number      I2C Port number

 @return true if hw setup OK, false otherwise
 */
/**************************************************************************/
bool pn532_init(uint8_t sda, uint8_t scl, uint8_t reset, uint8_t irq, i2c_port_t i2c_port_number) {
  SCL_PIN = scl;
  SDA_PIN = sda;
  RESET_PIN = reset;
  IRQ_PIN = irq;
  PN532_I2C_PORT = i2c_port_number;

  uint64_t pintBitMask = ((1ULL) << RESET_PIN);

  // initialize the PIN
  // Lets configure GPIO PIN for Reset
  gpio_config_t io_conf;
  // disable interrupt
  io_conf.intr_type = GPIO_INTR_DISABLE;
  // set as output mode
  io_conf.mode = GPIO_MODE_OUTPUT;
  // bit mask of the pins that you want to set,e.g.GPIO18/19
  io_conf.pin_bit_mask = pintBitMask;
  // disable pull-down mode
  io_conf.pull_down_en = 0;
  // enable pull-up mode
  io_conf.pull_up_en = 1;
  // configure GPIO with the given settings
  if (gpio_config(&io_conf) != ESP_OK) {
    return false;
  }

  pintBitMask = ((1ULL) << IRQ_PIN);
  // Lets configure GPIO PIN for IRQ
  // disable interrupt
#ifdef CONFIG_ENABLE_IRQ_ISR
  io_conf.intr_type = GPIO_INTR_NEGEDGE;
#else
  io_conf.intr_type = GPIO_INTR_DISABLE;
#endif

  // set as output mode
  io_conf.mode = GPIO_MODE_INPUT;
  // bit mask of the pins that you want to set,e.g.GPIO18/19
  io_conf.pin_bit_mask = pintBitMask;
  // disable pull-down mode
  io_conf.pull_down_en = 0;
  // enable pull-up mode
  io_conf.pull_up_en = 1;
  // configure GPIO with the given settings
  if (gpio_config(&io_conf) != ESP_OK) {
    return false;
  }

  // Reset the PN532
  resetPN532();

#ifdef CONFIG_ENABLE_IRQ_ISR
  if (IRQQueue != NULL) {
    vQueueDelete(IRQQueue);
  }
  // create a queue to handle gpio event from isr
  IRQQueue = xQueueCreate(1, sizeof(uint32_t));

  // Start the IRQ Service
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_install_isr_service(ESP_INTR_FLAG_EDGE));
  // hook isr handler for specific gpio pin
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_add(IRQ_PIN, IRQHandler, (void*)IRQ_PIN));
#endif
  i2c_config_t conf;
  // Open the I2C Bus
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = SDA_PIN;
  conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
  conf.scl_io_num = SCL_PIN;
  conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
  conf.master.clk_speed = 100000;
  conf.clk_flags = 0;

  if (i2c_param_config(PN532_I2C_PORT, &conf) != ESP_OK) {
    return false;
  }

  ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_driver_install(PN532_I2C_PORT, conf.mode, 0, 0, 0));

  // Needed due to long wake up procedure on the first command on i2c bus. May be decreased
  if (i2c_set_timeout(PN532_I2C_PORT, 0x0000001FU) != ESP_OK) {
    return false;
  }

  return true;
}

/**************************************************************************/
/*!
 @brief  Reads n bytes of data from the PN532 via SPI or I2C.

 @param  buff      Pointer to the buffer where data will be written
 @param  n         Number of bytes to be read
 @return true if read success, false otherwise
 */
/**************************************************************************/
bool pn532_read_data(uint8_t* buff, uint8_t n) {
  if (buff == NULL || n == 0 || n > PN532_PACKBUFFSIZ) {
    return false;
  }
  uint8_t buffer[PN532_PACKBUFFSIZ + 1];
  i2c_cmd_handle_t i2ccmd = i2c_cmd_link_create();
  if (i2ccmd == NULL) {
    return false;
  }
  bool ok = i2c_master_start(i2ccmd) == ESP_OK &&
            i2c_master_write_byte(i2ccmd, PN532_I2C_READ_ADDRESS, true) == ESP_OK;
  for (size_t i = 0; ok && i <= n; i++) {
    ok = i2c_master_read_byte(
             i2ccmd, &buffer[i], i == n ? I2C_MASTER_LAST_NACK : I2C_MASTER_ACK
         ) == ESP_OK;
  }
  ok = ok && i2c_master_stop(i2ccmd) == ESP_OK;
  if (ok) {
    ok = i2c_master_cmd_begin(
             PN532_I2C_PORT, i2ccmd, pdMS_TO_TICKS(I2C_READ_TIMEOUT)
         ) == ESP_OK;
  }
  i2c_cmd_link_delete(i2ccmd);
  if (!ok || buffer[0] != PN532_I2C_READY) {
    return false;
  }
  memcpy(buff, buffer + 1, n);
  return true;
}

/************** high level communication functions (handles both I2C and SPI) */

/**************************************************************************/
/*!
 @brief  Tries to read the SPI or I2C ACK signal
 @return true if ACK received, false otherwise
 */
/**************************************************************************/
bool readack() {
  uint8_t ackbuff[6];
  return pn532_read_data(ackbuff, sizeof(ackbuff)) &&
         memcmp(ackbuff, pn532ack, sizeof(ackbuff)) == 0;
}

/**************************************************************************/
/*!
 @brief  Return true if the PN532 is ready with a response.
 @return true if IRQ signal LOW
 */
/**************************************************************************/
bool isready() {
  // I2C check if status is ready by IRQ line being pulled low.
  uint8_t x = gpio_get_level(IRQ_PIN);
  ESP_LOG_LEVEL(PN532_LOG_LEVEL, TAG, "IRQ: %d", x);
  return (x == 0);
}

/**************************************************************************/
/*!
 @brief  Waits until the PN532 is ready.

 @param  timeout   Timeout before giving up in milliseconds. IF TIMEOUT 0 WILL WAIT UNDEFINITELY.
 @return true if PN532 is ready before timeout, false otherwise
 */
/**************************************************************************/
bool waitready(uint16_t timeout) {
  TickType_t started = xTaskGetTickCount();
  TickType_t budget = pdMS_TO_TICKS(timeout);
  if (budget == 0) {
    budget = 1;
  }
  while (!isready()) {
    TickType_t delay = pdMS_TO_TICKS(100);
    if (delay == 0) {
      delay = 1;
    }
    if (timeout != 0) {
      TickType_t elapsed = xTaskGetTickCount() - started;
      if (elapsed >= budget) {
        return false;
      }
      if (delay > budget - elapsed) {
        delay = budget - elapsed;
      }
    }
    // Edges are wake-up hints, not proof that the current response is ready.
    // The bounded wait also recovers when an edge was missed or coalesced.
#ifdef CONFIG_ENABLE_IRQ_ISR
    uint32_t io_num;
    if (IRQQueue != NULL) {
      xQueueReceive(IRQQueue, &io_num, delay);
    } else
#endif
    {
      vTaskDelay(delay);
    }
  }
  return true;
}

static int pn532_read_response(uint8_t command, uint16_t timeout, uint8_t* frame) {
  // A STOP discards unread frame bytes: read the bounded frame in one transaction.
  if (!waitready(timeout) || !pn532_read_data(frame, PN532_PACKBUFFSIZ)) {
    pn532_needs_resync = true;
    return -1;
  }
  uint8_t length = frame[3];
  if (frame[0] != 0 || frame[1] != 0 || frame[2] != 0xFF || length < 2 ||
      length > PN532_PACKBUFFSIZ - 7 || (uint8_t)(length + frame[4]) != 0 ||
      frame[5] != PN532_PN532TOHOST || frame[6] != (uint8_t)(command + 1) ||
      frame[length + 6] != PN532_POSTAMBLE) {
    pn532_needs_resync = true;
    return -1;
  }
  uint8_t checksum = 0;
  for (size_t i = 5; i <= length + 5; i++) {
    checksum += frame[i];
  }
  if (checksum != 0) {
    pn532_needs_resync = true;
    return -1;
  }
  return length - 2;
}

static bool pn532_resynchronize(void) {
  // Abort is a raw host ACK, not an ordinary command. The original operation
  // may already have changed the card. Never replay it here.
  uint8_t command = PN532_COMMAND_GETFIRMWAREVERSION;
  uint8_t response[PN532_PACKBUFFSIZ];
  if (!pn532_write_frame(pn532ack, sizeof(pn532ack)) || !writecommand(&command, 1) ||
      !waitready(I2C_WRITE_TIMEOUT) || !readack() ||
      pn532_read_response(command, I2C_READ_TIMEOUT, response) != 4) {
    return false;
  }
  // A complete harmless exchange is the synchronization barrier; IRQ going
  // high alone only indicates that the I2C address was acknowledged.
  pn532_needs_resync = false;
  return true;
}

/**************************************************************************/
/*!
 @brief  Sends a command and waits a specified period for the ACK

 @param  cmd       Pointer to the command buffer
 @param  cmdlen    The size of the command in bytes
 @param  timeout   timeout before giving up

 @returns  true if everything is OK, 0 if timeout occured before an
 ACK was recieved
 */
/**************************************************************************/
// default timeout of one second
bool pn532_send_cmd_check_ack(uint8_t* cmd, uint8_t cmdlen, uint16_t timeout) {
  if (pn532_needs_resync && !pn532_resynchronize()) {
    ESP_LOGE(TAG, "PN532 resynchronization failed");
    return false;
  }
  if (!writecommand(cmd, cmdlen) || !waitready(timeout) || !readack()) {
    pn532_needs_resync = true;
    ESP_LOGE(TAG, "PN532 command acknowledgement failed; outcome unknown");
    return false;
  }
  return true;  // Command received, not operation completed.
}

// Only the RFID task owns the driver. Keep its command, ACK and response in
// one synchronous exchange before another operation can use the shared buffer.
static int pn532_exchange(uint8_t cmdlen, uint16_t timeout) {
  uint8_t command = pn532_packetbuffer[0];
  if (!pn532_send_cmd_check_ack(pn532_packetbuffer, cmdlen, I2C_WRITE_TIMEOUT)) {
    return -1;
  }
  return pn532_read_response(command, timeout, pn532_packetbuffer);
}

/**************************************************************************/
/*!
 @brief  Checks the firmware version of the PN5xx chip

 @returns  The chip's firmware version and ID
 */
/**************************************************************************/
uint32_t pn532_get_firmware_version(void) {
  pn532_packetbuffer[0] = PN532_COMMAND_GETFIRMWAREVERSION;
  if (pn532_exchange(1, I2C_READ_TIMEOUT) != 4) {
    return 0;
  }
  return ((uint32_t)pn532_packetbuffer[7] << 24) |
         ((uint32_t)pn532_packetbuffer[8] << 16) |
         ((uint32_t)pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
}

/**************************************************************************/
/*!
 Writes an 8-bit value that sets the state of the PN532's GPIO pins

 @warning This function is provided exclusively for board testing and
 is dangerous since it will throw an error if any pin other
 than the ones marked "Can be used as GPIO" are modified!  All
 pins that can not be used as GPIO should ALWAYS be left high
 (value = 1) or the system will become unstable and a HW reset
 will be required to recover the PN532.

 pinState[0]  = P30     Can be used as GPIO
 pinState[1]  = P31     Can be used as GPIO
 pinState[2]  = P32     *** RESERVED (Must be 1!) ***
 pinState[3]  = P33     Can be used as GPIO
 pinState[4]  = P34     *** RESERVED (Must be 1!) ***
 pinState[5]  = P35     Can be used as GPIO

 @returns 1 if everything executed properly, 0 for an error
 */
/**************************************************************************/
bool pn532_write_gpio(uint8_t pinstate) {
  // Reserved pins must remain high.
  pinstate |= (1 << PN532_GPIO_P32) | (1 << PN532_GPIO_P34);
  pn532_packetbuffer[0] = PN532_COMMAND_WRITEGPIO;
  pn532_packetbuffer[1] = PN532_GPIO_VALIDATIONBIT | pinstate;
  pn532_packetbuffer[2] = 0x00;
  return pn532_exchange(3, I2C_READ_TIMEOUT) == 0;
}

/**************************************************************************/
/*!
 Reads the state of the PN532's GPIO pins

 @returns An 8-bit value containing the pin state where:

 pinState[0]  = P30
 pinState[1]  = P31
 pinState[2]  = P32
 pinState[3]  = P33
 pinState[4]  = P34
 pinState[5]  = P35
 */
/**************************************************************************/
uint8_t pn532_read_gpio(void) {
  pn532_packetbuffer[0] = PN532_COMMAND_READGPIO;
  if (pn532_exchange(1, I2C_READ_TIMEOUT) != 3) {
    return 0;
  }
  return pn532_packetbuffer[7];
}

/**************************************************************************/
/*!
 @brief  Configures the SAM (Secure Access Module)
 */
/**************************************************************************/
bool pn532_sam_configuration(void) {
  pn532_packetbuffer[0] = PN532_COMMAND_pn532_sam_configurationURATION;
  pn532_packetbuffer[1] = 0x01;  // normal mode
  pn532_packetbuffer[2] = 0x14;  // timeout 50ms * 20 = 1 second
  pn532_packetbuffer[3] = 0x01;  // use IRQ pin
  return pn532_exchange(4, I2C_READ_TIMEOUT) == 0;
}

/**************************************************************************/
/*!
 Sets the MxRtyPassiveActivation byte of the RFConfiguration register

 @param  max_retries    0xFF to wait forever, 0x00..0xFE to timeout
 after mxRetries

 @returns 1 if everything executed properly, 0 for an error
 */
/**************************************************************************/
bool pn532_set_passive_activation_retries(uint8_t max_retries) {
  pn532_packetbuffer[0] = PN532_COMMAND_RFCONFIGURATION;
  pn532_packetbuffer[1] = 5;     // Config item 5 (MaxRetries)
  pn532_packetbuffer[2] = 0xFF;  // MxRtyATR
  pn532_packetbuffer[3] = 0x01;  // MxRtyPSL
  pn532_packetbuffer[4] = max_retries;
  return pn532_exchange(5, I2C_READ_TIMEOUT) == 0;
}

/***** ISO14443A Commands ******/

/**************************************************************************/
/*!
 Waits for an ISO14443A target to enter the field

 @param  cardBaudRate  Baud rate of the card
 @param  uid           Pointer to the array that will be populated
 with the card's UID (up to 7 bytes)
 @param  uid_len     Pointer to the variable that will hold the
 length of the card's UID.

 @returns 1 if everything executed properly, 0 for an error
 */
/**************************************************************************/
bool iso14443a_read_passive_target_id(
    uint8_t cardbaudrate,
    uint8_t* uid,
    uint8_t* uid_len,
    uint16_t timeout
) {
  pn532_packetbuffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
  pn532_packetbuffer[1] = 1;  // At most one card.
  pn532_packetbuffer[2] = cardbaudrate;
  int length = pn532_exchange(3, timeout);
  if (length < 6 || pn532_packetbuffer[7] != 1) {
    return false;
  }
  uint8_t length_id = pn532_packetbuffer[12];
  if ((length_id != 4 && length_id != 7) || length < 6 + length_id) {
    return false;
  }
  memcpy(uid, pn532_packetbuffer + 13, length_id);
  *uid_len = length_id;
  _inListedTag = pn532_packetbuffer[8];
  return true;
}

/**************************************************************************/
/*!
 @brief  Exchanges an APDU with the currently inlisted peer

 @param  send            Pointer to data to send
 @param  send_len      Length of the data to send
 @param  response        Pointer to response data
 @param  response_len  Pointer to the response data length
 */
/**************************************************************************/
bool iso14443a_in_data_exchange(
    uint8_t* send,
    uint8_t send_len,
    uint8_t* response,
    uint8_t* response_len
) {
  if (send_len > PN532_PACKBUFFSIZ - 2) {
    return false;
  }
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = _inListedTag;
  memcpy(pn532_packetbuffer + 2, send, send_len);
  int length = pn532_exchange(send_len + 2, I2C_READ_TIMEOUT);
  if (length < 1 || pn532_packetbuffer[7] != 0 || length - 1 > *response_len) {
    return false;
  }
  memcpy(response, pn532_packetbuffer + 8, length - 1);
  *response_len = length - 1;
  return true;
}

bool iso14443a_in_deselect() {
  pn532_packetbuffer[0] = PN532_COMMAND_INDESELECT;
  pn532_packetbuffer[1] = 0x00;  // All targets.
  return pn532_exchange(2, I2C_READ_TIMEOUT) == 1 && pn532_packetbuffer[7] == 0;
}

bool iso14443a_in_auto_poll(uint8_t period) {
  pn532_packetbuffer[0] = PN532_COMMAND_INAUTOPOLL;
  pn532_packetbuffer[1] = 0x01;    // PollNr
  pn532_packetbuffer[2] = period;  // Period
  pn532_packetbuffer[3] = 0x00;    // Type A
  int length = pn532_exchange(4, 30000);
  if (length < 1 || pn532_packetbuffer[7] == 0 || pn532_packetbuffer[7] > 2) {
    return false;
  }
  int offset = 8;
  int end = 7 + length;
  for (uint8_t i = 0; i < pn532_packetbuffer[7]; i++) {
    // Each target has a type byte, a length byte, then that many data bytes.
    if (offset + 2 > end || pn532_packetbuffer[offset + 1] == 0) {
      return false;
    }
    offset += 2 + pn532_packetbuffer[offset + 1];
    if (offset > end) {
      return false;
    }
  }
  return offset == end;
}

/**************************************************************************/
/*!
 @brief  'InLists' a passive target. PN532 acting as reader/initiator,
 peer acting as card/responder.
 */
/**************************************************************************/
bool iso14443a_in_list_passive_targers() {
  uint8_t uid[7];
  uint8_t uid_len;
  return iso14443a_read_passive_target_id(PN532_MIFARE_ISO14443A, uid, &uid_len, 30000);
}

/***** Mifare Classic Functions ******/

/**************************************************************************/
/*!
 Indicates whether the specified block number is the first block
 in the sector (block 0 relative to the current sector)
 */
/**************************************************************************/
bool mfc_is_first_block(uint32_t uiBlock) {
  // Test if we are in the small or big sectors
  if (uiBlock < 128) {
    return ((uiBlock) % 4 == 0);
  } else {
    return ((uiBlock) % 16 == 0);
  }
}

/**************************************************************************/
/*!
 Indicates whether the specified block number is the sector trailer
 */
/**************************************************************************/
bool mfc_is_trailer_block(uint32_t uiBlock) {
  // Test if we are in the small or big sectors
  if (uiBlock < 128) {
    return ((uiBlock + 1) % 4 == 0);
  } else {
    return ((uiBlock + 1) % 16 == 0);
  }
}

/**************************************************************************/
/*!
 Tries to authenticate a block of memory on a MIFARE card using the
 INDATAEXCHANGE command.  See section 7.3.8 of the PN532 User Manual
 for more information on sending MIFARE and other commands.

 @param  uid           Pointer to a byte array containing the card UID
 @param  uidLen        The length (in bytes) of the card's UID (Should
 be 4 for MIFARE Classic)
 @param  block_num   The block number to authenticate.  (0..63 for
 1KB cards, and 0..255 for 4KB cards).
 @param  key_num     Which key type to use during authentication
 (0 = MIFARE_CMD_AUTH_A, 1 = MIFARE_CMD_AUTH_B)
 @param  key_data       Pointer to a byte array containing the 6 byte
 key value

 @returns 1 if everything executed properly, 0 for an error
 */
/**************************************************************************/
bool mfc_authenticate_block(
    uint8_t* uid,
    uint8_t uidLen,
    uint32_t block_num,
    uint8_t key_num,
    uint8_t* key_data
) {
  uint8_t i;

  // Hang on to the key and uid data
  if (uidLen > sizeof(_uid)) {
    return false;
  }
  memcpy(_key, key_data, 6);
  memcpy(_uid, uid, uidLen);
  _uidLen = uidLen;

  ESP_LOG_LEVEL(MIFARE_LEVEL, TAG, "Trying to authenticate card ");
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, _uid, _uidLen, MIFARE_LEVEL);
  ESP_LOG_LEVEL(MIFARE_LEVEL, TAG, "Using authentication KEY %c :", key_num ? 'B' : 'A');
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, _key, 6, MIFARE_LEVEL);

  // Prepare the authentication command //
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE; /* Data Exchange Header */
  pn532_packetbuffer[1] = 1;                            /* Max card numbers */
  pn532_packetbuffer[2] = (key_num) ? MIFARE_CMD_AUTH_B : MIFARE_CMD_AUTH_A;
  pn532_packetbuffer[3] = block_num; /* Block Number (1K = 0..63, 4K = 0..255 */
  memcpy(pn532_packetbuffer + 4, _key, 6);
  for (i = 0; i < _uidLen; i++) {
    pn532_packetbuffer[10 + i] = _uid[i]; /* 4 byte card ID */
  }

  if (pn532_exchange(10 + _uidLen, I2C_READ_TIMEOUT) != 1 || pn532_packetbuffer[7] != 0) {
    ESP_LOGE(TAG, "Authentification failed: ");
    ESP_LOG_BUFFER_HEX(TAG, pn532_packetbuffer, 12);
    return false;
  }

  return true;
}

/**************************************************************************/
/*!
 Tries to read an entire 16-byte data block at the specified block
 address.

 @param  block_num   The block number to authenticate.  (0..63 for
 1KB cards, and 0..255 for 4KB cards).
 @param  data          Pointer to the byte array that will hold the
 retrieved data (if any)

 @returns 1 if everything executed properly, 0 for an error
 */
/**************************************************************************/
bool mfc_read_data_block(uint8_t block_num, uint8_t* data) {
  ESP_LOG_LEVEL(MIFARE_LEVEL, TAG, "Trying to read 16 bytes from block %d", block_num);

  /* Prepare the command */
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = 1;               /* Card number */
  pn532_packetbuffer[2] = MIFARE_CMD_READ; /* Mifare Read command = 0x30 */
  pn532_packetbuffer[3] = block_num;       /* Block Number (0..63 for 1K, 0..255 for 4K) */

  /* Send the command */
  if (pn532_exchange(4, I2C_READ_TIMEOUT) != 17 || pn532_packetbuffer[7] != 0) {
    ESP_LOGE(TAG, "Unexpected response");
    ESP_LOG_BUFFER_HEX(TAG, pn532_packetbuffer, 26);
    return false;
  }

  /* Copy the 16 data bytes to the output buffer        */
  /* Block content starts at byte 9 of a valid response */
  memcpy(data, pn532_packetbuffer + 8, 16);

  /* Display data for debug if requested */
  ESP_LOG_LEVEL(MIFARE_LEVEL, TAG, "Block %d", block_num);
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, 16, MIFARE_LEVEL);

  return true;
}

/**************************************************************************/
/*!
 Tries to write an entire 16-byte data block at the specified block
 address.

 @param  block_num   The block number to authenticate.  (0..63 for
 1KB cards, and 0..255 for 4KB cards).
 @param  data          The byte array that contains the data to write.

 @returns 1 if everything executed properly, 0 for an error
 */
/**************************************************************************/
bool mfc_write_data_block(uint8_t block_num, uint8_t* data) {
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = 1;
  pn532_packetbuffer[2] = MIFARE_CMD_WRITE;
  pn532_packetbuffer[3] = block_num;
  memcpy(pn532_packetbuffer + 4, data, 16);
  return pn532_exchange(20, I2C_READ_TIMEOUT) == 1 && pn532_packetbuffer[7] == 0;
}

/**************************************************************************/
/*!
 Formats a Mifare Classic card to store NDEF Records

 @returns 1 if everything executed properly, 0 for an error
 */
/**************************************************************************/
bool mfc_format_ndef(void) {
  uint8_t sectorbuffer1[16] = {
      0x14, 0x01, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1
  };
  uint8_t sectorbuffer2[16] = {
      0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1
  };
  uint8_t sectorbuffer3[16] = {
      0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0x78, 0x77, 0x88, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };

  // Note 0xA0 0xA1 0xA2 0xA3 0xA4 0xA5 must be used for key A
  // for the MAD sector in NDEF records (sector 0)

  // Write block 1 and 2 to the card
  if (!(mfc_write_data_block(1, sectorbuffer1))) {
    return false;
  }
  if (!(mfc_write_data_block(2, sectorbuffer2))) {
    return false;
  }
  // Write key A and access rights card
  if (!(mfc_write_data_block(3, sectorbuffer3))) {
    return false;
  }

  // Seems that everything was OK (?!)
  return true;
}

/**************************************************************************/
/*!
 Writes an NDEF URI Record to the specified sector (1..15)

 Note that this function assumes that the Mifare Classic card is
 already formatted to work as an "NFC Forum Tag" and uses a MAD1
 file system.  You can use the NXP TagWriter app on Android to
 properly format cards for this.

 @param  sector_num  The sector that the URI record should be written
 to (can be 1..15 for a 1K card)
 @param  uri_identifier The uri identifier code (0 = none, 0x01 =
 "http://www.", etc.)
 @param  url           The uri text to write (max 38 characters).

 @returns 1 if everything executed properly, 0 for an error
 */
/**************************************************************************/
bool mfc_write_ndef_uri(uint8_t sector_num, uint8_t uri_identifier, const char* url) {
  // Figure out how long the string is
  uint8_t len = strlen(url);

  // Make sure we're within a 1K limit for the sector number
  if ((sector_num < 1) || (sector_num > 15)) {
    return false;
  }

  // Make sure the URI payload is between 1 and 38 chars
  if ((len < 1) || (len > 38)) {
    return false;
  }

  // Note 0xD3 0xF7 0xD3 0xF7 0xD3 0xF7 must be used for key A
  // in NDEF records

  // Setup the sector buffer (w/pre-formatted TLV wrapper and NDEF message)
  uint8_t sectorbuffer1[16] = {
      0x00,
      0x00,
      0x03,
      len + 5,
      0xD1,
      0x01,
      len + 1,
      0x55,
      uri_identifier,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00
  };
  uint8_t sectorbuffer2[16] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  uint8_t sectorbuffer3[16] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  uint8_t sectorbuffer4[16] = {
      0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7, 0x7F, 0x07, 0x88, 0x40, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  if (len <= 6) {
    // Unlikely we'll get a url this short, but why not ...
    memcpy(sectorbuffer1 + 9, url, len);
    sectorbuffer1[len + 9] = 0xFE;
  } else if (len == 7) {
    // 0xFE needs to be wrapped around to next block
    memcpy(sectorbuffer1 + 9, url, len);
    sectorbuffer2[0] = 0xFE;
  } else if ((len > 7) && (len <= 22)) {
    // Url fits in two blocks
    memcpy(sectorbuffer1 + 9, url, 7);
    memcpy(sectorbuffer2, url + 7, len - 7);
    sectorbuffer2[len - 7] = 0xFE;
  } else if (len == 23) {
    // 0xFE needs to be wrapped around to final block
    memcpy(sectorbuffer1 + 9, url, 7);
    memcpy(sectorbuffer2, url + 7, len - 7);
    sectorbuffer3[0] = 0xFE;
  } else {
    // Url fits in three blocks
    memcpy(sectorbuffer1 + 9, url, 7);
    memcpy(sectorbuffer2, url + 7, 16);
    memcpy(sectorbuffer3, url + 23, len - 24);
    sectorbuffer3[len - 22] = 0xFE;
  }

  // Now write all three blocks back to the card
  if (!(mfc_write_data_block(sector_num * 4, sectorbuffer1))) {
    return false;
  }
  if (!(mfc_write_data_block((sector_num * 4) + 1, sectorbuffer2))) {
    return false;
  }
  if (!(mfc_write_data_block((sector_num * 4) + 2, sectorbuffer3))) {
    return false;
  }
  if (!(mfc_write_data_block((sector_num * 4) + 3, sectorbuffer4))) {
    return false;
  }

  // Seems that everything was OK (?!)
  return true;
}

/***** Mifare Ultralight Functions ******/

/**************************************************************************/
/*!
 Tries to read an entire 4-byte page at the specified address.

 @param  page        The page number (0..63 in most cases)
 @param  buffer      Pointer to the byte array that will hold the
 retrieved data (if any)
 */
/**************************************************************************/
bool mfu_read_page(uint8_t page, uint8_t* buffer, uint8_t buffer_size) {
  if (page >= 64 || buffer_size > 16 || buffer_size < 4) {
    return false;
  }
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = 1;
  pn532_packetbuffer[2] = MIFARE_CMD_READ;
  pn532_packetbuffer[3] = page;
  if (pn532_exchange(4, I2C_READ_TIMEOUT) != 17 || pn532_packetbuffer[7] != 0) {
    return false;
  }
  memcpy(buffer, pn532_packetbuffer + 8, buffer_size);
  return true;
}

/**************************************************************************/
/*!
 Tries to write an entire 4-byte page at the specified block
 address.

 @param  page          The page number to write.  (0..63 for most cases)
 @param  data          The byte array that contains the data to write.
 Should be exactly 4 bytes long.

 @returns 1 if everything executed properly, 0 for an error
 */
/**************************************************************************/
bool mfu_write_page(uint8_t page, uint8_t* data) {
  if (page >= 64) {
    return false;
  }
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = 1;
  pn532_packetbuffer[2] = MIFARE_ULTRALIGHT_CMD_WRITE;
  pn532_packetbuffer[3] = page;
  memcpy(pn532_packetbuffer + 4, data, 4);
  // InDataExchange interprets the tag's ACK/NAK and returns a PN532 status.
  return pn532_exchange(8, I2C_READ_TIMEOUT) == 1 && pn532_packetbuffer[7] == 0;
}

static bool mfu_read_counter_value(uint8_t counter, uint32_t* value) {
  if (counter > 2) {
    return false;
  }
  pn532_packetbuffer[0] = PN532_COMMAND_INCOMMUNICATETHRU;
  pn532_packetbuffer[1] = 0x39;  // READ_CNT
  pn532_packetbuffer[2] = counter;
  if (pn532_exchange(3, I2C_READ_TIMEOUT) != 4 || pn532_packetbuffer[7] != 0) {
    return false;
  }
  *value = (uint32_t)pn532_packetbuffer[8] |
           ((uint32_t)pn532_packetbuffer[9] << 8) |
           ((uint32_t)pn532_packetbuffer[10] << 16);
  return true;
}

bool mfu_increment_counter(uint8_t counter, uint8_t value, uint32_t expected_value) {
  uint32_t before;
  if (!mfu_read_counter_value(counter, &before) || before != expected_value ||
      before + value > 0xFFFFFF) {
    return false;
  }
  if (value == 0) {
    return true;
  }
  pn532_packetbuffer[0] = PN532_COMMAND_INCOMMUNICATETHRU;
  pn532_packetbuffer[1] = 0xA5;  // INCR_CNT
  pn532_packetbuffer[2] = counter;
  pn532_packetbuffer[3] = value;
  pn532_packetbuffer[4] = 0x00;
  pn532_packetbuffer[5] = 0x00;
  pn532_packetbuffer[6] = 0x00;  // Ignored by the tag.
  int length = pn532_exchange(7, I2C_READ_TIMEOUT);
  if ((length != 1 && length != 2) ||
      (pn532_packetbuffer[7] != 0 && pn532_packetbuffer[7] != 0x02) ||
      (length == 2 && pn532_packetbuffer[8] != 0x0A)) {
    return false;
  }

  // INCR_CNT returns a four-bit ACK without CRC. InCommunicateThru may report
  // CRC error (0x02) even after a successful increment. Neither that status nor
  // command receipt proves success: confirm the full 24-bit counter instead.
  uint32_t after;
  return mfu_read_counter_value(counter, &after) && after == before + value;
}

bool mfu_read_counter(uint8_t counter, uint16_t* value) {
  uint32_t counter_value;
  if (!mfu_read_counter_value(counter, &counter_value) || counter_value > UINT16_MAX) {
    return false;
  }
  // Never alias an out-of-format physical counter to a valid 16-bit baseline.
  *value = (uint16_t)counter_value;
  return true;
}

/***** NTAG2xx Functions ******/

/**************************************************************************/
/*!
 Tries to read an entire 4-byte page at the specified address.

 @param  page        The page number (0..63 in most cases)
 @param  buffer      Pointer to the byte array that will hold the
 retrieved data (if any)
 */
/**************************************************************************/
bool ntag2xx_read_page(uint8_t page, uint8_t* buffer) {
  if (page >= 231) {
    return false;
  }
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = 1;
  pn532_packetbuffer[2] = MIFARE_CMD_READ;
  pn532_packetbuffer[3] = page;
  if (pn532_exchange(4, I2C_READ_TIMEOUT) != 17 || pn532_packetbuffer[7] != 0) {
    return false;
  }
  memcpy(buffer, pn532_packetbuffer + 8, 4);
  return true;
}

/**************************************************************************/
/*!
 Tries to write an entire 4-byte page at the specified block
 address.

 @param  page          The page number to write.  (0..63 for most cases)
 @param  data          The byte array that contains the data to write.
 Should be exactly 4 bytes long.

 @returns 1 if everything executed properly, 0 for an error
 */
/**************************************************************************/
bool ntag2xx_write_page(uint8_t page, uint8_t* data) {
  if (page < 4 || page > 225) {
    return false;
  }
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = 1;
  pn532_packetbuffer[2] = MIFARE_ULTRALIGHT_CMD_WRITE;
  pn532_packetbuffer[3] = page;
  memcpy(pn532_packetbuffer + 4, data, 4);
  return pn532_exchange(8, I2C_READ_TIMEOUT) == 1 && pn532_packetbuffer[7] == 0;
}

/**************************************************************************/
/*!
 Writes an NDEF URI Record starting at the specified page (4..nn)

 Note that this function assumes that the NTAG2xx card is
 already formatted to work as an "NFC Forum Tag".

 @param  uri_identifier The uri identifier code (0 = none, 0x01 =
 "http://www.", etc.)
 @param  url           The uri text to write (null-terminated string).
 @param  data_len       The size of the data area for overflow checks.

 @returns 1 if everything executed properly, 0 for an error
 */
/**************************************************************************/
bool ntag2xx_write_ndef_uri(uint8_t uri_identifier, char* url, uint8_t data_len) {
  uint8_t pageBuffer[4] = {0, 0, 0, 0};

  // Remove NDEF record overhead from the URI data (pageHeader below)
  uint8_t wrapperSize = 12;

  // Figure out how long the string is
  uint8_t len = strlen(url);

  // Make sure the URI payload will fit in data_len (include 0xFE trailer)
  if ((len < 1) || (len + 1 > (data_len - wrapperSize))) {
    return false;
  }

  // Setup the record header
  // See NFCForum-TS-Type-2-Tag_1.1.pdf for details
  uint8_t pageHeader[12] = {
      /* NDEF Lock Control TLV (must be first and always present) */
      0x01, /* Tag Field (0x01 = Lock Control TLV) */
      0x03, /* Payload Length (always 3) */
      0xA0, /* The position inside the tag of the lock bytes (upper 4 = page address, lower 4 = byte
               offset) */
      0x10, /* Size in bits of the lock area */
      0x44, /* Size in bytes of a page and the number of bytes each lock bit can lock (4 bit + 4
               bits) */
      /* NDEF Message TLV - URI Record */
      0x03,          /* Tag Field (0x03 = NDEF Message) */
      len + 5,       /* Payload Length (not including 0xFE trailer) */
      0xD1,          /* NDEF Record Header (TNF=0x1:Well known record + SR + ME + MB) */
      0x01,          /* Type Length for the record type indicator */
      len + 1,       /* Payload len */
      0x55,          /* Record Type Indicator (0x55 or 'U' = URI Record) */
      uri_identifier /* URI Prefix (ex. 0x01 = "http://www.") */
  };

  // Write 12 byte header (three pages of data starting at page 4)
  memcpy(pageBuffer, pageHeader, 4);
  if (!(ntag2xx_write_page(4, pageBuffer))) {
    return false;
  }
  memcpy(pageBuffer, pageHeader + 4, 4);
  if (!(ntag2xx_write_page(5, pageBuffer))) {
    return false;
  }
  memcpy(pageBuffer, pageHeader + 8, 4);
  if (!(ntag2xx_write_page(6, pageBuffer))) {
    return false;
  }

  // Write URI (starting at page 7)
  uint8_t currentPage = 7;
  char* urlcopy = url;
  while (len) {
    if (len < 4) {
      memset(pageBuffer, 0, 4);
      memcpy(pageBuffer, urlcopy, len);
      pageBuffer[len] = 0xFE;  // NDEF record footer
      if (!(ntag2xx_write_page(currentPage, pageBuffer))) {
        return false;
      }
      // DONE!
      return true;
    } else if (len == 4) {
      memcpy(pageBuffer, urlcopy, len);
      if (!(ntag2xx_write_page(currentPage, pageBuffer))) {
        return false;
      }
      memset(pageBuffer, 0, 4);
      pageBuffer[0] = 0xFE;  // NDEF record footer
      currentPage++;
      if (!(ntag2xx_write_page(currentPage, pageBuffer))) {
        return false;
      }
      // DONE!
      return true;
    } else {
      // More than one page of data left
      memcpy(pageBuffer, urlcopy, 4);
      if (!(ntag2xx_write_page(currentPage, pageBuffer))) {
        return false;
      }
      currentPage++;
      urlcopy += 4;
      len -= 4;
    }
  }

  // Seems that everything was OK (?!)
  return true;
}

bool ntag2xx_authenticate(uint8_t* pwd, uint8_t* pack) {
  pn532_packetbuffer[0] = PN532_COMMAND_INCOMMUNICATETHRU;
  pn532_packetbuffer[1] = MIFARE_PWD_AUTH_COMMAND;
  memcpy(pn532_packetbuffer + 2, pwd, 4);
  if (pn532_exchange(6, I2C_READ_TIMEOUT) != 3 || pn532_packetbuffer[7] != 0) {
    return false;
  }
  memcpy(pack, pn532_packetbuffer + 8, 2);
  return true;
}

int pn532_read_register(uint16_t reg) {
  pn532_packetbuffer[0] = PN532_COMMAND_READREGISTER;
  pn532_packetbuffer[1] = reg >> 8;
  pn532_packetbuffer[2] = reg & 0xFF;
  if (pn532_exchange(3, I2C_READ_TIMEOUT) != 1) {
    return -1;
  }
  return pn532_packetbuffer[7];
}

bool is_bit_set(int value, int bit_position) {
  return (value & (1 << bit_position)) != 0;
}

int pn532_antenna_test(bool decrease_lower_threshold, uint8_t andet_ithh) {
  resetPN532();
  pn532_packetbuffer[0] = PN532_COMMAND_DIAGNOSE;
  pn532_packetbuffer[1] = 0x07;  // Self Antenna Test

  // bit 7: andet_bot - A too low power consumption has been detected
  // bit 6: andet_up - A too high power consumption has been detected
  // bit 5 to 4: andet_ithl - Set the low current consumption threshold to be detected
  // bit 3 to 1: andet_ithh - Set the high current consumption threshold to be detected
  // bit 0: andet_en - Enable the detection of the antenna presence detector functionality
  uint8_t andet_control = 0b00110001;

  if (decrease_lower_threshold) {
    // sets the lower threshold to 25mA
    andet_control &= ~(1 << 4);
  }

  if (is_bit_set(andet_ithh, 0)) {
    andet_control |= 1 << 1;
  }
  if (is_bit_set(andet_ithh, 1)) {
    andet_control |= 1 << 2;
  }
  if (is_bit_set(andet_ithh, 2)) {
    andet_control |= 1 << 3;
  }

  pn532_packetbuffer[2] = andet_control;

  if (pn532_exchange(3, I2C_READ_TIMEOUT) < 0) {
    return -1;
  }

  return pn532_read_register(0x610C);  // Andet_control register
}
