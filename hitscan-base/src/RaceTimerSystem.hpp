
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPIFFS.h>
#include <Adafruit_NeoPixel.h>
#include <ESPmDNS.h>
#include "IRRacerDetector.hpp"
#include "LEDRing.hpp"
#include "AudioPlayer.hpp"

// ============================================================================
// Race Timer System Class
// ============================================================================
class RaceTimerSystem {
private:
    IRRacerDetector detector;
    LEDRing leds;
    AudioPlayer audio;
    WebServer server;

    enum class Mode {
        RACE,      // Race mode - track positions
        LAP_TIMER  // Lap timer - just record all crossings
    };

    struct RaceResult {
        uint8_t racerId;
        unsigned long timestamp;
        uint8_t position;
    };

    struct LapTime {
        uint8_t racerId;
        unsigned long lapTime;
        unsigned long timestamp;
    };

    std::vector<RaceResult> results;
    std::vector<LapTime> laps;
    String racerNames[8] = {"Racer 0", "Racer 1", "Racer 2", "Racer 3",
                            "Racer 4", "Racer 5", "Racer 6", "Racer 7"};

    Mode currentMode = Mode::RACE;
    bool raceActive = false;
    unsigned long raceStartTime = 0;
    unsigned long lastDetectionTime[8] = {0}; // Non-blocking debounce per racer
    static constexpr unsigned long DEBOUNCE_MS = 200;

    void setupWebServer() {
        // Serve files from SPIFFS
        server.serveStatic("/", SPIFFS, "/index.html");
        server.serveStatic("/style.css", SPIFFS, "/style.css");
        server.serveStatic("/app.js", SPIFFS, "/app.js");
        server.serveStatic("/assets", SPIFFS, "/assets/");

        // API: Get current mode
        server.on("/mode", HTTP_GET, [this]() {
            String mode = (currentMode == Mode::RACE) ? "race" : "lap";
            server.send(200, "text/plain", mode);
        });

        // API: Set mode
        server.on("/mode", HTTP_POST, [this]() {
            if(server.hasArg("plain")) {
                String body = server.arg("plain");
                if(body == "race") {
                    currentMode = Mode::RACE;
                    server.send(200, "text/plain", "Mode set to RACE");
                } else if(body == "lap") {
                    currentMode = Mode::LAP_TIMER;
                    server.send(200, "text/plain", "Mode set to LAP TIMER");
                } else {
                    server.send(400, "text/plain", "Invalid mode");
                }
            }
        });

        // API: Get racer names
        server.on("/racers", HTTP_GET, [this]() {
            String json = "[";
            for(int i = 0; i < 8; i++) {
                if(i > 0) json += ",";
                json += "{\"id\":" + String(i) + ",\"name\":\"" + racerNames[i] + "\"}";
            }
            json += "]";
            server.send(200, "application/json", json);
        });

        // API: Set racer name
        server.on("/racers", HTTP_POST, [this]() {
            if(server.hasArg("plain")) {
                // Expected format: {"id":0,"name":"FastFlyer"}
                String body = server.arg("plain");
                int idStart = body.indexOf("\"id\":") + 5;
                int idEnd = body.indexOf(",", idStart);
                int id = body.substring(idStart, idEnd).toInt();

                int nameStart = body.indexOf("\"name\":\"") + 8;
                int nameEnd = body.indexOf("\"", nameStart);
                String name = body.substring(nameStart, nameEnd);

                if(id >= 0 && id < 8 && name.length() > 0) {
                    racerNames[id] = name;
                    server.send(200, "text/plain", "Racer name updated");
                } else {
                    server.send(400, "text/plain", "Invalid data");
                }
            }
        });

        // Start race
        server.on("/start", [this]() {
            startRace();
            server.send(200, "text/plain", "Race started");
        });

        // Stop race
        server.on("/stop", [this]() {
            stopRace();
            server.send(200, "text/plain", "Race stopped");
        });

        // Get results
        server.on("/results", [this]() {
            String json = "[";

            if(currentMode == Mode::RACE) {
                // Race mode - show positions
                for(size_t i = 0; i < results.size(); i++) {
                    if(i > 0) json += ",";
                    json += "{\"racer\":" + String(results[i].racerId) +
                           ",\"name\":\"" + racerNames[results[i].racerId] + "\"" +
                           ",\"time\":" + String(results[i].timestamp) +
                           ",\"position\":" + String(results[i].position) + "}";
                }
            } else {
                // Lap timer mode - show all laps
                for(size_t i = 0; i < laps.size(); i++) {
                    if(i > 0) json += ",";
                    json += "{\"racer\":" + String(laps[i].racerId) +
                           ",\"name\":\"" + racerNames[laps[i].racerId] + "\"" +
                           ",\"lapTime\":" + String(laps[i].lapTime) +
                           ",\"timestamp\":" + String(laps[i].timestamp) + "}";
                }
            }

            json += "]";
            server.send(200, "application/json", json);
        });

        server.begin();
    }

    void logToSD(const RaceResult& result) {
        // TODO: Implement SD card logging
        // File file = SD.open("/races.csv", FILE_APPEND);
        // file.printf("%d,%lu,%d\n", result.racerId, result.timestamp, result.position);
        // file.close();
        Serial.printf("LOG: Racer %d, Time %lu, Position %d\n",
                     result.racerId, result.timestamp, result.position);
    }

public:
    RaceTimerSystem(uint8_t irPin, uint8_t ledPin,
                   uint8_t i2sBck, uint8_t i2sWs, uint8_t i2sData)
        : detector(irPin), leds(ledPin), audio(i2sBck, i2sWs, i2sData), server(80) {}

