/* Host tests exercise the production driver against a scripted I2C/IRQ device.
 * The script describes complete wire transactions, including the I2C status
 * byte. It deliberately does not replace the driver's exchange/parser code.
 */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stubs/sdk.h"

#define MAX_STEPS 64
#define MAX_WIRE_BYTES 128

typedef struct {
  bool read;
  uint8_t bytes[MAX_WIRE_BYTES];
  size_t size;
  esp_err_t error;
  TickType_t delay;
} wire_step_t;

struct mock_i2c_command {
  uint8_t written[MAX_WIRE_BYTES];
  size_t written_size;
  uint8_t *read_destinations[MAX_WIRE_BYTES];
  int read_ack[MAX_WIRE_BYTES];
  size_t read_size;
  unsigned starts;
  unsigned stops;
};

static wire_step_t script[MAX_STEPS];
static size_t script_size;
static size_t script_position;
static TickType_t clock_ticks;
static TickType_t step_started_at;
static unsigned stale_irq_events;
static bool deliver_irq_edges;
static unsigned i2c_reads;
static unsigned i2c_writes;
static unsigned queue_receives;
static unsigned live_commands;
static unsigned cases_run;
static int queue_token;
static const char *test_name;

static void require(bool condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s: %s (wire step %zu/%zu, tick %u)\n",
            test_name, message, script_position, script_size, clock_ticks);
    abort();
  }
}

/* Include the actual implementation, so its private recovery state can be
 * reset between independent cases without adding production test hooks. */
#include "../../main/pn532.c"

static bool next_frame_ready(void) {
  return script_position < script_size && script[script_position].read &&
         clock_ticks - step_started_at >= script[script_position].delay;
}

i2c_cmd_handle_t i2c_cmd_link_create(void) {
  i2c_cmd_handle_t command = calloc(1, sizeof(*command));
  assert(command);
  live_commands++;
  return command;
}

void i2c_cmd_link_delete(i2c_cmd_handle_t command) {
  assert(command && live_commands > 0);
  live_commands--;
  free(command);
}

esp_err_t i2c_master_start(i2c_cmd_handle_t command) {
  command->starts++;
  return ESP_OK;
}

esp_err_t i2c_master_stop(i2c_cmd_handle_t command) {
  command->stops++;
  return ESP_OK;
}

esp_err_t i2c_master_write_byte(i2c_cmd_handle_t command, uint8_t byte, bool ack) {
  require(command->written_size < MAX_WIRE_BYTES, "oversized I2C write");
  require(ack, "I2C writes must check slave acknowledgement");
  command->written[command->written_size++] = byte;
  return ESP_OK;
}

esp_err_t i2c_master_read_byte(i2c_cmd_handle_t command, uint8_t *byte, int ack) {
  require(command->read_size < MAX_WIRE_BYTES, "oversized I2C read");
  command->read_destinations[command->read_size] = byte;
  command->read_ack[command->read_size++] = ack;
  return ESP_OK;
}

esp_err_t i2c_master_write(i2c_cmd_handle_t command, const uint8_t *bytes,
                           size_t size, bool ack) {
  for (size_t i = 0; i < size; i++) i2c_master_write_byte(command, bytes[i], ack);
  return ESP_OK;
}

esp_err_t i2c_master_read(i2c_cmd_handle_t command, uint8_t *bytes,
                          size_t size, int ack) {
  for (size_t i = 0; i < size; i++) {
    i2c_master_read_byte(command, bytes + i,
                         i + 1 == size ? ack : I2C_MASTER_ACK);
  }
  return ESP_OK;
}

esp_err_t i2c_master_cmd_begin(i2c_port_t port, i2c_cmd_handle_t command,
                               TickType_t timeout) {
  (void)port;
  (void)timeout;
  require(command->starts == 1 && command->stops == 1,
          "each scripted frame must use one complete I2C transaction");
  require(script_position < script_size, "unexpected I2C transaction");
  wire_step_t *step = &script[script_position];
  if (step->read) {
    require(next_frame_ready(), "driver read a result before it was ready");
    require(command->written_size == 1 &&
                command->written[0] == PN532_I2C_READ_ADDRESS,
            "wrong I2C read address or unexpected write");
    require(command->read_size == step->size, "wrong I2C frame read size");
    for (size_t i = 0; i < command->read_size; i++) {
      require(command->read_ack[i] ==
                  (i + 1 == command->read_size ? I2C_MASTER_LAST_NACK : I2C_MASTER_ACK),
              "incorrect I2C ACK/NACK sequence");
      /* Failed transport may still have delivered partial bytes. */
      if (step->error == ESP_OK || i < command->read_size / 2) {
        *command->read_destinations[i] = step->bytes[i];
      }
    }
    i2c_reads++;
  } else {
    require(command->read_size == 0, "read attempted where command was expected");
    require(command->written_size == step->size, "wrong command frame size");
    require(memcmp(command->written, step->bytes, step->size) == 0,
            "unexpected command bytes or unintended operation replay");
    i2c_writes++;
  }
  script_position++;
  step_started_at = clock_ticks;
  return step->error;
}

