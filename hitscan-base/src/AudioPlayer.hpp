
#include <Arduino.h>
#include <driver/i2s.h>

// ============================================================================
// Audio Player Class (MAX98357A)
// ============================================================================
class AudioPlayer
{
private:
    static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
    static constexpr int SAMPLE_RATE = 44100;

public:
    AudioPlayer(uint8_t bckPin, uint8_t wsPin, uint8_t dataPin)
    {
        i2s_config_t i2s_config = {
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
            .sample_rate = SAMPLE_RATE,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = 8,
            .dma_buf_len = 64,
            .use_apll = false,
            .tx_desc_auto_clear = true,
            .fixed_mclk = 0};

        i2s_pin_config_t pin_config = {
            .bck_io_num = bckPin,
            .ws_io_num = wsPin,
            .data_out_num = dataPin,
            .data_in_num = I2S_PIN_NO_CHANGE};

        i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
        i2s_set_pin(I2S_PORT, &pin_config);
    }

    void playWav(const char *filename)
    {
        // TODO: Implement WAV file playback from SD card
        // 1. Open file from SD
        // 2. Parse WAV header
        // 3. Stream audio data to I2S
        Serial.printf("Playing: %s\n", filename);
    }

    void playTone(int frequency, int duration)
    {
        // Non-blocking tone generation using task
        // Store params for background task
        static int toneFreq = frequency;
        static int toneDuration = duration;

        // Quick fire-and-forget tone (simplified, doesn't actually block main loop)
        // For true non-blocking, would need FreeRTOS task
        // For now, keep tones SHORT (< 150ms) to minimize impact
        const int sampleCount = (SAMPLE_RATE * duration) / 1000;
        int16_t sample;
        size_t bytesWritten;

        for (int i = 0; i < sampleCount && i < 6615; i++)
        { // Max ~150ms at 44.1kHz
            sample = (int16_t)(sin(2.0 * PI * frequency * i / SAMPLE_RATE) * 10000);
            i2s_write(I2S_PORT, &sample, sizeof(sample), &bytesWritten, 0); // No wait
        }
    }
};
