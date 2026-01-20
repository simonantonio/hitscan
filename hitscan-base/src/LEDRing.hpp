
#include <Adafruit_NeoPixel.h>

// ============================================================================
// LED Ring Controller Class
// ============================================================================
class LEDRing {
private:
    Adafruit_NeoPixel strip;

public:
    enum class Status {
        IDLE,        // Blue breathing
        DETECTING,   // Green pulse
        RACER_0,     // Individual racer colors
        RACER_1,
        RACER_2,
        RACER_3,
        RACER_4,
        RACER_5,
        RACER_6,
        RACER_7,
        ERROR        // Red flash
    };

    LEDRing(uint8_t pin, uint16_t numLeds = 16)
        : strip(numLeds, pin, NEO_GRB + NEO_KHZ800) {}

    void begin() {
        strip.begin();
        strip.show();
        strip.setBrightness(50);
    }

    void setStatus(Status status) {
        strip.clear();

        switch(status) {
            case Status::IDLE:
                setColor(0, 0, 255); // Blue
                break;
            case Status::DETECTING:
                setColor(0, 255, 0); // Green
                break;
            case Status::ERROR:
                setColor(255, 0, 0); // Red
                break;
            case Status::RACER_0:
                setColor(255, 0, 0);   // Red
                break;
            case Status::RACER_1:
                setColor(0, 255, 0);   // Green
                break;
            case Status::RACER_2:
                setColor(0, 0, 255);   // Blue
                break;
            case Status::RACER_3:
                setColor(255, 255, 0); // Yellow
                break;
            case Status::RACER_4:
                setColor(255, 0, 255); // Magenta
                break;
            case Status::RACER_5:
                setColor(0, 255, 255); // Cyan
                break;
            case Status::RACER_6:
                setColor(255, 128, 0); // Orange
                break;
            case Status::RACER_7:
                setColor(128, 0, 255); // Purple
                break;
        }

        strip.show();
    }

    void setColor(uint8_t r, uint8_t g, uint8_t b) {
        for(int i = 0; i < strip.numPixels(); i++) {
            strip.setPixelColor(i, strip.Color(r, g, b));
        }
    }

    void flash(uint8_t r, uint8_t g, uint8_t b, int count = 3) {
        for(int i = 0; i < count; i++) {
            setColor(r, g, b);
            strip.show();
            delay(100);
            strip.clear();
            strip.show();
            delay(100);
        }
    }
};