esp_err_t i2c_param_config(i2c_port_t p, const i2c_config_t *c) {
  (void)p; (void)c; return ESP_OK;
}
esp_err_t i2c_driver_install(i2c_port_t p, int m, int r, int t, int f) {
  (void)p; (void)m; (void)r; (void)t; (void)f; return ESP_OK;
}
esp_err_t i2c_set_timeout(i2c_port_t p, int t) { (void)p; (void)t; return ESP_OK; }
esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return ESP_OK; }
esp_err_t gpio_set_level(int p, int l) { (void)p; (void)l; return ESP_OK; }
int gpio_get_level(int pin) { require(pin == IRQ_PIN, "unexpected GPIO read"); return !next_frame_ready(); }
esp_err_t gpio_install_isr_service(int f) { (void)f; return ESP_OK; }
esp_err_t gpio_isr_handler_add(int p, void (*h)(void *), void *a) {
  (void)p; (void)h; (void)a; return ESP_OK;
}
void vTaskDelay(TickType_t ticks) { clock_ticks += ticks; }
TickType_t xTaskGetTickCount(void) { return clock_ticks; }
QueueHandle_t xQueueCreate(unsigned s, unsigned i) { (void)s; (void)i; return &queue_token; }
void vQueueDelete(QueueHandle_t q) { (void)q; }
int xQueueReset(QueueHandle_t q) { (void)q; stale_irq_events = 0; return pdTRUE; }
int xQueueSendFromISR(QueueHandle_t q, const void *i, void *w) {
  (void)q; (void)i; (void)w; stale_irq_events++; return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void *item, TickType_t timeout) {
  (void)q;
  require(++queue_receives < 100000, "readiness wait did not terminate");
  if (stale_irq_events) {
    stale_irq_events--;
    *(uint32_t *)item = IRQ_PIN;
    return pdTRUE;
  }
  if (deliver_irq_edges && script_position < script_size && script[script_position].read) {
    TickType_t elapsed = clock_ticks - step_started_at;
    TickType_t delay = script[script_position].delay;
    TickType_t until_ready = elapsed >= delay ? 0 : delay - elapsed;
    if (until_ready <= timeout) {
      clock_ticks += until_ready;
      *(uint32_t *)item = IRQ_PIN;
      return pdTRUE;
    }
  }
  require(timeout != portMAX_DELAY, "test unexpectedly waited forever");
  clock_ticks += timeout;
  return pdFALSE;
}
const char *esp_err_to_name(esp_err_t e) { (void)e; return "injected I2C error"; }

static void begin_case(const char *name) {
  require(live_commands == 0, "previous case leaked an I2C command");
  cases_run++;
  test_name = name;
  memset(script, 0, sizeof(script));
  script_size = script_position = 0;
  clock_ticks = step_started_at = 0;
  stale_irq_events = i2c_reads = i2c_writes = queue_receives = 0;
  deliver_irq_edges = true;
  IRQ_PIN = 47;
  IRQQueue = &queue_token;
  pn532_needs_resync = false;
  memset(pn532_packetbuffer, 0xCC, sizeof(pn532_packetbuffer));
}

static void finish_case(void) {
  require(script_position == script_size, "expected wire operations were not completed");
  require(live_commands == 0, "I2C command allocation leaked");
}

static wire_step_t *add_step(bool read) {
  require(script_size < MAX_STEPS, "test script overflow");
  wire_step_t *step = &script[script_size++];
  step->read = read;
  step->error = ESP_OK;
  return step;
}

