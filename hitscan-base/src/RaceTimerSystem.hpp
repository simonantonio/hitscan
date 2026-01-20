
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPIFFS.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include "IRRacerDetector.hpp"
#include "LEDRing.hpp"
#include "AudioPlayer.hpp"

// ============================================================================
// Race Timer System Class
// ============================================================================
class RaceTimerSystem
{
private:
    IRRacerDetector detector;
    LEDRing leds;
    AudioPlayer audio;
    WebServer server;

    // FreeRTOS task handle for Core 1 detection
    TaskHandle_t detectionTaskHandle = NULL;

    // Thread-safe queue for detection events
    QueueHandle_t detectionQueue;

    struct DetectionEvent
    {
        uint8_t racerId;
        unsigned long timestamp;
    };

    enum class Mode
    {
        RACE,     // Race mode - track positions
        LAP_TIMER // Lap timer - just record all crossings
    };

    struct RaceResult
    {
        uint8_t racerId;
        unsigned long timestamp;
        uint8_t position;
    };

    struct LapTime
    {
        uint8_t racerId;
        unsigned long lapTime;
        unsigned long timestamp;
    };

    std::vector<RaceResult> results;
    std::vector<LapTime> laps;
    String racerNames[8] = {"Racer 0", "Racer 1", "Racer 2", "Racer 3",
                            "Racer 4", "Racer 5", "Racer 6", "Racer 7"};

    unsigned long fastestLap = 0xFFFFFFFF; // Track overall fastest
    uint8_t fastestLapRacer = 0;
    unsigned long personalBest[8] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
                                     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

    Mode currentMode = Mode::RACE;
    bool raceActive = false;
    unsigned long raceStartTime = 0;
    unsigned long lastDetectionTime[8] = {0}; // Non-blocking debounce per racer
    static constexpr unsigned long DEBOUNCE_MS = 200;

    // Core 1 detection task (runs independently)
    static void detectionTask(void *parameter)
    {
        RaceTimerSystem *timer = (RaceTimerSystem *)parameter;

        Serial.println("Detection task started on Core 1");

        while (true)
        {
            if (timer->raceActive)
            {
                // CRITICAL: This runs uninterrupted on Core 1
                int racerId = timer->detector.decode();

                if (racerId >= 0 && racerId <= 7)
                {
                    unsigned long now = millis();

                    // Check debounce
                    if (now - timer->lastDetectionTime[racerId] >= DEBOUNCE_MS)
                    {
                        timer->lastDetectionTime[racerId] = now;

                        // Send detection event to Core 0 via queue
                        DetectionEvent event = {
                            static_cast<uint8_t>(racerId),
                            now - timer->raceStartTime};

                        xQueueSend(timer->detectionQueue, &event, 0);

                        Serial.printf("[Core 1] Detected Racer %d at %lu ms\n",
                                      racerId, event.timestamp);
                    }
                }
            }

            // Tiny yield to prevent watchdog timeout
            vTaskDelay(1); // 1ms
        }
    }

    void setupWebServer()
    {
        // Serve files from SPIFFS
        server.serveStatic("/", SPIFFS, "/index.html");
        server.serveStatic("/style.css", SPIFFS, "/style.css");
        server.serveStatic("/app.js", SPIFFS, "/app.js");
        server.serveStatic("/assets", SPIFFS, "/assets/");

        // API: Get current mode
        server.on("/mode", HTTP_GET, [this]()
                  {
            String mode = (currentMode == Mode::RACE) ? "race" : "lap";
            server.send(200, "text/plain", mode); });

        // API: Set mode
        server.on("/mode", HTTP_POST, [this]()
                  {
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
            } });

        // API: Get racer names
        server.on("/racers", HTTP_GET, [this]()
                  {
            String json = "[";
            for(int i = 0; i < 8; i++) {
                if(i > 0) json += ",";
                json += "{\"id\":" + String(i) +
                       ",\"name\":\"" + racerNames[i] + "\"" +
                       ",\"color\":\"#" + String(leds.getRacerColor(i), HEX) + "\"" +
                       ",\"pb\":" + String(personalBest[i] == 0xFFFFFFFF ? 0 : personalBest[i]) + "}";
            }
            json += "]";
            server.send(200, "application/json", json); });

