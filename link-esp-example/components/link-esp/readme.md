esp32 live link with bidirectional MIDI control

## Features

### Ableton Link Core
- Synchronization with Ableton Live and other Link-enabled applications
- Beat timing, tempo, and phase synchronization
- LED indicator synchronized to Link beats
- Audio feedback buzzer with different frequencies for beat positions

### Bidirectional MIDI Bridge
This ESP32 now acts as a **MIDI-to-Ableton Link bridge** in both directions:

**Incoming MIDI → Ableton Link:**
- Read MIDI from connected hardware controllers on MIDI_UART
- MIDI Timing Clock (0xF8) → Automatically detect and set Link tempo
- MIDI Start (0xFA) → Start Link playback
- MIDI Stop (0xFC) → Stop Link playback
- Seamless integration with hardware MIDI sequencers and drum machines

**Ableton Link → Outgoing MIDI:**
- Link tempo changes → Sends MIDI Timing Clock stream at detected tempo
- Link playback start → Sends MIDI Start command (0xFA)
- Link playback stop → Sends MIDI Stop command (0xFC)
- Allows synchronizing external MIDI gear with Ableton Live via Link

## Hardware Setup

### UART Connections
- **MIDI_UART (UART2)** - Primary MIDI I/O
  - RX (GPIO 16): Incoming MIDI from controller
  - TX (GPIO 17): Outgoing MIDI to devices
  - Baud Rate: 31250 (standard MIDI)
- **USB_UART (UART0)** - USB MIDI
  - TX: GPIO 1
  - RX: GPIO 3
  - Baud Rate: 31250

### GPIO Pins
- **LED (GPIO 4)**: Blinks synchronized with Link beat
- **Buzzer (GPIO 2)**: PWM-controlled audio feedback

### Tempo Detection
The system automatically detects incoming MIDI timing clocks:
- **Timing Clock Format**: MIDI sends 24 clocks per quarter note (standard)
- **Formula**: `BPM = 2,500,000 / clock_interval_us`
- **Filtering**: Exponential Moving Average (EMA) with alpha=0.1 for stable tempo
- **Range**: Accepts 40-300 BPM, rejects outliers > ±20% from expected
- **Update Rate**: Every 6 clocks (quarter note boundary) for stable detection

## WiFi Access Point

Runs as a standalone WiFi AP for Link peer discovery:
- **SSID**: `Ableton-Link-ESP32`
- **Password**: `1234567890`
- **IP Range**: `192.168.4.0/24` (default ESP32 AP)
- **Max Clients**: 4

Configure in `main/main.cpp`:
```cpp
#define WIFI_SSID "Ableton-Link-ESP32"
#define WIFI_PASS "1234567890"
```

## Usage Scenarios

### Scenario 1: Hardware Controller → Ableton Live
1. Connect MIDI controller (with timing clock output) to ESP32 MIDI_UART
2. ESP32 detects clock tempo and updates Link
3. Open Ableton Live with Link enabled on same WiFi network
4. Live automatically syncs to controller tempo
5. Send MIDI Start/Stop from controller → Live transport follows

### Scenario 2: Ableton Live → Hardware Devices
1. Open Ableton Live with Link enabled
2. Connect hardware MIDI devices (drum machine, synth) to ESP32 MIDI_UART (TX)
3. ESP32 sends MIDI clock matching Live's tempo
4. External devices sync to Live's transport
5. Change Live tempo → External devices follow in real-time

### Scenario 3: Multi-Device Sync
1. Hardware controller sends MIDI clock to ESP32
2. ESP32 sets Link tempo to match controller
3. Ableton Live (via WiFi Link) syncs to same tempo
4. Other Link peers (phones, laptops) join Link session
5. Everything in sync: hardware + Live + remote peers

## MIDI Message Details

### Incoming MIDI Processing
- **Timing Clock (0xF8)**
  - Measured over 6 clocks (1 quarter note)
  - Converted to BPM using clock interval timing
  - Applied to Link every quarter note boundary
  - Smoothed with EMA filter to reject jitter

- **Start (0xFA)**
  - Immediately sets Link `isPlaying = true`
  - Committed at current clock time

- **Stop (0xFC)**
  - Immediately sets Link `isPlaying = false`
  - Committed at current clock time

### Outgoing MIDI Generation
- **Timing Clock (0xF8)**
  - Generated whenever Link beat advances
  - Rate determined by Link's current tempo
  - Stream continues as long as Link is playing

- **Start (0xFA)**
  - Sent when Link transport transitions from stopped to playing
  - With optional reset sequence (Stop→Start for phase alignment)

- **Stop (0xFC)**
  - Sent when Link transport transitions from playing to stopped

## Configuration & Tuning

### Tempo Filter Sensitivity
Edit in `main/midi_parser.c`:
```c
#define EMA_ALPHA 0.1f  // 0.05 (smoother) to 0.2 (more responsive)
```

### BPM Validation Range
```c
#define MIN_BPM 40.0f
#define MAX_BPM 300.0f
#define OUTLIER_THRESHOLD 0.2f  // Reject > ±20% outliers
```

## Building and Flashing

```bash
idf.py build
idf.py flash
idf.py monitor
```

Monitor output shows:
- MIDI messages detected
- Tempo updates and calculations
- WiFi connection events
- Link peer connections

## Testing Checklist

### Incoming MIDI → Link
- [ ] Send steady MIDI clocks from external device to ESP32
- [ ] Watch serial monitor for tempo detection messages
- [ ] Connect Ableton Live via Link WiFi AP
- [ ] Verify Live's tempo matches incoming MIDI
- [ ] Send MIDI Start → Link transport should play
- [ ] Send MIDI Stop → Link transport should stop

### Link → Outgoing MIDI
- [ ] Change Ableton Live tempo
- [ ] Verify MIDI timing clock rate changes on MIDI_UART (use oscilloscope/analyzer)
- [ ] Start Live transport → MIDI Start message sent
- [ ] Stop Live transport → MIDI Stop message sent

### Full Integration
- [ ] LED blinks in sync with beats
- [ ] Buzzer provides audio feedback
- [ ] Multiple Link peers see same tempo
- [ ] Bidirectional control works smoothly
- [ ] No crashes under sustained MIDI streaming

## Performance Notes

- **MIDI Clock Rate**: ~24 clocks per quarter note at typical tempo
- **Incoming Processing**: ~1 tempo update per quarter note (efficient)
- **Link Update Frequency**: Whenever MIDI changes detected
- **WiFi Latency**: Typical 5-50ms (acceptable for Link sync)
- **CPU Usage**: Minimal, mainly on clock reception processing

## Troubleshooting

**Tempo not updating:**
- Verify MIDI controller is sending timing clocks (0xF8)
- Check UART pins are correctly connected (RX on GPIO 16)
- Enable debug logging: `esp_log_level_set("MIDI_PARSER", ESP_LOG_DEBUG)`

**No WiFi AP appearing:**
- Verify WiFi enabled and connected in `app_main()`
- Check SSID in logs matches configuration
- Try scanning from phone to verify AP broadcast

**Link peers not connecting:**
- Ensure ESP32 and peers are on same WiFi network
- Check firewall isn't blocking UDP multicast (Link uses UDP)
- Verify Link is enabled on all devices

**MIDI not sending:**
- Verify external device is listening on correct serial port
- Check TX pin (GPIO 17) is connected
- Monitor MIDI output: `nc -l /dev/ttyUSB0` or similar


