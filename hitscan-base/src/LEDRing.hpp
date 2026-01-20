
#include <Adafruit_NeoPixel.h>

// ============================================================================
// LED Ring Controller Class
// ============================================================================
class LEDRing
{
private:
    Adafruit_NeoPixel strip;
    unsigned long lastUpdate = 0;
    unsigned long pulseStart = 0;
    int pulsingRacer = -1;

    // Racer colors (RGB)
    struct Color
    {
        uint8_t r, g, b;
    };

    const Color racerColors[8] = {
        {255, 0, 0},   // 0: Red
        {0, 255, 0},   // 1: Green
        {0, 0, 255},   // 2: Blue
        {255, 255, 0}, // 3: Yellow
        {255, 0, 255}, // 4: Magenta
        {0, 255, 255}, // 5: Cyan
        {255, 128, 0}, // 6: Orange
        {128, 0, 255}  // 7: Purple
    };

public:
    enum class Status
    {
        IDLE,
        DETECTING,
        RACER_PULSE, // New: pulse effect for racer
        ERROR
    };

    LEDRing(uint8_t pin, uint16_t numLeds = 16)
        : strip(numLeds, pin, NEO_GRB + NEO_KHZ800) {}

    void begin()
    {
        strip.begin();
        strip.show();
        strip.setBrightness(50);
    }

    // Non-blocking update - call this in loop()
    void update()
    {
        unsigned long now = millis();

        // Update pulse effect
        if (pulsingRacer >= 0)
        {
            unsigned long elapsed = now - pulseStart;

            if (elapsed < 500)
            { // 500ms pulse
                // Breathing effect: fade in/out
                float phase = (elapsed % 250) / 250.0; // 0-1-0 over 250ms
                float intensity = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;

                Color c = racerColors[pulsingRacer];
                uint8_t r = c.r * intensity;
                uint8_t g = c.g * intensity;
                uint8_t b = c.b * intensity;

                // Spinning effect
                int offset = (elapsed / 20) % strip.numPixels();
                for (int i = 0; i < strip.numPixels(); i++)
                {
                    int pos = (i + offset) % strip.numPixels();
                    strip.setPixelColor(pos, strip.Color(r, g, b));
                }
                strip.show();
            }
            else
            {
                // Pulse finished, return to detecting
                pulsingRacer = -1;
                setStatus(Status::DETECTING);
            }
        }
    }

    void setStatus(Status status)
    {
        strip.clear();

        switch (status)
        {
        case Status::IDLE:
            setColor(0, 50, 100); // Dim blue
            break;
        case Status::DETECTING:
            setColor(0, 100, 0); // Dim green
            break;
        case Status::ERROR:
            flash(255, 0, 0, 3);
            break;
        default:
            break;
        }

        strip.show();
    }

    // Trigger racer pulse effect (non-blocking)
    void pulseRacer(uint8_t racerId)
    {
        if (racerId < 8)
        {
            pulsingRacer = racerId;
            pulseStart = millis();
        }
    }

    // Get racer color for web display
    uint32_t getRacerColor(uint8_t racerId)
    {
        if (racerId >= 8)
            return 0;
        Color c = racerColors[racerId];
        return (c.r << 16) | (c.g << 8) | c.b;
    }

    void setColor(uint8_t r, uint8_t g, uint8_t b)
    {
        for (int i = 0; i < strip.numPixels(); i++)
        {
            strip.setPixelColor(i, strip.Color(r, g, b));
        }
    }

    void flash(uint8_t r, uint8_t g, uint8_t b, int count = 3)
    {
        for (int i = 0; i < count; i++)
        {
            setColor(r, g, b);
            strip.show();
            delay(100); // Only used during startup, OK to block
            strip.clear();
            strip.show();
            delay(100);
        }
    }
};
