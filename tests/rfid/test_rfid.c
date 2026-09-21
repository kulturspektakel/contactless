/* Test the production RFID implementation, not a copy of its retry algorithm. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdk.h"
#include "../../main/rfid.c"

state_t current_state;
EventGroupHandle_t event_group;
const char SALT[SALT_LENGTH + 1] = "host-only-deterministic-test-salt";

static const uint8_t fixture_uid[7] = {4, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
static const char *case_name;
static unsigned cases;
static uint8_t tag_pages[32][4];
static uint8_t selected_uid[7];
static uint16_t physical_counter;
static unsigned counter_reads, page_reads, page_writes, increments, authentications;
static unsigned selections;
static unsigned reselections, increment_requests;
static uint64_t failed_counter_reads, failed_page_reads, failed_page_writes;
static bool fail_authentication, lost_increment_reply;
static bool fail_reselection, change_pending_on_failed_write, change_counter_after_pages;
static bool substitute_counter_readback;
static bool fail_verification_read;
static bool force_signature_collision;
static uint8_t selected_uid_length;
static uint8_t increment_values[16];
static uint8_t written_pages[64];
static uint8_t written_bytes[64][4];
static event_t terminal_event;
static jmp_buf task_exit;
static bool in_task;
static void put_card_payload(const ultralight_card_info_t *card);

#define CHECK(expression) do { \
  if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, case_name, #expression); \
    exit(1); \
  } \
} while (0)

static bool fails(uint64_t mask, unsigned call) {
  CHECK(call > 0 && call <= 64);
  return (mask & (UINT64_C(1) << (call - 1))) != 0;
}

/* A deterministic hash seam: signature/password plumbing is real, SHA-1 is not. */
void create_sha1_hash(const char *input, size_t length, uint8_t *output) {
  if (force_signature_collision) {
    memset(output, 0x5A, 20);
    return;
  }
  uint32_t hash = UINT32_C(2166136261);
  for (size_t i = 0; i < length; ++i) {
    hash = (hash ^ (uint8_t)input[i]) * UINT32_C(16777619);
  }
  for (size_t i = 0; i < 20; ++i) {
    hash ^= hash << 13;
    hash ^= hash >> 17;
    hash ^= hash << 5;
    output[i] = (uint8_t)hash;
  }
}

