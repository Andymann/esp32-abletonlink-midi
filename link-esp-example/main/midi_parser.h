#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MIDI message types that we track
 */
typedef enum {
    MIDI_MSG_NONE,
    MIDI_MSG_TIMING_CLOCK,
    MIDI_MSG_START,
    MIDI_MSG_CONTINUE,
    MIDI_MSG_STOP
} midi_message_type_t;

/**
 * Initialize MIDI parser
 * Sets up tempo detection filter with default BPM of 120
 */
void midi_parser_init(void);

/**
 * Process a single incoming MIDI byte
 * Returns the message type if a complete message was detected
 * @param byte The MIDI byte to process
 * @return Message type (MIDI_MSG_NONE if no complete message yet)
 */
midi_message_type_t midi_parser_process_byte(uint8_t byte);

/**
 * Get the currently detected/filtered tempo in BPM
 * Updated as timing clocks are received and processed
 * @return Current BPM estimate
 */
float midi_parser_get_tempo(void);

/**
 * Check if we have collected enough clock measurements to report tempo
 * Returns true when we've received at least one quarter note worth of clocks (6 clocks)
 * @return true if tempo is ready for use
 */
bool midi_parser_is_tempo_ready(void);

/**
 * Check whether MIDI clock pulses are currently arriving.
 * Returns false if no clock has been received within the last second,
 * which indicates the MIDI clock source has stopped or disconnected.
 */
bool midi_parser_is_clock_active(void);

/**
 * Reset tempo detection state
 * Useful if you lose sync or want to restart
 */
void midi_parser_reset_tempo(void);

/**
 * Get raw (unfiltered) BPM from last measurement
 * Useful for debugging
 * @return Last raw BPM calculation
 */
float midi_parser_get_raw_tempo(void);

#ifdef __cplusplus
}
#endif

#endif // MIDI_PARSER_H