static wire_step_t *expect_command(const uint8_t *command, size_t size) {
  wire_step_t *step = add_step(false);
  step->size = size + 9;
  uint8_t *wire = step->bytes;
  wire[0] = PN532_I2C_ADDRESS;
  wire[1] = wire[2] = 0;
  wire[3] = 0xFF;
  wire[4] = size + 1;
  wire[5] = (uint8_t)-wire[4];
  wire[6] = 0xD4;
  memcpy(wire + 7, command, size);
  uint8_t sum = 0xD4;
  for (size_t i = 0; i < size; i++) sum += command[i];
  wire[7 + size] = (uint8_t)-sum;
  wire[8 + size] = 0;
  return step;
}

static wire_step_t *expect_ack(void) {
  static const uint8_t ack[] = {1, 0, 0, 0xFF, 0, 0xFF, 0};
  wire_step_t *step = add_step(true);
  step->size = sizeof(ack);
  memcpy(step->bytes, ack, sizeof(ack));
  return step;
}

static void fix_response_checksum(wire_step_t *step) {
  uint8_t *frame = step->bytes + 1;
  uint8_t sum = 0;
  for (size_t i = 0; i < frame[3]; i++) sum += frame[5 + i];
  frame[5 + frame[3]] = (uint8_t)-sum;
}

static wire_step_t *expect_response(uint8_t response, const uint8_t *data, size_t size) {
  wire_step_t *step = add_step(true);
  step->size = 65; /* I2C status plus the bounded, complete response read. */
  memset(step->bytes, 0xEE, step->size);
  step->bytes[0] = 1;
  uint8_t *frame = step->bytes + 1;
  frame[0] = frame[1] = 0;
  frame[2] = 0xFF;
  frame[3] = size + 2;
  frame[4] = (uint8_t)-frame[3];
  frame[5] = 0xD5;
  frame[6] = response;
  memcpy(frame + 7, data, size);
  fix_response_checksum(step);
  frame[6 + frame[3]] = 0;
  return step;
}

static wire_step_t *expect_exchange(const uint8_t *command, size_t command_size,
                                    const uint8_t *data, size_t data_size) {
  expect_command(command, command_size);
  expect_ack();
  return expect_response(command[0] + 1, data, data_size);
}

static const uint8_t page_data[] = {0x11, 0x22, 0x33, 0x44};
static const uint8_t write_page_command[] = {0x40, 1, 0xA2, 11, 0x11, 0x22, 0x33, 0x44};
static const uint8_t read_page_command[] = {0x40, 1, 0x30, 8};
static const uint8_t counter_command[] = {0x42, 0x39, 0};
static const uint8_t increment_command[] = {0x42, 0xA5, 0, 1, 0, 0, 0};
static const uint8_t auth_command[] = {0x42, 0x1B, 0x12, 0x34, 0x56, 0x78};
static const uint8_t status_ok[] = {0};

static wire_step_t *expect_page_write(void) {
  return expect_exchange(write_page_command, sizeof(write_page_command), status_ok, 1);
}

static wire_step_t *expect_counter(uint32_t value) {
  uint8_t data[] = {0, value, value >> 8, value >> 16};
  return expect_exchange(counter_command, sizeof(counter_command), data, sizeof(data));
}

static wire_step_t *expect_increment_result(uint8_t status, int ack) {
  uint8_t data[] = {status, ack};
  return expect_exchange(increment_command, sizeof(increment_command), data, ack < 0 ? 1 : 2);
}

static wire_step_t *expect_barrier(void) {
  static const uint8_t abort_frame[] = {0x48, 0, 0, 0xFF, 0, 0xFF, 0};
  static const uint8_t firmware_command[] = {0x02};
  static const uint8_t firmware_data[] = {0x32, 1, 6, 7};
  wire_step_t *abort = add_step(false);
  abort->size = sizeof(abort_frame);
  memcpy(abort->bytes, abort_frame, sizeof(abort_frame));
  return expect_exchange(firmware_command, 1, firmware_data, sizeof(firmware_data));
}

static void test_write_completion(void) {
  begin_case("write waits for delayed operation result");
  wire_step_t *response = expect_page_write();
  response->delay = pdMS_TO_TICKS(200);
  require(mfu_write_page(11, (uint8_t *)page_data), "valid delayed write rejected");
  require(clock_ticks >= pdMS_TO_TICKS(200) && i2c_reads == 2,
          "write returned before operation response");
  finish_case();

  begin_case("reader ACK alone is not card write success");
  expect_command(write_page_command, sizeof(write_page_command));
  expect_ack();
  require(!mfu_write_page(11, (uint8_t *)page_data), "ACK-only write accepted");
  require(pn532_needs_resync, "missing response did not require resynchronization");
  finish_case();

  begin_case("card write status failure");
  uint8_t status[] = {0x14};
  expect_exchange(write_page_command, sizeof(write_page_command), status, sizeof(status));
  require(!mfu_write_page(11, (uint8_t *)page_data), "card rejection accepted");
  require(i2c_writes == 1, "failed write replayed automatically");
  finish_case();
}