/* Standard Base64 at the SDK boundary, including mbedTLS's trailing NUL. */
static const char base64_digits[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int mbedtls_base64_encode(unsigned char *dst, size_t capacity, size_t *length,
                          const unsigned char *src, size_t count) {
  size_t required = ((count + 2) / 3) * 4;
  *length = required;
  if (capacity < required + 1) return -1;
  size_t out = 0;
  for (size_t i = 0; i < count; i += 3) {
    uint32_t bits = (uint32_t)src[i] << 16;
    if (i + 1 < count) bits |= (uint32_t)src[i + 1] << 8;
    if (i + 2 < count) bits |= src[i + 2];
    dst[out++] = base64_digits[(bits >> 18) & 63];
    dst[out++] = base64_digits[(bits >> 12) & 63];
    dst[out++] = i + 1 < count ? base64_digits[(bits >> 6) & 63] : '=';
    dst[out++] = i + 2 < count ? base64_digits[bits & 63] : '=';
  }
  dst[out] = 0;
  return 0;
}

int mbedtls_base64_decode(unsigned char *dst, size_t capacity, size_t *length,
                          const unsigned char *src, size_t count) {
  if (count == 0 || count % 4 != 0) return -1;
  size_t required = count / 4 * 3;
  if (src[count - 1] == '=') --required;
  if (src[count - 2] == '=') --required;
  *length = required;
  if (capacity < required) return -1;
  size_t out = 0;
  for (size_t i = 0; i < count; i += 4) {
    uint32_t bits = 0;
    for (size_t j = 0; j < 4; ++j) {
      const char *digit = strchr(base64_digits, src[i + j]);
      if (src[i + j] != '=' && (!digit || src[i + j] == 0)) return -1;
      bits = (bits << 6) | (src[i + j] == '=' ? 0 : (unsigned)(digit - base64_digits));
    }
    for (int shift = 16; shift >= 0 && out < required; shift -= 8) {
      dst[out++] = (uint8_t)(bits >> shift);
    }
  }
  return 0;
}

bool mfu_read_counter(uint8_t counter, uint16_t *value) {
  CHECK(counter == 0);
  ++counter_reads;
  if (fails(failed_counter_reads, counter_reads)) return false;
  *value = physical_counter;
  return true;
}

bool mfu_increment_counter(uint8_t counter, uint8_t value, uint32_t expected_value) {
  CHECK(counter == 0 && increments < sizeof(increment_values));
  ++increment_requests;
  if (expected_value != physical_counter) return false;
  increment_values[increments++] = value;
  physical_counter += value;
  if (lost_increment_reply) {
    lost_increment_reply = false;
    return false;
  }
  return true;
}

bool mfu_read_page(uint8_t page, uint8_t *buffer, uint8_t size) {
  CHECK(size <= 16 && (size_t)page * 4 + size <= sizeof(tag_pages));
  ++page_reads;
  if (fails(failed_page_reads, page_reads)) return false;
  if (fail_verification_read && increments > 0 && page == 8) {
    fail_verification_read = false;
    return false;
  }
  if (substitute_counter_readback && increments > 0 && page == 8) {
    substitute_counter_readback = false;
    ultralight_card_info_t replacement = current_state.data_to_write;
    physical_counter = ++replacement.data.regular.counter;
    put_card_payload(&replacement);
  }
  memcpy(buffer, (uint8_t *)tag_pages + page * 4, size);
  return true;
}

bool mfu_write_page(uint8_t page, uint8_t *bytes) {
  CHECK(page < 32 && page_writes < sizeof(written_pages));
  written_pages[page_writes] = page;
  memcpy(written_bytes[page_writes], bytes, 4);
  ++page_writes;
  if (fails(failed_page_writes, page_writes)) {
    if (change_pending_on_failed_write) {
      current_state.data_to_write.data.regular.balance = 500;
      current_state.data_to_write.data.regular.counter = 42;
      current_state.data_before_write.data.regular.counter = 41;
    }
    return false;
  }
  memcpy(tag_pages[page], bytes, 4);
  if (change_counter_after_pages && page_writes == 4) ++physical_counter;
  return true;
}

bool ntag2xx_authenticate(uint8_t *password, uint8_t *pack) {
  ++authentications;
  uint8_t expected_password[4], expected_pack[2];
  calculate_password(selected_uid, expected_password, expected_pack);
  if (fail_authentication || memcmp(password, expected_password, 4)) return false;
  memcpy(pack, expected_pack, 2);
  return true;
}

bool pn532_init(uint8_t sda, uint8_t scl, uint8_t reset, uint8_t irq, i2c_port_t port) {
  (void)sda; (void)scl; (void)reset; (void)irq; (void)port;
  return true;
}
bool pn532_sam_configuration(void) { return true; }
uint32_t pn532_get_firmware_version(void) { return UINT32_C(0x32010607); }
bool iso14443a_in_deselect(void) { return true; }
bool iso14443a_in_auto_poll(uint8_t period) { (void)period; return false; }
bool iso14443a_read_passive_target_id(uint8_t baud, uint8_t *uid, uint8_t *length,
                                     uint16_t timeout) {
  (void)baud;
  if (timeout == 0) {
    CHECK(in_task);
    if (++selections > 1) longjmp(task_exit, 2);
  } else {
    ++reselections;
    if (fail_reselection) return false;
  }
  memcpy(uid, selected_uid, sizeof(selected_uid));
  *length = selected_uid_length;
  return true;
}
bool mfc_authenticate_block(uint8_t *uid, uint8_t length, uint32_t block,
                            uint8_t key, uint8_t *bytes) {
  (void)uid; (void)length; (void)block; (void)key; (void)bytes;
  CHECK(false);
  return false;
}
bool mfc_read_data_block(uint8_t block, uint8_t *bytes) {
  (void)block; (void)bytes;
  CHECK(false);
  return false;
}
void vTaskDelay(TickType_t ticks) { (void)ticks; }
int64_t esp_timer_get_time(void) { return 1000000; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits) {
  (void)group;
  return bits;
}
card_error_t validate_values(int balance, int deposit) {
  return balance >= 0 && balance <= MAX_BALANCE && deposit >= 0 && deposit <= MAX_DEPOSIT
             ? NONE : TECHNICAL_ERROR;
}
void trigger_event(event_t event) {
  CHECK(event != FATAL_ERROR);
  if (event == WRITE_SUCCESSFUL || event == WRITE_UNSUCCESSFUL) {
    terminal_event = event;
    CHECK(in_task);
    longjmp(task_exit, 1);
  }
}

static void put_card_payload(const ultralight_card_info_t *card) {
  uint8_t encoded[PAYLOAD_BASE64_LENGTH + 2];
  ultralight_card_info_t copy = *card;
  CHECK(card->type == REGULAR ? regular_card_payload(&copy, encoded)
                             : crew_card_payload(&copy, encoded));
  memcpy(tag_pages[8], card->type == REGULAR ? "/$$/" : "/$c/", 4);
  memcpy(tag_pages[9], encoded, PAYLOAD_BASE64_LENGTH + 1);
}

/* Capture through production read_card, exactly as transaction creation does. */
static void save_baseline(event_t expected_status) {
  byte_array_t uid = {.length = LENGTH_ID};
  memcpy(uid.bytes, selected_uid, LENGTH_ID);
  CHECK(read_card(&uid) == expected_status);
  current_state.data_before_write = current_card;
  counter_reads = page_reads = 0;
}

static void install_baseline(void) {
  physical_counter = current_state.data_before_write.data.regular.counter;
  put_card_payload(&current_state.data_before_write);
  save_baseline(CARD_DETECTED_OK);
}

static void begin_case(const char *name) {
  case_name = name;
  ++cases;
  memset(&current_state, 0, sizeof(current_state));
  memset(&current_card, 0, sizeof(current_card));
  memset(tag_pages, 0, sizeof(tag_pages));
  memcpy(selected_uid, fixture_uid, sizeof(selected_uid));
  memcpy(current_card.id, fixture_uid, LENGTH_ID);
  current_card.type = REGULAR;
  current_card.data.regular.counter = 40;
  current_card.data.regular.balance = 2000;
  current_card.data.regular.deposit = 1;
  current_state.mode = WRITE_CARD;
  current_state.data_before_write = current_card;
  current_state.data_to_write = current_card;
  current_state.data_to_write.data.regular.counter = 41;
  current_state.data_to_write.data.regular.balance = 1700;
  physical_counter = 40;
  counter_reads = page_reads = page_writes = increments = authentications = selections = 0;
  reselections = increment_requests = 0;
  failed_counter_reads = failed_page_reads = failed_page_writes = 0;
  fail_authentication = lost_increment_reply = in_task = false;
  fail_reselection = change_pending_on_failed_write = change_counter_after_pages = false;
  substitute_counter_readback = fail_verification_read = false;
  force_signature_collision = false;
  selected_uid_length = sizeof(selected_uid);
  memset(increment_values, 0, sizeof(increment_values));
  memset(written_pages, 0, sizeof(written_pages));
  memset(written_bytes, 0, sizeof(written_bytes));
  terminal_event = FATAL_ERROR;
  put_card_payload(&current_card);
  save_baseline(CARD_DETECTED_OK);
  current_state.data_to_write = current_card;
  current_state.data_to_write.data.regular.counter = 41;
  current_state.data_to_write.data.regular.balance = 1700;
}

static int run_presentation(void) {
  selections = 0;
  in_task = true;
  int reason = setjmp(task_exit);
  if (reason == 0) rfid(NULL);
  in_task = false;
  return reason;
}

static void check_target_payload(void) {
  uint8_t encoded[PAYLOAD_BASE64_LENGTH + 2];
  CHECK(regular_card_payload(&current_state.data_to_write, encoded));
  CHECK(memcmp(tag_pages[9], encoded, PAYLOAD_BASE64_LENGTH + 1) == 0);
}

static bool attempt_write(void) {
  return write_card(&current_state.data_to_write, &current_state.data_before_write);
}

static void target_image(uint8_t image[28]) {
  uint8_t encoded[PAYLOAD_BASE64_LENGTH + 2];
  CHECK(regular_card_payload(&current_state.data_to_write, encoded));
  memcpy(image, "/$$/", 4);
  memcpy(image + 4, encoded, PAYLOAD_BASE64_LENGTH + 1);
}

static void expect_rejected_without_mutation(void) {
  uint8_t before[sizeof(tag_pages)];
  ultralight_card_info_t saved_baseline = current_state.data_before_write;
  memcpy(before, tag_pages, sizeof(before));
  CHECK(!attempt_write());
  CHECK(page_writes == 0 && increment_requests == 0 && increments == 0);
  CHECK(memcmp(before, tag_pages, sizeof(before)) == 0);
  CHECK(memcmp(&saved_baseline, &current_state.data_before_write, sizeof(saved_baseline)) == 0);
}

static void test_prewrite_guards(void) {
  begin_case("fresh counter read failure does not mutate card");
  failed_counter_reads = UINT64_MAX;
  CHECK(!attempt_write());
  CHECK(counter_reads == 1 && page_writes == 0 && increments == 0);
  CHECK(current_card.data.regular.counter == 40);

  begin_case("failed fresh read cannot manufacture a valid zero baseline");
  current_state.data_before_write.data.regular.counter = 0;
  current_state.data_to_write.data.regular.counter = 1;
  physical_counter = 0;
  failed_counter_reads = UINT64_MAX;
  CHECK(!attempt_write());
  CHECK(counter_reads == 1 && page_writes == 0 && increments == 0);

  begin_case("physical counter before transaction baseline is rejected before writes");
  physical_counter = 39;
  CHECK(!attempt_write());
  CHECK(counter_reads == 1 && page_writes == 0 && increments == 0);

  begin_case("physical counter beyond target is rejected before writes");
  physical_counter = 42;
  CHECK(!attempt_write());
  CHECK(counter_reads == 1 && page_writes == 0 && increments == 0);

  begin_case("target UID differs from observed card");
  selected_uid[6] ^= 1;
  CHECK(!attempt_write());
  CHECK(page_writes == 0 && increments == 0);

  begin_case("baseline UID differs from target");
  current_state.data_before_write.id[6] ^= 1;
  CHECK(!attempt_write());
  CHECK(page_writes == 0 && increments == 0);

  begin_case("target must be one transaction beyond baseline");
  current_state.data_to_write.data.regular.counter = 42;
  CHECK(!attempt_write());
  CHECK(page_writes == 0 && increments == 0);

  begin_case("stale observed counter cannot determine the increment");
  current_card.data.regular.counter = 3;
  CHECK(attempt_write());
  CHECK(counter_reads == 1 && increments == 1 && increment_values[0] == 1);
  CHECK(physical_counter == 41);
  check_target_payload();

  begin_case("already completed target needs no second increment");
  physical_counter = 41;
  put_card_payload(&current_state.data_to_write);
  CHECK(attempt_write());
  CHECK(counter_reads == 1 && increments == 0 && page_writes == 0);
  CHECK(physical_counter == 41);
  check_target_payload();

  begin_case("failed card reselection cannot write, authenticate, or increment");
  fail_reselection = true;
  CHECK(!attempt_write());
  CHECK(reselections == 1 && authentications == 0 && counter_reads == 0);
  CHECK(page_writes == 0 && increments == 0);

  begin_case("unsupported reselection UID length is rejected");
  selected_uid_length = 4;
  CHECK(!attempt_write());
  CHECK(authentications == 0 && page_writes == 0 && increments == 0);

  begin_case("regular target cannot use a crew transaction baseline");
  current_state.data_before_write.type = CREW;
  CHECK(!attempt_write());
  CHECK(reselections == 0 && page_writes == 0 && increments == 0);

  begin_case("wrapped 16-bit pending target is rejected before mutation");
  current_state.data_before_write.data.regular.counter = UINT16_MAX;
  current_state.data_to_write.data.regular.counter = 0;
  physical_counter = UINT16_MAX;
  CHECK(!attempt_write());
  CHECK(reselections == 0 && page_writes == 0 && increments == 0);

  begin_case("last representable normal transaction increments exactly once");
  current_state.data_before_write.data.regular.counter = UINT16_MAX - 1;
  current_state.data_to_write.data.regular.counter = UINT16_MAX;
  install_baseline();
  CHECK(attempt_write());
  CHECK(increments == 1 && increment_values[0] == 1 && physical_counter == UINT16_MAX);
  check_target_payload();

  begin_case("last representable completed transaction is retried without increment");
  current_state.data_before_write.data.regular.counter = UINT16_MAX - 1;
  current_state.data_to_write.data.regular.counter = UINT16_MAX;
  install_baseline();
  physical_counter = UINT16_MAX;
  put_card_payload(&current_state.data_to_write);
  CHECK(attempt_write());
  CHECK(increment_requests == 0 && physical_counter == UINT16_MAX);
  check_target_payload();

  begin_case("increment remains bound to preflight counter after page writes");
  change_counter_after_pages = true;
  CHECK(!attempt_write());
  CHECK(increment_requests == 1 && increments == 0 && physical_counter == 41);
  CHECK(attempt_write());
  CHECK(increment_requests == 1 && increments == 0 && physical_counter == 41);
  check_target_payload();
}

static void test_retry_loop(void) {
  begin_case("all attempt-local counter refreshes fail before any page write");
  failed_counter_reads = UINT64_MAX ^ UINT64_C(1); /* Initial card read succeeds. */
  CHECK(run_presentation() == 1 && terminal_event == WRITE_UNSUCCESSFUL);
  CHECK(counter_reads == 4 && page_writes == 0 && increments == 0);

  begin_case("lost increment reply then failed refresh cannot increment again");
  lost_increment_reply = true;
  failed_counter_reads = (UINT64_C(1) << 2) | (UINT64_C(1) << 3);
  CHECK(run_presentation() == 1 && terminal_event == WRITE_UNSUCCESSFUL);
  CHECK(counter_reads == 4 && page_writes == 4 && increments == 1);
  CHECK(physical_counter == 41 && increment_values[0] == 1);
  check_target_payload();

  /* Same saved transaction and physical tag, but newly readable on re-presentation. */
  case_name = "same target resumes after lost increment reply without double charging";
  ++cases;
  failed_counter_reads = 0;
  CHECK(run_presentation() == 1 && terminal_event == WRITE_SUCCESSFUL);
  CHECK(increments == 1 && physical_counter == 41 && page_writes == 4);
  CHECK(current_state.data_before_write.data.regular.counter == 40);
  CHECK(current_state.data_to_write.data.regular.counter == 41);
  check_target_payload();

  begin_case("partial payload write retries once against unchanged physical baseline");
  failed_page_writes = UINT64_C(1) << 2;
  CHECK(run_presentation() == 1 && terminal_event == WRITE_SUCCESSFUL);
  CHECK(page_writes == 7 && increments == 1 && increment_values[0] == 1);
  CHECK(physical_counter == 41 && counter_reads == 4);
  check_target_payload();

  begin_case("lost verification read after success never repeats increment");
  fail_verification_read = true;
  CHECK(run_presentation() == 1 && terminal_event == WRITE_SUCCESSFUL);
  CHECK(page_writes == 4 && increments == 1 && physical_counter == 41);
  check_target_payload();

  begin_case("wrong presented UID never writes saved transaction");
  selected_uid[6] ^= 1;
  CHECK(run_presentation() == 2);
  CHECK(page_writes == 0 && increments == 0 && authentications == 0);

  begin_case("ahead-of-target presentation does not damage payload on any retry");
  physical_counter = 42;
  uint8_t old_pages[sizeof(tag_pages)];
  memcpy(old_pages, tag_pages, sizeof(old_pages));
  CHECK(run_presentation() == 1 && terminal_event == WRITE_UNSUCCESSFUL);
  CHECK(counter_reads == 4 && page_writes == 0 && increments == 0);
  CHECK(memcmp(old_pages, tag_pages, sizeof(old_pages)) == 0);

  begin_case("retries retain the original baseline and target snapshots");
  failed_page_writes = UINT64_C(1) << 2;
  change_pending_on_failed_write = true;
  CHECK(run_presentation() == 1 && terminal_event == WRITE_SUCCESSFUL);
  CHECK(increments == 1 && physical_counter == 41);
  CHECK(current_card.data.regular.counter == 41 && current_card.data.regular.balance == 1700);
  CHECK(current_state.data_to_write.data.regular.counter == 42);
  CHECK(current_state.data_to_write.data.regular.balance == 500);

  begin_case("valid readback with a different counter is not transaction success");
  substitute_counter_readback = true;
  CHECK(run_presentation() == 1 && terminal_event == WRITE_UNSUCCESSFUL);
  CHECK(page_writes == 4 && increments == 1 && physical_counter == 42);

  begin_case("privileged repair may replace invalid signature from authorized baseline");
  current_state.transaction_type = LogMessage_CardTransaction_TransactionType_REPAIR;
  current_state.data_to_write.data.regular.balance = current_card.data.regular.balance;
  tag_pages[14][0] = tag_pages[14][0] == 'A' ? 'B' : 'A';
  save_baseline(CARD_DETECTED_SKIPPED_SECURITY);
  CHECK(run_presentation() == 1 && terminal_event == WRITE_SUCCESSFUL);
  CHECK(increments == 1 && physical_counter == 41);
  check_target_payload();
}

static void test_unaffected_paths(void) {
  begin_case("authentication failure prevents card writes and increments");
  fail_authentication = true;
  CHECK(!attempt_write());
  CHECK(page_writes == 0 && increments == 0);

  begin_case("regular initialization retries do not use payment counter refreshes");
  current_state.mode = WRITE_CARD_INITIALIZE;
  memset(tag_pages, 0, sizeof(tag_pages));
  failed_page_writes = UINT64_MAX; /* Stop before known, separate initialization buffer bug. */
  failed_counter_reads = UINT64_MAX;
  CHECK(run_presentation() == 1 && terminal_event == WRITE_UNSUCCESSFUL);
  CHECK(page_writes == 3 && counter_reads == 0 && increments == 0);
  CHECK(authentications == 0);

  begin_case("crew initialization retries never read or increment regular counters");
  current_card.type = CREW;
  current_card.data.crew.valid_until = 700;
  current_state.data_to_write = current_card;
  current_state.mode = WRITE_CARD_INITIALIZE;
  put_card_payload(&current_card);
  failed_page_writes = UINT64_MAX;
  failed_counter_reads = UINT64_MAX;
  CHECK(run_presentation() == 1 && terminal_event == WRITE_UNSUCCESSFUL);
  CHECK(page_writes == 3 && counter_reads == 0 && increments == 0);
  CHECK(authentications == 3);

  begin_case("ordinary crew write does not acquire regular counter guards");
  current_state.data_to_write.type = CREW;
  current_state.data_to_write.data.crew.valid_until = 701;
  memset(&current_state.data_before_write, 0, sizeof(current_state.data_before_write));
  failed_counter_reads = UINT64_MAX;
  CHECK(attempt_write());
  CHECK(counter_reads == 0 && increment_requests == 0 && page_writes == 4);

  begin_case("existing regular initialization authentication failure avoids counter retry reads");
  current_state.mode = WRITE_CARD_INITIALIZE;
  fail_authentication = true;
  CHECK(run_presentation() == 1 && terminal_event == WRITE_UNSUCCESSFUL);
  CHECK(authentications == 3 && counter_reads == 1 && page_writes == 0 && increments == 0);
}

static void test_payload_reconciliation(void) {
  begin_case("missing original raw snapshot fails before card I/O");
  current_state.data_before_write.raw_payload_valid = false;
  expect_rejected_without_mutation();
  CHECK(reselections == 0 && authentications == 0 && counter_reads == 0 && page_reads == 0);

  begin_case("exact target payload with baseline counter only finishes the increment");
  put_card_payload(&current_state.data_to_write);
  CHECK(attempt_write());
  CHECK(page_writes == 0 && increments == 1 && increment_values[0] == 1);
  CHECK(physical_counter == 41);
  check_target_payload();

  for (unsigned counter = 40; counter <= 41; ++counter) {
    char name[120];
    snprintf(name, sizeof(name), "valid unrelated balance at allowed counter %u is not overwritten", counter);
    begin_case(name);
    ultralight_card_info_t unrelated = current_state.data_to_write;
    unrelated.data.regular.counter = counter;
    unrelated.data.regular.balance = 900;
    physical_counter = counter;
    put_card_payload(&unrelated);
    CHECK(regular_payload_has_valid_signature(tag_pages[8], selected_uid));
    expect_rejected_without_mutation();

    snprintf(name, sizeof(name), "valid unrelated deposit at allowed counter %u is not overwritten", counter);
    begin_case(name);
    unrelated = current_state.data_to_write;
    unrelated.data.regular.counter = counter;
    unrelated.data.regular.deposit = 2;
    physical_counter = counter;
    put_card_payload(&unrelated);
    CHECK(regular_payload_has_valid_signature(tag_pages[8], selected_uid));
    expect_rejected_without_mutation();
  }

  for (unsigned read = 1; read <= 2; ++read) {
    char name[100];
    snprintf(name, sizeof(name), "fresh payload read %u fails before writes or increment", read);
    begin_case(name);
    failed_page_reads = UINT64_C(1) << (read - 1);
    expect_rejected_without_mutation();
    CHECK(counter_reads == 1 && page_reads == read);
  }

  begin_case("changed prefix is not accepted as an interrupted write");
  tag_pages[8][2] = 'c';
  expect_rejected_without_mutation();

  begin_case("changed immutable UID bytes are not accepted as an interrupted write");
  tag_pages[9][0] = tag_pages[9][0] == 'A' ? 'B' : 'A';
  expect_rejected_without_mutation();

  begin_case("unknown torn bytes within one page fail closed");
  uint8_t intended[28];
  target_image(intended);
  memcpy(tag_pages[11], intended + 12, 4);
  tag_pages[12][1] = '!';
  expect_rejected_without_mutation();

  begin_case("changed terminator does not become an intended target");
  put_card_payload(&current_state.data_to_write);
  physical_counter = 41;
  tag_pages[14][3] = 0;
  expect_rejected_without_mutation();

  /* Deliberately collide test signatures to exercise the independent signed-state guard. */
  begin_case("self-consistently signed prefix mixture is rejected using its stored counter");
  force_signature_collision = true;
  install_baseline();
  target_image(intended);
  memcpy(tag_pages[11], intended + 12, 4);
  CHECK(memcmp(tag_pages[8], intended, sizeof(intended)) != 0);
  CHECK(memcmp(tag_pages[8], current_state.data_before_write.raw_payload, sizeof(intended)) != 0);
  CHECK(regular_payload_has_valid_signature(tag_pages[8], selected_uid));
  expect_rejected_without_mutation();
}

static void test_every_interruption_boundary(void) {
  for (unsigned completed_pages = 0; completed_pages <= 4; ++completed_pages) {
    char name[120];
    snprintf(name, sizeof(name), "baseline counter resumes ordered prefix of %u completed pages", completed_pages);
    begin_case(name);
    uint8_t intended[28];
    target_image(intended);
    memcpy(tag_pages[11], intended + 12, completed_pages * 4);
    CHECK(attempt_write());
    CHECK(page_writes == (completed_pages == 4 ? 0 : 4));
    CHECK(increments == 1 && increment_values[0] == 1 && physical_counter == 41);
    check_target_payload();

    snprintf(name, sizeof(name), "target counter accepts only completed image, prefix %u", completed_pages);
    begin_case(name);
    target_image(intended);
    memcpy(tag_pages[11], intended + 12, completed_pages * 4);
    physical_counter = 41;
    if (completed_pages == 4) {
      CHECK(attempt_write());
      CHECK(page_writes == 0 && increment_requests == 0 && physical_counter == 41);
    } else {
      expect_rejected_without_mutation();
    }
  }

  for (unsigned mask = 1; mask < 15; ++mask) {
    if (mask == 1 || mask == 3 || mask == 7) continue; /* Ordered prefixes tested above. */
    char name[120];
    snprintf(name, sizeof(name), "out-of-order old/new page mixture mask 0x%X is rejected", mask);
    begin_case(name);
    uint8_t intended[28];
    target_image(intended);
    for (unsigned page = 0; page < 4; ++page) {
      CHECK(memcmp(tag_pages[11 + page], intended + 12 + page * 4, 4) != 0);
      if (mask & (1U << page)) memcpy(tag_pages[11 + page], intended + 12 + page * 4, 4);
    }
    expect_rejected_without_mutation();
  }
}

static void test_raw_repair_snapshots(void) {
  begin_case("raw snapshot preserves invalid characters before decoder normalization");
  tag_pages[14][0] = '!';
  uint8_t original[28];
  memcpy(original, tag_pages[8], sizeof(original));
  save_baseline(CARD_DETECTED_SKIPPED_SECURITY);
  CHECK(current_card.raw_payload_valid && current_state.data_before_write.raw_payload_valid);
  CHECK(memcmp(current_card.raw_payload, original, sizeof(original)) == 0);
  CHECK(memcmp(current_state.data_before_write.raw_payload, original, sizeof(original)) == 0);
  CHECK(current_card.raw_payload[24] == '!' && current_card.raw_payload[27] == 0xFE);

  begin_case("authorized repair preserves original damaged image during partial retry");
  tag_pages[14][0] = '!';
  save_baseline(CARD_DETECTED_SKIPPED_SECURITY);
  current_state.transaction_type = LogMessage_CardTransaction_TransactionType_REPAIR;
  current_state.data_to_write = current_card;
  current_state.data_to_write.data.regular.counter++;
  failed_page_writes = UINT64_C(1) << 2;
  CHECK(run_presentation() == 1 && terminal_event == WRITE_SUCCESSFUL);
  CHECK(increments == 1 && physical_counter == 41 && page_writes == 7);
  CHECK(current_state.data_before_write.raw_payload[24] == '!');
  check_target_payload();

  begin_case("repair does not adopt changed damage that normalizes to the same characters");
  tag_pages[14][0] = '!';
  save_baseline(CARD_DETECTED_SKIPPED_SECURITY);
  tag_pages[14][0] = '?';
  expect_rejected_without_mutation();

  begin_case("repair retains raw payload counter mismatch instead of reconstructing baseline");
  physical_counter = 43;
  save_baseline(CARD_DETECTED_SKIPPED_SECURITY);
  CHECK(current_state.data_before_write.data.regular.counter == 43);
  current_state.data_to_write = current_card;
  current_state.data_to_write.data.regular.counter = 44;
  CHECK(attempt_write());
  CHECK(increments == 1 && physical_counter == 44 && page_writes == 4);
  check_target_payload();

  begin_case("unrelated same-counter card is not adopted as baseline on repeated presentation");
  ultralight_card_info_t saved_baseline = current_state.data_before_write;
  ultralight_card_info_t unrelated = current_state.data_to_write;
  unrelated.data.regular.balance = 900;
  physical_counter = 41;
  put_card_payload(&unrelated);
  for (unsigned presentation = 0; presentation < 2; ++presentation) {
    CHECK(run_presentation() == 1 && terminal_event == WRITE_UNSUCCESSFUL);
    CHECK(page_writes == 0 && increment_requests == 0 && physical_counter == 41);
    CHECK(memcmp(&saved_baseline, &current_state.data_before_write, sizeof(saved_baseline)) == 0);
  }
}

int main(void) {
  test_prewrite_guards();
  test_retry_loop();
  test_unaffected_paths();
  test_payload_reconciliation();
  test_every_interruption_boundary();
  test_raw_repair_snapshots();
  printf("RFID host regression tests passed: %u cases (production RFID code, ASan/UBSan).\n", cases);
  return 0;
}
