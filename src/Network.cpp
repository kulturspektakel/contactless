#include "Network.h"
#include "StateManager.h"

extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;

Network::Network(StateManager& sm) : stateManager(sm) {}

Network::~Network() {}

void Network::setup() {
  if (!WiFi.mode(WIFI_STA)) {
    Serial.println("[NETWORK] Failed to activate Station Mode");
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void Network::loop() {
  wl_status_t newWifiStatus = WiFi.status();

  if ((newWifiStatus == WL_DISCONNECTED &&
       nextReconnect < millis()) ||  // try after time
      (wifiStatus == WL_CONNECTED &&
       newWifiStatus != WL_CONNECTED)  // just disconnected
  ) {
    nextReconnect = millis() + RECONNECT_INTERVAL;
    tryToConnect();
  }

  wifiStatus = newWifiStatus;
}

void Network::tryToConnect() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NETWORK] Try connecting to network");
    WiFi.reconnect();
  } else {
    Serial.print("[NETWORK] Connected to ");
    Serial.println(WIFI_SSID);
  }
}