static void test_transport_failures(void) {
  begin_case("command I2C failure stops before ACK read");
  expect_command(write_page_command, sizeof(write_page_command))->error = ESP_FAIL;
  require(!mfu_write_page(11, (uint8_t *)page_data), "failed command transport accepted");
  require(i2c_reads == 0 && pn532_needs_resync, "failed command did not stop safely");
  finish_case();

  begin_case("ACK I2C failure");
  expect_command(write_page_command, sizeof(write_page_command));
  expect_ack()->error = ESP_ERR_TIMEOUT;
  require(!mfu_write_page(11, (uint8_t *)page_data), "failed ACK transport accepted");
  require(pn532_needs_resync, "failed ACK did not require resynchronization");
  finish_case();

  begin_case("operation response I2C failure");
  expect_page_write()->error = ESP_ERR_TIMEOUT;
  require(!mfu_write_page(11, (uint8_t *)page_data), "failed response transport accepted");
  require(pn532_needs_resync, "failed response did not require resynchronization");
  finish_case();

  begin_case("invalid reader ACK");
  expect_command(write_page_command, sizeof(write_page_command));
  expect_ack()->bytes[4] = 1;
  require(!mfu_write_page(11, (uint8_t *)page_data), "invalid ACK accepted");
  require(pn532_needs_resync, "invalid ACK did not require resynchronization");
  finish_case();
}

static void test_invalid_frames(void) {
  static const char *names[] = {
      "I2C NOT READY", "bad preamble", "bad start code", "bad LCS", "bad DCS",
      "wrong direction", "wrong command", "bad postamble", "oversized LEN",
      "short LEN", "extended frame", "unexpected response data"};
  for (size_t fault = 0; fault < sizeof(names) / sizeof(names[0]); fault++) {
    begin_case(names[fault]);
    wire_step_t *step = expect_page_write();
    uint8_t *frame = step->bytes + 1;
    switch (fault) {
      case 0: step->bytes[0] = 0; break;
      case 1: frame[0] = 1; break;
      case 2: frame[2] = 0; break;
      case 3: frame[4] ^= 1; break;
      case 4: frame[5 + frame[3]] ^= 1; break;
      case 5: frame[5] = 0xD4; fix_response_checksum(step); break;
      case 6: frame[6] = 0x43; fix_response_checksum(step); break;
      case 7: frame[6 + frame[3]] = 1; break;
      case 8: frame[3] = 64; frame[4] = (uint8_t)-64; break;
      case 9: frame[3] = 1; frame[4] = (uint8_t)-1; break;
      case 10: frame[3] = frame[4] = 0xFF; break;
      case 11:
        frame[3] = 4; frame[4] = (uint8_t)-4; frame[8] = 0x0A;
        fix_response_checksum(step); frame[10] = 0; break;
    }
    require(!mfu_write_page(11, (uint8_t *)page_data), "invalid response accepted");
    if (fault < 11) {
      require(pn532_needs_resync, "invalid frame did not require resynchronization");
    }
    finish_case();
  }
}

static void test_irq_readiness(void) {
  begin_case("stale queued IRQ cannot authorize an early result read");
  stale_irq_events = 2;
  wire_step_t *response = expect_page_write();
  script[1].delay = pdMS_TO_TICKS(30);
  response->delay = pdMS_TO_TICKS(200);
  require(mfu_write_page(11, (uint8_t *)page_data), "stale IRQ prevented valid completion");
  finish_case();

  begin_case("IRQ already low works without queued interrupt");
  deliver_irq_edges = false;
  expect_page_write();
  require(mfu_write_page(11, (uint8_t *)page_data), "ready response required an IRQ edge");
  finish_case();

  begin_case("delayed result with lost IRQ edge still completes");
  deliver_irq_edges = false;
  expect_page_write()->delay = pdMS_TO_TICKS(200);
  require(mfu_write_page(11, (uint8_t *)page_data), "lost edge prevented readiness polling");
  finish_case();

  begin_case("stale IRQ with missing ACK times out without a read");
  stale_irq_events = 1;
  expect_command(write_page_command, sizeof(write_page_command));
  require(!mfu_write_page(11, (uint8_t *)page_data), "stale IRQ invented command ACK");
  require(i2c_reads == 0 && pn532_needs_resync, "missing ACK did not stop safely");
  finish_case();

  begin_case("readiness deadline tolerates tick counter wrap");
  clock_ticks = step_started_at = UINT32_MAX - 5;
  expect_page_write()->delay = pdMS_TO_TICKS(200);
  require(mfu_write_page(11, (uint8_t *)page_data), "tick wrap broke readiness deadline");
  finish_case();
}

