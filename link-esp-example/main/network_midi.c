#include "network_midi.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <string.h>
#include <errno.h>

static const char *TAG = "NETWORK_MIDI";

#define NETWORK_MIDI_PORT 5555
#define MIDI_QUEUE_SIZE 256
#define BROADCAST_ADDR "255.255.255.255"

typedef struct {
    uint8_t data[4];      // Max 3 bytes for MIDI + 1 for length
    size_t length;
    uint32_t timestamp;
} midi_message_t;

static int broadcast_sock = -1;
static QueueHandle_t midi_queue = NULL;
static TaskHandle_t midi_task_handle = NULL;
static volatile int connected_clients = 0;

static void midi_broadcast_task(void *pvParameters) {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NETWORK_MIDI_PORT);
    inet_aton(BROADCAST_ADDR, &addr.sin_addr);

    midi_message_t msg;
    uint8_t packet[8];  // length(1) + midi(3) + timestamp(4)

    ESP_LOGI(TAG, "MIDI broadcast task started on port %d", NETWORK_MIDI_PORT);

    while (true) {
        // Wait for MIDI message from queue
        if (xQueueReceive(midi_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (broadcast_sock < 0) {
                continue;  // Socket not ready
            }

            // Build packet: length + MIDI data + timestamp
            packet[0] = msg.length;
            memcpy(&packet[1], msg.data, msg.length);
            uint32_t ts = msg.timestamp;
            packet[1 + msg.length] = (ts >> 24) & 0xFF;
            packet[2 + msg.length] = (ts >> 16) & 0xFF;
            packet[3 + msg.length] = (ts >> 8) & 0xFF;
            packet[4 + msg.length] = ts & 0xFF;

            // Send broadcast
            int packet_len = msg.length + 5;
            int result = sendto(broadcast_sock, packet, packet_len, 0,
                              (struct sockaddr *)&addr, sizeof(addr));

            if (result < 0) {
                ESP_LOGD(TAG, "UDP send failed: %d", errno);
            }
        }
    }
}

void network_midi_init(void) {
    if (midi_queue != NULL) {
        ESP_LOGW(TAG, "Already initialized");
        return;
    }

    // Create message queue
    midi_queue = xQueueCreate(MIDI_QUEUE_SIZE, sizeof(midi_message_t));
    if (!midi_queue) {
        ESP_LOGE(TAG, "Failed to create MIDI queue");
        return;
    }

    // Create UDP broadcast socket
    broadcast_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (broadcast_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vQueueDelete(midi_queue);
        midi_queue = NULL;
        return;
    }

    // Set socket options for broadcast
    int opt = 1;
    if (setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        ESP_LOGE(TAG, "Failed to set SO_BROADCAST");
        close(broadcast_sock);
        broadcast_sock = -1;
        vQueueDelete(midi_queue);
        midi_queue = NULL;
        return;
    }

    // Create broadcast task
    if (xTaskCreate(midi_broadcast_task, "midi_broadcast", 4096, NULL, 5, &midi_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MIDI broadcast task");
        close(broadcast_sock);
        broadcast_sock = -1;
        vQueueDelete(midi_queue);
        midi_queue = NULL;
        return;
    }

    ESP_LOGI(TAG, "Network MIDI initialized");
}

void network_midi_send(const uint8_t *data, size_t length) {
    if (!midi_queue || length == 0 || length > 3) {
        return;  // Invalid message or not initialized
    }

    midi_message_t msg = {0};
    msg.length = length;
    msg.timestamp = (uint32_t)(esp_timer_get_time() / 1000);  // ms since boot
    memcpy(msg.data, data, length);

    // Send to queue (non-blocking to avoid blocking MIDI timing)
    xQueueSendToBack(midi_queue, &msg, 0);
}

void network_midi_deinit(void) {
    if (midi_task_handle) {
        vTaskDelete(midi_task_handle);
        midi_task_handle = NULL;
    }

    if (broadcast_sock >= 0) {
        close(broadcast_sock);
        broadcast_sock = -1;
    }

    if (midi_queue) {
        vQueueDelete(midi_queue);
        midi_queue = NULL;
    }

    ESP_LOGI(TAG, "Network MIDI deinitialized");
}

int network_midi_get_client_count(void) {
    // Note: UDP is stateless, so we track clients indirectly
    // For now, return a placeholder - could be enhanced with client tracking
    return connected_clients;
}