        // API: Set racer name
        server.on("/racers", HTTP_POST, [this]()
                  {
            if(server.hasArg("plain")) {
                String body = server.arg("plain");
                int idStart = body.indexOf("\"id\":") + 5;
                int idEnd = body.indexOf(",", idStart);
                int id = body.substring(idStart, idEnd).toInt();

                int nameStart = body.indexOf("\"name\":\"") + 8;
                int nameEnd = body.indexOf("\"", nameStart);
                String name = body.substring(nameStart, nameEnd);

                if(id >= 0 && id < 8 && name.length() > 0) {
                    racerNames[id] = name;
                    saveRacerNamesToEEPROM();
                    server.send(200, "text/plain", "Racer name updated");
                } else {
                    server.send(400, "text/plain", "Invalid data");
                }
            } });

        // API: Get fastest lap info
        server.on("/fastest", HTTP_GET, [this]()
                  {
            String json = "{";
            json += "\"overall\":" + String(fastestLap == 0xFFFFFFFF ? 0 : fastestLap) + ",";
            json += "\"racer\":" + String(fastestLapRacer) + ",";
            json += "\"name\":\"" + racerNames[fastestLapRacer] + "\"";
            json += "}";
            server.send(200, "application/json", json); });

        // Start race
        server.on("/start", [this]()
                  {
            startRace();
            server.send(200, "text/plain", "Race started"); });

        // Stop race
        server.on("/stop", [this]()
                  {
            stopRace();
            server.send(200, "text/plain", "Race stopped"); });

        // Get results
        server.on("/results", [this]()
                  {
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
            server.send(200, "application/json", json); });

        server.begin();
    }

    void logToSD(const RaceResult &result)
    {
        // Non-blocking SD logging using background write
        // TODO: Implement proper async SD writes
        // For now, quick write with minimal blocking
        Serial.printf("LOG: Racer %d, Time %lu, Position %d\n",
                      result.racerId, result.timestamp, result.position);

        // Quick SD write (keep file operations minimal)
        // File file = SD.open("/races.csv", FILE_APPEND);
        // if(file) {
        //     file.printf("%d,%lu,%d\n", result.racerId, result.timestamp, result.position);
        //     file.close();
        // }
    }

    void saveRacerNamesToEEPROM()
    {
        // Save all racer names to EEPROM
        EEPROM.begin(512);
        int addr = 0;

        for (int i = 0; i < 8; i++)
        {
            int len = racerNames[i].length();
            EEPROM.write(addr++, len); // Store length
            for (int j = 0; j < len && j < 30; j++)
            { // Max 30 chars per name
                EEPROM.write(addr++, racerNames[i][j]);
            }
        }

        EEPROM.commit();
        Serial.println("Racer names saved to EEPROM");
    }

    void loadRacerNamesFromEEPROM()
    {
        EEPROM.begin(512);
        int addr = 0;

        for (int i = 0; i < 8; i++)
        {
            int len = EEPROM.read(addr++);
            if (len > 0 && len < 30)
            { // Valid length
                String name = "";
                for (int j = 0; j < len; j++)
                {
                    name += (char)EEPROM.read(addr++);
                }
                racerNames[i] = name;
            }
            else
            {
                addr += 30; // Skip invalid data
            }
        }

        Serial.println("Racer names loaded from EEPROM");
    }

