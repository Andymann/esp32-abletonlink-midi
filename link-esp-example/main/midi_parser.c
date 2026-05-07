#include "midi_parser.h"
#include <esp_timer.h>
#include <esp_log.h>

static const char *TAG = "MIDI_PARSER";

#define MIDI_TIMING_CLOCK 0xF8
#define MIDI_START        0xFA
#define MIDI_CONTINUE     0xFB
#define MIDI_STOP         0xFC

#define MIN_BPM 40.0f
#define MAX_BPM 300.0f

#define EMA_ALPHA_STEADY  0.2f
#define EMA_ALPHA_CHANGE  0.9f
#define CHANGE_THRESHOLD  0.02f

#define CLOCKS_PER_QUARTER_NOTE 24

void midi_parser_init(midi_parser_state_t *p) {
    p->current_bpm        = 120.0f;
    p->raw_bpm            = 120.0f;
    p->last_clock_time_us = 0;
    p->clock_count        = 0;
    p->tempo_ready        = false;
    ESP_LOGI(TAG, "MIDI parser initialized");
}

static void update_tempo_with_clock(midi_parser_state_t *p) {
    uint64_t current_time = esp_timer_get_time();

    if (p->last_clock_time_us == 0) {
        p->last_clock_time_us = current_time;
        p->clock_count = 0;
        return;
    }

    p->clock_count++;

    if (p->clock_count >= CLOCKS_PER_QUARTER_NOTE) {
        uint64_t interval = current_time - p->last_clock_time_us;
        p->last_clock_time_us = current_time;
        p->clock_count = 0;

        if (interval == 0) return;
        float raw_bpm = 60000000.0f / (float)interval;

        if (raw_bpm < MIN_BPM || raw_bpm > MAX_BPM) {
            ESP_LOGD(TAG, "BPM out of range: %.1f", raw_bpm);
            return;
        }

        float change = (p->current_bpm > 0.0f)
                       ? (raw_bpm - p->current_bpm) / p->current_bpm
                       : 1.0f;
        float alpha = (change < -CHANGE_THRESHOLD || change > CHANGE_THRESHOLD)
                      ? EMA_ALPHA_CHANGE : EMA_ALPHA_STEADY;

        p->raw_bpm     = raw_bpm;
        p->current_bpm = alpha * raw_bpm + (1.0f - alpha) * p->current_bpm;
        p->tempo_ready = true;

        ESP_LOGD(TAG, "Tempo: raw=%.1f filtered=%.1f", raw_bpm, p->current_bpm);
    }
}

midi_message_type_t midi_parser_process_byte(midi_parser_state_t *p, uint8_t byte) {
    switch (byte) {
        case MIDI_TIMING_CLOCK:
            update_tempo_with_clock(p);
            return MIDI_MSG_TIMING_CLOCK;

        case MIDI_START:
            ESP_LOGI(TAG, "MIDI START");
            p->last_clock_time_us = 0;
            p->clock_count        = 0;
            p->tempo_ready        = false;
            return MIDI_MSG_START;

        case MIDI_CONTINUE:
            ESP_LOGI(TAG, "MIDI CONTINUE");
            return MIDI_MSG_CONTINUE;

        case MIDI_STOP:
            ESP_LOGI(TAG, "MIDI STOP");
            return MIDI_MSG_STOP;

        default:
            return MIDI_MSG_NONE;
    }
}

float midi_parser_get_tempo(midi_parser_state_t *p)    { return p->current_bpm; }
bool  midi_parser_is_tempo_ready(midi_parser_state_t *p) { return p->tempo_ready; }
float midi_parser_get_raw_tempo(midi_parser_state_t *p) { return p->raw_bpm; }

void midi_parser_reset_tempo(midi_parser_state_t *p) {
    p->current_bpm        = 120.0f;
    p->raw_bpm            = 120.0f;
    p->last_clock_time_us = 0;
    p->clock_count        = 0;
    p->tempo_ready        = false;
    ESP_LOGI(TAG, "Tempo detection reset");
}

bool midi_parser_is_clock_active(midi_parser_state_t *p) {
    if (p->last_clock_time_us == 0) return false;
    return (esp_timer_get_time() - p->last_clock_time_us) < 1000000;
}
