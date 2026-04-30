#ifndef NETWORK_MIDI_H
#define NETWORK_MIDI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize network MIDI streaming server
 * Creates a UDP broadcast server on port 5555 to send MIDI messages to network clients
 * Must be called after WiFi AP is initialized
 */
void network_midi_init(void);

/**
 * Send MIDI message to all connected clients via UDP broadcast
 * Also maintains message history for client synchronization
 * @param data Pointer to MIDI message bytes
 * @param length Number of bytes in MIDI message (1-3 typically)
 */
void network_midi_send(const uint8_t *data, size_t length);

/**
 * Stop network MIDI server and clean up resources
 */
void network_midi_deinit(void);

/**
 * Get number of connected MIDI clients (tracked via UDP client discovery)
 * @return Number of clients or -1 if error
 */
int network_midi_get_client_count(void);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_MIDI_H
