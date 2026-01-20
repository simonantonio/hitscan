#include <Arduino.h>

/*
 * ESP32 Race Timer System
 * Modern C++ single-file implementation
 *
 * Features:
 * - Fast IR detection (TSOP382)
 * - Web server for race management
 * - Audio feedback (MAX98357A)
 * - WS2812B LED ring status
 * - SD card logging
 */

// ============================================================================
// IR Detector Class
// ============================================================================
class IRRacerDetector {
private:
    const uint8_t irPin;

    // Timing constants (±30% tolerance)
    static constexpr unsigned long SYNC_BURST_MIN = 190;
    static constexpr unsigned long SYNC_BURST_MAX = 350;
    static constexpr unsigned long SYNC_GAP_MIN = 630;
    static constexpr unsigned long SYNC_GAP_MAX = 1170;
    static constexpr unsigned long BIT_BURST_MIN = 190;
    static constexpr unsigned long BIT_BURST_MAX = 350;
    static constexpr unsigned long SHORT_GAP_MIN = 210;
    static constexpr unsigned long SHORT_GAP_MAX = 390;
    static constexpr unsigned long LONG_GAP_MIN = 420;
    static constexpr unsigned long LONG_GAP_MAX = 780;
    static constexpr unsigned long TIMEOUT_US = 2000;

    unsigned long measurePulse(bool level, unsigned long timeout) {
        unsigned long start = micros();
        while(digitalRead(irPin) == level) {
            if(micros() - start > timeout) return 0;
        }
        return micros() - start;
    }

    bool detectSync() {
        unsigned long timeout = millis() + 100;
        while(digitalRead(irPin) == HIGH) {
            if(millis() > timeout) return false;
        }

        unsigned long burst = measurePulse(LOW, TIMEOUT_US);
        if(burst < SYNC_BURST_MIN || burst > SYNC_BURST_MAX) return false;

        unsigned long gap = measurePulse(HIGH, TIMEOUT_US);
        if(gap < SYNC_GAP_MIN || gap > SYNC_GAP_MAX) return false;

        return true;
    }

    int readBit() {
        unsigned long burst = measurePulse(LOW, TIMEOUT_US);
        if(burst < BIT_BURST_MIN || burst > BIT_BURST_MAX) return -1;

        unsigned long gap = measurePulse(HIGH, TIMEOUT_US);

        if(gap >= SHORT_GAP_MIN && gap <= SHORT_GAP_MAX) return 0;
        if(gap >= LONG_GAP_MIN && gap <= LONG_GAP_MAX) return 1;

        return -1;
    }

public:
    IRRacerDetector(uint8_t pin) : irPin(pin) {}

    void begin() {
        pinMode(irPin, INPUT);
    }

    int decode() {
        if(!detectSync()) return -1;

        int bit2 = readBit();
        if(bit2 < 0) return -1;

        int bit1 = readBit();
        if(bit1 < 0) return -1;

        int bit0 = readBit();
        if(bit0 < 0) return -1;

        return (bit2 << 2) | (bit1 << 1) | bit0;
    }
};
