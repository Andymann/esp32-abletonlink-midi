#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MIDI_MSG_NONE,
    MIDI_MSG_TIMING_CLOCK,
    MIDI_MSG_START,
    MIDI_MSG_CONTINUE,
    MIDI_MSG_STOP
} midi_message_type_t;

typedef struct {
    float    current_bpm;
    float    raw_bpm;
    uint64_t last_clock_time_us;
    int      clock_count;
    bool     tempo_ready;
} midi_parser_state_t;

void               midi_parser_init(midi_parser_state_t *p);
midi_message_type_t midi_parser_process_byte(midi_parser_state_t *p, uint8_t byte);
float              midi_parser_get_tempo(midi_parser_state_t *p);
bool               midi_parser_is_tempo_ready(midi_parser_state_t *p);
bool               midi_parser_is_clock_active(midi_parser_state_t *p);
void               midi_parser_reset_tempo(midi_parser_state_t *p);
float              midi_parser_get_raw_tempo(midi_parser_state_t *p);

#ifdef __cplusplus
}
#endif

#endif // MIDI_PARSER_H
