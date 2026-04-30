#include "midi_parser.h"
#include <esp_timer.h>
#include <esp_log.h>

static const char *TAG = "MIDI_PARSER";

#define MIDI_TIMING_CLOCK 0xF8
#define MIDI_START        0xFA
#define MIDI_CONTINUE     0xFB
#define MIDI_STOP         0xFC

// BPM range limits — only hard filter, no outlier rejection
#define MIN_BPM 40.0f
#define MAX_BPM 300.0f

// Adaptive EMA: large tempo jumps (>= CHANGE_THRESHOLD) get near-instant
// response; small fluctuations get gentle smoothing.
#define EMA_ALPHA_STEADY  0.2f   // jitter filtering during stable playback
#define EMA_ALPHA_CHANGE  0.9f   // fast lock-on after a tempo change
#define CHANGE_THRESHOLD  0.02f  // 2% change (~2.4 BPM at 120) triggers fast mode

// Standard MIDI: 24 timing clock pulses per quarter note
#define CLOCKS_PER_QUARTER_NOTE 24

typedef struct {
    float current_bpm;              // Current filtered BPM
    float raw_bpm;                  // Last raw BPM measurement
    uint64_t last_clock_time_us;    // Timestamp of last timing clock
    int clock_count;                // Clocks received since last tempo update
    bool tempo_ready;               // Whether we have enough samples
} midi_parser_state_t;

static midi_parser_state_t state = {0};

void midi_parser_init(void) {
    state.current_bpm = 120.0f;
    state.raw_bpm = 120.0f;
    state.last_clock_time_us = 0;
    state.clock_count = 0;
    state.tempo_ready = false;

    ESP_LOGI(TAG, "MIDI parser initialized");
}

static float calculate_bpm_from_interval(uint64_t interval_us) {
    // interval_us is the duration of exactly 24 MIDI clocks (one quarter note)
    if (interval_us == 0) {
        return state.current_bpm;
    }
    return 60000000.0f / (float)interval_us;
}

static bool is_valid_bpm(float bpm) {
    return bpm >= MIN_BPM && bpm <= MAX_BPM;
}

static void update_tempo_with_clock(void) {
    uint64_t current_time = esp_timer_get_time();

    if (state.last_clock_time_us == 0) {
        // Anchor the start of the first quarter note measurement
        state.last_clock_time_us = current_time;
        state.clock_count = 0;
        return;
    }

    state.clock_count++;

    // Measure once per quarter note: interval spans exactly 24 clock pulses
    if (state.clock_count >= CLOCKS_PER_QUARTER_NOTE) {
        uint64_t interval = current_time - state.last_clock_time_us;
        state.last_clock_time_us = current_time;
        state.clock_count = 0;

        float raw_bpm = calculate_bpm_from_interval(interval);

        if (!is_valid_bpm(raw_bpm)) {
            ESP_LOGD(TAG, "BPM out of range: %.1f", raw_bpm);
            return;
        }

        // Adaptive alpha: large tempo changes lock on fast; steady state smooths jitter.
        float change = (state.current_bpm > 0.0f)
                       ? (raw_bpm - state.current_bpm) / state.current_bpm
                       : 1.0f;
        float alpha = (change < -CHANGE_THRESHOLD || change > CHANGE_THRESHOLD)
                      ? EMA_ALPHA_CHANGE : EMA_ALPHA_STEADY;

        state.raw_bpm = raw_bpm;
        state.current_bpm = alpha * raw_bpm + (1.0f - alpha) * state.current_bpm;
        state.tempo_ready = true;

        ESP_LOGD(TAG, "Tempo updated: raw=%.1f, filtered=%.1f", raw_bpm, state.current_bpm);
    }
}

midi_message_type_t midi_parser_process_byte(uint8_t byte) {
    midi_message_type_t msg_type = MIDI_MSG_NONE;

    switch (byte) {
        case MIDI_TIMING_CLOCK:
            update_tempo_with_clock();
            msg_type = MIDI_MSG_TIMING_CLOCK;
            break;

        case MIDI_START:
            ESP_LOGI(TAG, "MIDI START received");
            // Re-anchor the clock measurement so tempo re-locks from the new downbeat
            state.last_clock_time_us = 0;
            state.clock_count = 0;
            state.tempo_ready = false;
            msg_type = MIDI_MSG_START;
            break;

        case MIDI_CONTINUE:
            ESP_LOGI(TAG, "MIDI CONTINUE received");
            msg_type = MIDI_MSG_CONTINUE;
            break;

        case MIDI_STOP:
            ESP_LOGI(TAG, "MIDI STOP received");
            msg_type = MIDI_MSG_STOP;
            break;

        default:
            // Ignore other MIDI messages (note on/off, etc.)
            // In a full implementation, you might handle these
            break;
    }

    return msg_type;
}

float midi_parser_get_tempo(void) {
    return state.current_bpm;
}

bool midi_parser_is_tempo_ready(void) {
    return state.tempo_ready;
}

void midi_parser_reset_tempo(void) {
    state.current_bpm = 120.0f;
    state.raw_bpm = 120.0f;
    state.last_clock_time_us = 0;
    state.clock_count = 0;
    state.tempo_ready = false;
    ESP_LOGI(TAG, "Tempo detection reset");
}

bool midi_parser_is_clock_active(void) {
    if (state.last_clock_time_us == 0) {
        return false;
    }
    // Consider clock inactive if no pulse received in the last second.
    // At the slowest valid tempo (40 BPM) a clock arrives every ~62.5 ms,
    // so 1 second is a very generous timeout.
    return (esp_timer_get_time() - state.last_clock_time_us) < 1000000;
}

float midi_parser_get_raw_tempo(void) {
    return state.raw_bpm;
}
