#include <Arduino.h>

#define IR_PIN1 1        // PA1
#define IR_PIN2 2        // PA2
#define STATUS_LED_PIN 3 // PA3
#define RACER_ID 0       // ID 0 - 7

// 38kHz carrier: period = 26.3μs, half-period = 13.15μs
// ATtiny402 typically runs at 20MHz by default
// delayMicroseconds() handles the timing
#define CARRIER_HALF_PERIOD 13

void setup()
{
  pinMode(IR_PIN1, OUTPUT);
  pinMode(IR_PIN2, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);

  digitalWrite(IR_PIN1, LOW);
  digitalWrite(IR_PIN2, LOW);
  digitalWrite(STATUS_LED_PIN, HIGH); //we booted and have power
}


// Generate 38kHz carrier for specified microseconds
void burstIR(uint16_t micros) {
  uint16_t cycles = micros / 26; // ~38kHz cycles to generate

  for(uint16_t i = 0; i < cycles; i++) {
    digitalWrite(IR_PIN1, HIGH);
    digitalWrite(IR_PIN2, HIGH);
    delayMicroseconds(CARRIER_HALF_PERIOD);
    digitalWrite(IR_PIN1, LOW);
    digitalWrite(IR_PIN2, LOW);
    delayMicroseconds(CARRIER_HALF_PERIOD);
  }
}

// Gap with no carrier
void gapIR(uint16_t micros) {
  digitalWrite(IR_PIN1, LOW);
  digitalWrite(IR_PIN2, LOW);
  delayMicroseconds(micros);
}

// Send sync burst (unique long gap)
void sendSync() {
  burstIR(270);  // Minimum safe burst
  gapIR(900);    // Unique long gap - never in data
}

// Send a data bit
void sendBit(uint8_t bit) {
  burstIR(270);  // Minimum burst
  if(bit) {
    gapIR(600);  // Long gap = 1
  } else {
    gapIR(300);  // Short gap = 0
  }
}

// Send complete racer ID packet
void sendPacket(uint8_t id) {
  sendSync();
  sendBit((id >> 2) & 1);  // Bit 2 (MSB)
  sendBit((id >> 1) & 1);  // Bit 1
  sendBit(id & 1);          // Bit 0 (LSB)
  // No inter-packet gap - continuous transmission
}

void loop() {
  // Continuously transmit racer ID back-to-back
  sendPacket(RACER_ID);
}