static void test_read_outputs(void) {
  uint8_t page[16];
  uint8_t page_response[17] = {0};
  for (size_t i = 1; i < sizeof(page_response); i++) page_response[i] = 0x40 + i;
  begin_case("page read copies only requested bytes after validation");
  memset(page, 0xA5, sizeof(page));
  expect_exchange(read_page_command, sizeof(read_page_command), page_response, sizeof(page_response));
  require(mfu_read_page(8, page, 12), "valid page read rejected");
  require(memcmp(page, page_response + 1, 12) == 0 && page[12] == 0xA5,
          "page read copied wrong bytes or overran output");
  finish_case();

  begin_case("failed page read preserves output");
  memset(page, 0xA5, sizeof(page));
  expect_exchange(read_page_command, sizeof(read_page_command), page_response, sizeof(page_response))
      ->error = ESP_FAIL;
  require(!mfu_read_page(8, page, sizeof(page)), "failed page transport accepted");
  for (size_t i = 0; i < sizeof(page); i++) require(page[i] == 0xA5, "failed read changed output");
  finish_case();

  begin_case("short page response preserves output");
  expect_exchange(read_page_command, sizeof(read_page_command), page_response, 5);
  require(!mfu_read_page(8, page, sizeof(page)), "short page response accepted");
  for (size_t i = 0; i < sizeof(page); i++) require(page[i] == 0xA5, "short read changed output");
  finish_case();

  uint16_t counter = 0xBEEF;
  begin_case("valid counter value");
  expect_counter(0x1234);
  require(mfu_read_counter(0, &counter) && counter == 0x1234, "counter decoded incorrectly");
  finish_case();

  begin_case("failed counter read cannot invent zero");
  counter = 0xBEEF;
  expect_counter(0x1234)->error = ESP_FAIL;
  require(!mfu_read_counter(0, &counter) && counter == 0xBEEF,
          "failed counter read succeeded or changed output");
  finish_case();

  begin_case("short counter response preserves output");
  uint8_t short_counter[] = {0, 0x34, 0x12};
  expect_exchange(counter_command, sizeof(counter_command), short_counter, sizeof(short_counter));
  require(!mfu_read_counter(0, &counter) && counter == 0xBEEF, "short counter response accepted");
  finish_case();

  uint8_t pack[2] = {0xA5, 0x5A};
  uint8_t auth_response[] = {0, 0x34, 0x56};
  begin_case("valid authentication PACK");
  expect_exchange(auth_command, sizeof(auth_command), auth_response, sizeof(auth_response));
  require(ntag2xx_authenticate((uint8_t *)auth_command + 2, pack) &&
              pack[0] == 0x34 && pack[1] == 0x56, "PACK decoded incorrectly");
  finish_case();

  begin_case("failed authentication preserves PACK");
  pack[0] = 0xA5; pack[1] = 0x5A;
  expect_exchange(auth_command, sizeof(auth_command), auth_response, sizeof(auth_response))
      ->error = ESP_FAIL;
  require(!ntag2xx_authenticate((uint8_t *)auth_command + 2, pack) &&
              pack[0] == 0xA5 && pack[1] == 0x5A, "failed auth changed PACK or succeeded");
  finish_case();

  begin_case("short authentication result preserves PACK");
  expect_exchange(auth_command, sizeof(auth_command), auth_response, 2);
  require(!ntag2xx_authenticate((uint8_t *)auth_command + 2, pack) &&
              pack[0] == 0xA5 && pack[1] == 0x5A, "short auth result accepted");
  finish_case();
}