    void begin(const char* apSSID, const char* apPassword,
               const char* staSSID = nullptr, const char* staPassword = nullptr) {
        Serial.begin(115200);
        Serial.println("Race Timer System Starting...");

        // Initialize SPIFFS
        if(!SPIFFS.begin(true)) {
            Serial.println("SPIFFS Mount Failed!");
            leds.setStatus(LEDRing::Status::ERROR);
            return;
        }
        Serial.println("SPIFFS mounted successfully");

        // List files in SPIFFS for debugging
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        Serial.println("Files in SPIFFS:");
        while(file) {
            Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
            file = root.openNextFile();
        }

        // Initialize components
        detector.begin();
        leds.begin();
        leds.setStatus(LEDRing::Status::IDLE);

        // Initialize SD card
        if(!SD.begin()) {
            Serial.println("SD Card init failed!");
            leds.setStatus(LEDRing::Status::ERROR);
        }

        // Setup WiFi in AP+STA mode
        WiFi.mode(WIFI_AP_STA);

        // Start Access Point
        WiFi.softAP(apSSID, apPassword);
        Serial.println("AP Started:");
        Serial.println("  SSID: " + String(apSSID));
        Serial.println("  IP: " + WiFi.softAPIP().toString());

        // Connect to external WiFi if credentials provided
        if(staSSID != nullptr && staPassword != nullptr) {
            WiFi.begin(staSSID, staPassword);
            Serial.print("Connecting to WiFi");
            int attempts = 0;
            while(WiFi.status() != WL_CONNECTED && attempts < 20) {
                delay(500);
                Serial.print(".");
                attempts++;
            }
            if(WiFi.status() == WL_CONNECTED) {
                Serial.println("\nSTA Connected: " + WiFi.localIP().toString());
            } else {
                Serial.println("\nSTA Connection failed, AP-only mode");
            }
        }

        // Setup mDNS for easy access (http://racetimer.local)
        if(MDNS.begin("racetimer")) {
            Serial.println("mDNS responder started: http://racetimer.local");
        }

        setupWebServer();

        Serial.println("System Ready!");
        Serial.println("Access via:");
        Serial.println("  AP: http://" + WiFi.softAPIP().toString());
        if(WiFi.status() == WL_CONNECTED) {
            Serial.println("  STA: http://" + WiFi.localIP().toString());
            Serial.println("  mDNS: http://racetimer.local");
        }

        leds.flash(0, 255, 0); // Green flash
        audio.playTone(1000, 100);
    }

    void startRace() {
        raceActive = true;
        raceStartTime = millis();
        results.clear();
        laps.clear();
        // Reset debounce timers
        for(int i = 0; i < 8; i++) {
            lastDetectionTime[i] = 0;
        }
        leds.setStatus(LEDRing::Status::DETECTING);
        audio.playTone(1000, 200);
        Serial.println("🏁 RACE STARTED!");
    }

    void stopRace() {
        raceActive = false;
        leds.setStatus(LEDRing::Status::IDLE);
        audio.playTone(500, 200);
        Serial.println("🏁 RACE STOPPED!");
    }

    void update() {
        // PRIORITY 1: Handle web requests (non-blocking)
        server.handleClient();

        // PRIORITY 2: IR Detection (critical timing)
        if(!raceActive) return;

        int racerId = detector.decode();

        if(racerId >= 0 && racerId <= 7) {
            unsigned long now = millis();
            unsigned long timestamp = now - raceStartTime;

            // Non-blocking debounce check
            if(now - lastDetectionTime[racerId] < DEBOUNCE_MS) {
                return; // Too soon, ignore
            }

            lastDetectionTime[racerId] = now;

            if(currentMode == Mode::RACE) {
                // Race mode - only count first crossing
                for(const auto& result : results) {
                    if(result.racerId == racerId) return; // Already finished
                }

                RaceResult result = {
                    static_cast<uint8_t>(racerId),
                    timestamp,
                    static_cast<uint8_t>(results.size() + 1)
                };

                results.push_back(result);

                Serial.printf("🏁 %s FINISHED! Position: %d, Time: %lu ms\n",
                             racerNames[racerId].c_str(), result.position, timestamp);

                logToSD(result);

            } else {
                // Lap timer mode - record every crossing
                unsigned long lapTime = timestamp;

                // Calculate lap time (time since last crossing)
                for(int i = laps.size() - 1; i >= 0; i--) {
                    if(laps[i].racerId == racerId) {
                        lapTime = timestamp - laps[i].timestamp;
                        break;
                    }
                }

                LapTime lap = {
                    static_cast<uint8_t>(racerId),
                    lapTime,
                    timestamp
                };

                laps.push_back(lap);

                Serial.printf("⏱️ %s LAP! Lap time: %lu ms, Total: %lu ms\n",
                             racerNames[racerId].c_str(), lapTime, timestamp);
            }

            // Visual/audio feedback (non-blocking)
            leds.setStatus(static_cast<LEDRing::Status>(static_cast<int>(LEDRing::Status::RACER_0) + racerId));
            audio.playTone(800 + (racerId * 100), 150);

            // Brief LED flash then back to detecting
            // Using millis()-based timing instead of delay
            static unsigned long ledChangeTime = 0;
            ledChangeTime = now;

            // Check if we should reset LED (in next loop iteration)
            if(now - ledChangeTime > 200) {
                leds.setStatus(LEDRing::Status::DETECTING);
            }
        }
    }
};