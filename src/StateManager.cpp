#include "StateManager.h"
#include <ESP8266WiFi.h>
#include <TimeLib.h>

extern const int YEAR;
extern const int TOKEN_VALUE;
extern const int TIMEZONE_OFFSET_MINUTES;

StateManager::StateManager() {}

void StateManager::setup(Display* d,
                         Buzzer* b,
                         Logger* l,
                         Network* n,
                         Config* c) {
  display = d;
  buzzer = b;
  logger = l;
  network = n;
  config = c;
}

void StateManager::stateUpdated() {
  display->updateScreen(true);
  buzzer->beep(50);
}

void StateManager::error(const char* message) {
  buzzer->beep(1000);
  display->message(message, 5000);
}

void StateManager::showBalance() {
  display->showBalance(3000);
  buzzer->beep(200);
  char message[8];
  sprintf(message, "%d,%d", state.transaction.balanceBefore,
          state.transaction.tokensBefore);
}

void StateManager::chargeCard(int newValue, int newTokens) {
  state.transaction.balanceAfter = newValue;
  state.transaction.tokensAfter = newTokens;
  logger->log();
  // update for display
  state.transaction.balanceBefore = newValue;
  state.transaction.tokensBefore = newTokens;
  display->showBalance(3000);
  resetState();
  buzzer->beep(200);
}

void StateManager::cashoutCard() {
  int cashoutValue = state.transaction.balanceBefore +
                     state.transaction.tokensBefore * TOKEN_VALUE;
  state.transaction.tokensAfter = 0;
  state.transaction.balanceAfter = 0;
  logger->log();
  display->showCashout(cashoutValue);
  resetState();
  state.transaction.mode = TransactionMessage_Mode_TOP_UP;
  buzzer->beep(200);
}

void StateManager::cardInitialized() {
  display->message("OK", 1000);
  buzzer->beep(200);
}

void StateManager::receivedTime(const char* time, bool isUTC) {
  strncpy(state.entry, time, 9);

  int minutes = atoi(state.entry + 6);
  state.entry[6] = '\0';
  int hours = atoi(state.entry + 4);
  state.entry[4] = '\0';
  int month = atoi(state.entry + 2);
  state.entry[2] = '\0';
  int day = atoi(state.entry);
  state.entry[0] = '\0';

  setTime(hours, minutes,
          0,  // seconds
          day, month, YEAR);

  if (isUTC) {
    adjustTime(TIMEZONE_OFFSET_MINUTES * 60);
  }

  if (state.transaction.mode == TransactionMessage_Mode_TIME_ENTRY) {
    state.transaction.mode = TransactionMessage_Mode_CHARGE;
  }

  display->updateScreen(true);
}

void StateManager::toggleChargerMode() {
  state.transaction.mode =
      state.transaction.mode == TransactionMessage_Mode_TOP_UP
          ? TransactionMessage_Mode_CHARGE
          : TransactionMessage_Mode_TOP_UP;
  resetState();
  display->updateScreen(true);
  buzzer->beep(50);
  delay(2000);  // prevent rapid mode switching while key is present
}

bool StateManager::updateConfig(uint8_t* buffer, size_t len) {
  int32_t oldChecksum = state.config.checksum;
  pb_istream_t stream = pb_istream_from_buffer(buffer, len);
  pb_decode(&stream, ConfigMessage_fields, &state.config);
  if (strlen(state.config.name) > 0) {
    strlcpy(state.transaction.list_name, state.config.name, 17);
    state.transaction.has_list_name = true;
  }
  if (state.config.checksum == oldChecksum) {
    return false;
  }
  welcome();
  state.manualEntry = state.config.price1 == 0;
  // display->updateScreen(false); not sure if needed
  return true;
}

void StateManager::welcome() {
  char message[] = "                                ";  // 32 spaces
  strncpy(message, WiFi.macAddress().substring(9).c_str(), 8);
  snprintf(&message[16], 16, "%s", state.config.name);
  display->message(message, 2000);
}

void StateManager::selectProduct(int index) {
  int productPrice = 0;
  char* proudctName = nullptr;
  switch (index) {
    case 0:
      productPrice = state.config.price1;
      proudctName = state.config.product1;
      break;
    case 1:
      productPrice = state.config.price2;
      proudctName = state.config.product2;
      break;
    case 2:
      productPrice = state.config.price3;
      proudctName = state.config.product3;
      break;
    case 3:
      productPrice = state.config.price4;
      proudctName = state.config.product4;
      break;
    case 4:
      productPrice = state.config.price5;
      proudctName = state.config.product5;
      break;
    case 5:
      productPrice = state.config.price6;
      proudctName = state.config.product6;
      break;
    case 6:
      productPrice = state.config.price7;
      proudctName = state.config.product7;
      break;
    case 7:
      productPrice = state.config.price8;
      proudctName = state.config.product8;
      break;
    case 8:
      productPrice = state.config.price9;
      proudctName = state.config.product9;
      break;
  }

  if (productPrice > 0 && state.value + productPrice < 10000) {
    state.value += productPrice;
    display->messageWithPrice(proudctName, productPrice, 1000);
    // Cart is limited to 10 items, more will not be logged
    if (state.transaction.cart_items_count < 10) {
      state.transaction.cart_items[state.transaction.cart_items_count].price =
          productPrice;
      strlcpy(state.transaction.cart_items[state.transaction.cart_items_count]
                  .product,
              proudctName, 16);
      state.transaction.cart_items_count++;
    }
  }
}

void StateManager::resetState() {
  state.value = 0;
  state.tokens = 0;
  TransactionMessage_Mode mode = state.transaction.mode;
  TransactionMessage newTransaction = TransactionMessage_init_default;
  state.transaction = newTransaction;
  if (strlen(state.config.name) > 0) {
    strlcpy(state.transaction.list_name, state.config.name, 17);
    state.transaction.has_list_name = true;
  }
  state.transaction.mode = mode;
  state.manualEntry = state.config.price1 == 0;
}

void StateManager::showDebug(bool startDebugMode) {
  display->showDebug(startDebugMode,
                     logger->numberPendingUploadsCached(startDebugMode));
  buzzer->beep(50);
}

void StateManager::clearCart() {
  CartItemMessage emptyCartItem = CartItemMessage_init_default;
  for (int i = 0; i < 10; i++) {
    state.transaction.cart_items[0] = emptyCartItem;
  }
  state.transaction.cart_items_count = 0;
}

void StateManager::reload() {
  network->tryToConnect();
  config->resetRetryCounter();
  config->updateSoftware();
}