static void test_counter_increment(void) {
  begin_case("increment verified with full 24-bit baseline/readback");
  expect_counter(0x123456);
  expect_increment_result(0, 0x0A);
  expect_counter(0x123457);
  require(mfu_increment_counter(0, 1), "verified increment rejected");
  require(i2c_writes == 3, "increment command repeated");
  finish_case();

  begin_case("increment readback handles carry into the third counter byte");
  expect_counter(0x00FFFF);
  expect_increment_result(0, 0x0A);
  expect_counter(0x010000);
  require(mfu_increment_counter(0, 1), "24-bit counter carry was truncated");
  finish_case();

  begin_case("matching low counter bytes cannot hide an unexpected high byte");
  expect_counter(0x013456);
  expect_increment_result(0, 0x0A);
  expect_counter(0x023457);
  require(!mfu_increment_counter(0, 1), "counter verification ignored the high byte");
  finish_case();

  begin_case("CRC status on four-bit ACK resolved by counter readback");
  expect_counter(2);
  expect_increment_result(2, -1);
  expect_counter(3);
  require(mfu_increment_counter(0, 1), "verified CRC-error increment rejected");
  finish_case();

  begin_case("status-only increment result still requires readback");
  expect_counter(2);
  expect_increment_result(0, -1);
  expect_counter(3);
  require(mfu_increment_counter(0, 1), "verified status-only increment rejected");
  finish_case();

  begin_case("CRC status does not prove increment success");
  expect_counter(2);
  expect_increment_result(2, -1);
  expect_counter(2);
  require(!mfu_increment_counter(0, 1), "unchanged counter accepted as increment");
  require(i2c_writes == 3, "unconfirmed increment replayed");
  finish_case();

  begin_case("tag NAK is not successful increment");
  expect_counter(2);
  expect_increment_result(0, 4);
  require(!mfu_increment_counter(0, 1), "counter NAK accepted");
  finish_case();

  begin_case("increment RF timeout is not success or automatic replay");
  expect_counter(2);
  expect_increment_result(1, -1);
  require(!mfu_increment_counter(0, 1), "increment RF timeout accepted");
  require(i2c_writes == 2, "increment replayed after RF timeout");
  finish_case();

  begin_case("failed baseline read prevents any increment");
  expect_counter(2)->error = ESP_FAIL;
  require(!mfu_increment_counter(0, 1), "failed baseline accepted");
  require(i2c_writes == 1, "increment sent without valid baseline");
  finish_case();

  begin_case("lost increment result is not automatically replayed");
  expect_counter(2);
  expect_increment_result(0, 0x0A)->error = ESP_FAIL;
  require(!mfu_increment_counter(0, 1), "lost increment result accepted");
  require(i2c_writes == 2 && pn532_needs_resync, "lost increment replayed or uncertainty forgotten");
  finish_case();

  begin_case("failed increment readback is not success");
  expect_counter(2);
  expect_increment_result(0, 0x0A);
  expect_counter(3)->error = ESP_FAIL;
  require(!mfu_increment_counter(0, 1), "failed readback accepted");
  require(i2c_writes == 3, "increment repeated after failed readback");
  finish_case();

  begin_case("zero increment sends no modifying command");
  expect_counter(3);
  require(mfu_increment_counter(0, 0), "zero increment rejected");
  require(i2c_writes == 1, "zero increment sent an unnecessary modifying command");
  finish_case();

  begin_case("24-bit overflow prevented before increment");
  expect_counter(0xFFFFFF);
  require(!mfu_increment_counter(0, 1), "overflowing increment accepted");
  require(i2c_writes == 1, "overflowing increment sent to card");
  finish_case();
}

