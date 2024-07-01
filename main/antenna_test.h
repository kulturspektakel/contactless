#pragma once

typedef enum {
  ANTRNNA_TEST_RUNNING = -4,
  ANTENNA_TEST_FAILED = -3,
  ANTENNA_TEST_TOO_LOW = -2,
  ANTENNA_TEST_NOT_STARTED = -1,
  ANTENNA_TEST_45MA,
  ANTENNA_TEST_60MA,
  ANTENNA_TEST_75MA,
  ANTENNA_TEST_90MA,
  ANTENNA_TEST_105MA,
  ANTENNA_TEST_120MA,
  ANTENNA_TEST_130MA,
  ANTENNA_TEST_150MA,
  ANTENNA_TEST_TOO_HIGH,

} antenna_test_state_t;

extern antenna_test_state_t antenna_test_status;
void antenna_test(void* params);