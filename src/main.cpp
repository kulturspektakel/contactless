#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include "Buzzer.h"
#include "Card.h"
#include "Config.h"
#include "Constants.h"
#include "Display.h"
#include "Keypad.h"
#include "Logger.h"
#include "Network.h"
#include "StateManager.h"

sdfat::SdFat sdCard;
StateManager stateManager;
Keypad* keypad = new Keypad(stateManager, 0x20);
Display* display = new Display(stateManager, 0x27);
Buzzer* buzzer = new Buzzer(0);
Logger* logger = new Logger(stateManager, sdCard);
Card* card = new Card(stateManager, 15, 2);
Config* config = new Config(stateManager, sdCard);
Network* network = new Network(stateManager);

void setup() {
  Serial.begin(9600);
  Serial.println("SPI.begin");
  SPI.begin();

  stateManager.setup(display, buzzer, logger, network, config);
  card->setup();
  display->setup();
  logger->setup();
  keypad->setup();
  buzzer->setup();
  network->setup();

  // needs to be after card setup, for SPI bus to be free
  if (!sdCard.begin(10 /* , SPI_QUARTER_SPEED */)) {
    Serial.println("[LOGGER] SD card failed");
    display->message("SD-Karte fehlt", 500);
    while (true) {
    }  // block execution
  }

  config->setup();
}

void loop() {
  keypad->loop();
  display->loop();
  buzzer->loop();
  network->loop();
  logger->loop();
  config->loop();
  card->loop();  // must be after logger, because of
                 // Logger::numberPendingUploads
}