static void test_resynchronization(void) {
  begin_case("next command requires abort and valid firmware barrier");
  expect_page_write()->error = ESP_FAIL;
  require(!mfu_write_page(11, (uint8_t *)page_data), "injected failure accepted");
  require(pn532_needs_resync, "failure did not persist recovery state");
  expect_barrier();
  expect_page_write();
  require(mfu_write_page(11, (uint8_t *)page_data), "command failed after valid barrier");
  require(!pn532_needs_resync, "valid barrier did not clear recovery state");
  finish_case();

  begin_case("invalid recovery barrier prevents pending mutation");
  pn532_needs_resync = true;
  wire_step_t *barrier = expect_barrier();
  barrier->bytes[7] = 0x41; /* Wrong response command, but valid checksum. */
  fix_response_checksum(barrier);
  require(!mfu_write_page(11, (uint8_t *)page_data), "bad barrier allowed write");
  require(pn532_needs_resync && i2c_writes == 2, "bad barrier cleared recovery state");
  finish_case();

  /* A subsequent caller may recover, but its own command must survive the
   * private firmware exchange instead of being overwritten in the buffer. */
  expect_barrier();
  expect_page_write();
  require(mfu_write_page(11, (uint8_t *)page_data), "later valid recovery did not succeed");
  require(!pn532_needs_resync, "recovery flag remained set after valid barrier");
  finish_case();

  begin_case("recovery result timeout blocks mutation");
  pn532_needs_resync = true;
  expect_barrier();
  script_size--; /* Firmware ACK arrives, but its operation result never does. */
  require(!mfu_write_page(11, (uint8_t *)page_data), "missing barrier result allowed mutation");
  require(pn532_needs_resync && i2c_writes == 2, "barrier timeout lost recovery state");
  finish_case();

  begin_case("failed abort prevents pending mutation");
  pn532_needs_resync = true;
  static const uint8_t abort_frame[] = {0x48, 0, 0, 0xFF, 0, 0xFF, 0};
  wire_step_t *abort = add_step(false);
  abort->size = sizeof(abort_frame);
  memcpy(abort->bytes, abort_frame, sizeof(abort_frame));
  abort->error = ESP_FAIL;
  require(!mfu_write_page(11, (uint8_t *)page_data), "failed abort allowed mutation");
  require(pn532_needs_resync && i2c_writes == 1, "failed abort lost recovery state");
  finish_case();
}

static void test_startup_and_selection(void) {
  static const uint8_t firmware_command[] = {0x02};
  static const uint8_t firmware[] = {0x32, 1, 6, 7, 0xA5};
  begin_case("firmware response decodes all four data bytes");
  expect_exchange(firmware_command, 1, firmware, 4);
  require(pn532_get_firmware_version() == 0x32010607, "firmware bytes decoded incorrectly");
  finish_case();

  for (size_t size = 3; size <= 5; size += 2) {
    begin_case(size == 3 ? "short firmware response rejected" : "long firmware response rejected");
    expect_exchange(firmware_command, 1, firmware, size);
    require(pn532_get_firmware_version() == 0, "incorrect firmware response length accepted");
    finish_case();
  }

  static const uint8_t sam_command[] = {0x14, 1, 0x14, 1};
  begin_case("SAM configuration consumes empty successful response");
  expect_exchange(sam_command, sizeof(sam_command), status_ok, 0);
  require(pn532_sam_configuration(), "empty SAM response rejected");
  finish_case();

  begin_case("SAM response must not contain an invented status byte");
  expect_exchange(sam_command, sizeof(sam_command), status_ok, 1);
  require(!pn532_sam_configuration(), "extra SAM response data accepted");
  finish_case();

  static const uint8_t select_command[] = {0x4A, 1, 0};
  uint8_t selected[] = {1, 1, 0, 0x44, 0, 7, 0x04, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
                        0xDE, 0xF0, 0x11};
  uint8_t uid[7];
  uint8_t uid_size;
  for (uint8_t size = 4; size <= 7; size += 3) {
    begin_case(size == 4 ? "four-byte UID selection" : "seven-byte UID selection");
    memset(uid, 0xA5, sizeof(uid));
    uid_size = 0xA5;
    selected[5] = size;
    expect_exchange(select_command, sizeof(select_command), selected, 6 + size);
    require(iso14443a_read_passive_target_id(0, uid, &uid_size, 1000), "valid UID rejected");
    require(uid_size == size && memcmp(uid, selected + 6, size) == 0 && _inListedTag == 1,
            "selected UID or target number decoded incorrectly");
    for (size_t i = size; i < sizeof(uid); i++) require(uid[i] == 0xA5, "UID output overrun");
    finish_case();
  }

  for (unsigned fault = 0; fault < 2; fault++) {
    begin_case(fault == 0 ? "ten-byte UID cannot overrun caller" : "truncated UID rejected");
    memset(uid, 0xA5, sizeof(uid));
    uid_size = 0xA5;
    _inListedTag = 0xCC;
    selected[5] = fault == 0 ? 10 : 7;
    expect_exchange(select_command, sizeof(select_command), selected, fault == 0 ? 16 : 12);
    require(!iso14443a_read_passive_target_id(0, uid, &uid_size, 1000), "invalid UID accepted");
    require(uid_size == 0xA5 && _inListedTag == 0xCC, "failed selection changed caller state");
    for (size_t i = 0; i < sizeof(uid); i++) require(uid[i] == 0xA5, "failed selection changed UID");
    finish_case();
  }
}