public:
    RaceTimerSystem(uint8_t irPin, uint8_t ledPin,
                    uint8_t i2sBck, uint8_t i2sWs, uint8_t i2sData)
        : detector(irPin), leds(ledPin), audio(i2sBck, i2sWs, i2sData), server(80)
    {

        // Create queue for detection events (max 10 events)
        detectionQueue = xQueueCreate(10, sizeof(DetectionEvent));
    }

    void begin(const char *apSSID, const char *apPassword,
               const char *staSSID = nullptr, const char *staPassword = nullptr)
    {
        Serial.begin(115200);
        Serial.println("Race Timer System Starting...");

        // Initialize SPIFFS
        if (!SPIFFS.begin(true))
        {
            Serial.println("SPIFFS Mount Failed!");
            leds.setStatus(LEDRing::Status::ERROR);
            return;
        }
        Serial.println("SPIFFS mounted successfully");

        // List files in SPIFFS for debugging
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        Serial.println("Files in SPIFFS:");
        while (file)
        {
            Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
            file = root.openNextFile();
        }

        // Initialize components
        detector.begin();
        leds.begin();
        leds.setStatus(LEDRing::Status::IDLE);

        // Load racer names from EEPROM
        loadRacerNamesFromEEPROM();

        // Initialize SD card
        if (!SD.begin())
        {
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
        if (staSSID != nullptr && staPassword != nullptr)
        {
            WiFi.begin(staSSID, staPassword);
            Serial.print("Connecting to WiFi");
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 20)
            {
                delay(500);
                Serial.print(".");
                attempts++;
            }
            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.println("\nSTA Connected: " + WiFi.localIP().toString());
            }
            else
            {
                Serial.println("\nSTA Connection failed, AP-only mode");
            }
        }

        // Setup mDNS for easy access (http://racetimer.local)
        if (MDNS.begin("racetimer"))
        {
            Serial.println("mDNS responder started: http://racetimer.local");
        }

        setupWebServer();

        // Start detection task on Core 1 (dedicated timing core)
        xTaskCreatePinnedToCore(
            detectionTask,        // Task function
            "IR_Detection",       // Name
            4096,                 // Stack size (bytes)
            this,                 // Parameters
            2,                    // Priority (high)
            &detectionTaskHandle, // Task handle
            1                     // Core 1 (app CPU)
        );

        Serial.println("System Ready!");
        Serial.println("Access via:");
        Serial.println("  AP: http://" + WiFi.softAPIP().toString());
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("  STA: http://" + WiFi.localIP().toString());
            Serial.println("  mDNS: http://racetimer.local");
        }
        Serial.println("IR Detection running on Core 1");
        Serial.println("Web/LED/Audio running on Core 0");

        leds.flash(0, 255, 0); // Green flash
        audio.playTone(1000, 100);
    }

    void startRace()
    {
        raceActive = true;
        raceStartTime = millis();
        results.clear();
        laps.clear();
        fastestLap = 0xFFFFFFFF;
        // Reset debounce timers but keep personal bests
        for (int i = 0; i < 8; i++)
        {
            lastDetectionTime[i] = 0;
        }
        leds.setStatus(LEDRing::Status::DETECTING);
        audio.playTone(1000, 100); // Shortened tone
        Serial.println("🏁 RACE STARTED!");
    }

    void stopRace()
    {
        raceActive = false;
        leds.setStatus(LEDRing::Status::IDLE);
        audio.playTone(500, 200);
        Serial.println("🏁 RACE STOPPED!");
    }

    void update()
    {
        // PRIORITY 1: Update LED animations (non-blocking)
        leds.update();

        // PRIORITY 2: Handle web requests (non-blocking)
        server.handleClient();

        // PRIORITY 3: IR Detection (critical timing - always active)
        if (!raceActive)
            return;

        int racerId = detector.decode();

        if (racerId >= 0 && racerId <= 7)
        {
            unsigned long now = millis();
            unsigned long timestamp = now - raceStartTime;

            // Non-blocking debounce check
            if (now - lastDetectionTime[racerId] < DEBOUNCE_MS)
            {
                return; // Too soon, ignore
            }

            lastDetectionTime[racerId] = now;

            if (currentMode == Mode::RACE)
            {
                // Race mode - only count first crossing
                for (const auto &result : results)
                {
                    if (result.racerId == racerId)
                        return; // Already finished
                }

                RaceResult result = {
                    static_cast<uint8_t>(racerId),
                    timestamp,
                    static_cast<uint8_t>(results.size() + 1)};

                results.push_back(result);

                Serial.printf("🏁 %s FINISHED! Position: %d, Time: %lu ms\n",
                              racerNames[racerId].c_str(), result.position, timestamp);

                logToSD(result);
            }
            else
            {
                // Lap timer mode - record every crossing
                unsigned long lapTime = timestamp;

                // Calculate lap time (time since last crossing)
                for (int i = laps.size() - 1; i >= 0; i--)
                {
                    if (laps[i].racerId == racerId)
                    {
                        lapTime = timestamp - laps[i].timestamp;
                        break;
                    }
                }

                // Track fastest lap
                if (lapTime < fastestLap && lapTime > 1000)
                { // Ignore < 1sec (likely errors)
                    fastestLap = lapTime;
                    fastestLapRacer = racerId;
                    Serial.printf("⚡ NEW FASTEST LAP! %s - %lu ms\n",
                                  racerNames[racerId].c_str(), lapTime);
                }

                // Track personal best
                if (lapTime < personalBest[racerId] && lapTime > 1000)
                {
                    personalBest[racerId] = lapTime;
                    Serial.printf("🏆 %s PERSONAL BEST! %lu ms\n",
                                  racerNames[racerId].c_str(), lapTime);
                }

                LapTime lap = {
                    static_cast<uint8_t>(racerId),
                    lapTime,
                    timestamp};

                laps.push_back(lap);

                Serial.printf("⏱️ %s LAP! Lap: %lu ms, Total: %lu ms\n",
                              racerNames[racerId].c_str(), lapTime, timestamp);
            }

            // Visual/audio feedback (non-blocking)
            leds.pulseRacer(racerId);                   // Trigger pulse animation
            audio.playTone(800 + (racerId * 100), 100); // Quick tone
        }
    }
};
