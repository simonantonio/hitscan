#include <Arduino.h>
#include "RaceTimerSystem.hpp"

// Pin definitions
#define IR_PIN 23
#define LED_PIN 25
#define I2S_BCK 26
#define I2S_WS 27
#define I2S_DATA 14

// WiFi credentials
const char *AP_SSID = "RaceTimer-01";  // Direct connection AP
const char *AP_PASSWORD = "racing123"; // AP password (min 8 chars)
const char *STA_SSID = "YOUR_AP";       // Optional: your home WiFi
const char *STA_PASSWORD = "YOUR_PASSWORD"; // Optional: home WiFi password

RaceTimerSystem raceTimer(IR_PIN, LED_PIN, I2S_BCK, I2S_WS, I2S_DATA);

void heartBeat()
{
  pinMode(2, OUTPUT);

// thump
for (int i = 0; i <= 255; i += 5) {
  analogWrite(2, i);
  delay(6);
}

// slight relax
for (int i = 255; i >= 80; i -= 5) {
  analogWrite(2, i);
  delay(6);
}

delay(120);

// fade out
for (int i = 80; i >= 0; i -= 5) {
  analogWrite(2, i);
  delay(8);
}
}

void setup()
{
  heartBeat();
  heartBeat();
  heartBeat();
  // Start in AP mode, optionally connect to home WiFi
  // To use AP-only mode, pass nullptr for STA credentials:
  // raceTimer.begin(AP_SSID, AP_PASSWORD);
  raceTimer.begin(AP_SSID, AP_PASSWORD, STA_SSID, STA_PASSWORD);
}

void loop()
{
  raceTimer.update();
}