static void test_deselect_and_autopoll(void) {
  static const uint8_t deselect_command[] = {0x44, 0};
  static const uint8_t poll_command[] = {0x60, 1, 1, 0};
  /* UM0701-02 section 7.3.13: NbTg, Type, Ln, then Ln bytes of TargetData.
   * Mifare TargetData is Tg, SENS_RES[2], SEL_RES, UID length, UID. */
  static const uint8_t one_target[] = {
      1, 0x10, 12, 1, 0, 0x44, 0, 7, 4, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
  static const uint8_t two_targets[] = {
      2, 0x10, 12, 1, 0, 0x44, 0, 7, 4, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
         0x10, 12, 2, 0, 0x44, 0, 7, 4, 0x21, 0x43, 0x65, 0x87, 0xA9, 0xCB};

  begin_case("deselect result consumed before subsequent autopoll");
  expect_exchange(deselect_command, sizeof(deselect_command), status_ok, 1);
  expect_exchange(poll_command, sizeof(poll_command), one_target, sizeof(one_target));
  require(iso14443a_in_deselect(), "successful deselection rejected");
  require(iso14443a_in_auto_poll(1), "present card treated as removed");
  finish_case();

  begin_case("deselect card error is rejected");
  const uint8_t error[] = {0x27};
  expect_exchange(deselect_command, sizeof(deselect_command), error, sizeof(error));
  require(!iso14443a_in_deselect(), "deselect error accepted");
  finish_case();

  begin_case("autopoll no-target response");
  expect_exchange(poll_command, sizeof(poll_command), status_ok, 1);
  require(!iso14443a_in_auto_poll(1), "empty field treated as occupied");
  finish_case();

  begin_case("autopoll accepts two present targets");
  expect_exchange(poll_command, sizeof(poll_command), two_targets, sizeof(two_targets));
  require(iso14443a_in_auto_poll(1), "two present targets treated as removed");
  finish_case();

  begin_case("autopoll rejects truncated first target record");
  expect_exchange(poll_command, sizeof(poll_command), one_target, sizeof(one_target) - 1);
  require(!iso14443a_in_auto_poll(1), "truncated target record accepted");
  finish_case();

  begin_case("autopoll rejects truncated second target record");
  expect_exchange(poll_command, sizeof(poll_command), two_targets, sizeof(two_targets) - 1);
  require(!iso14443a_in_auto_poll(1), "truncated second target record accepted");
  finish_case();
}

static void test_classic_smoke(void) {
  uint8_t block[16];
  uint8_t response[17] = {0};
  for (size_t i = 1; i < sizeof(response); i++) response[i] = (uint8_t)i;
  static const uint8_t read_command[] = {0x40, 1, 0x30, 4};
  uint8_t write_command[20] = {0x40, 1, 0xA0, 4};
  memcpy(write_command + 4, response + 1, 16);

  begin_case("classic read and write consume complete replies");
  expect_exchange(read_command, sizeof(read_command), response, sizeof(response));
  expect_exchange(write_command, sizeof(write_command), status_ok, 1);
  require(mfc_read_data_block(4, block) && memcmp(block, response + 1, 16) == 0,
          "classic block read failed");
  require(mfc_write_data_block(4, block), "classic block write failed");
  finish_case();

  begin_case("classic write rejects operation failure");
  const uint8_t error[] = {0x14};
  expect_exchange(write_command, sizeof(write_command), error, sizeof(error));
  require(!mfc_write_data_block(4, block), "classic write accepted card error");
  finish_case();

  begin_case("classic short read preserves output");
  memset(block, 0xA5, sizeof(block));
  expect_exchange(read_command, sizeof(read_command), response, 16);
  require(!mfc_read_data_block(4, block), "classic short read accepted");
  for (size_t i = 0; i < sizeof(block); i++) require(block[i] == 0xA5, "short read changed block");
  finish_case();
}

int main(void) {
  test_name = "startup";
  test_write_completion();
  test_transport_failures();
  test_invalid_frames();
  test_irq_readiness();
  test_read_outputs();
  test_counter_increment();
  test_resynchronization();
  test_startup_and_selection();
  test_deselect_and_autopoll();
  test_classic_smoke();
  printf("PN532 host regression tests passed: %u cases (production driver, ASan/UBSan).\n",
         cases_run);
  return 0;
}
