# Falafel Device architecture

## Goals and boundaries

Falafel separates portable product code from platform integration. The desktop
simulator is the first platform; ESP-IDF on the Waveshare ESP32-S3 Touch LCD
3.5 will be the second. The firmware uses ESP-IDF, not Arduino.

The layers are intentionally modest:

- `shared/gui`: LVGL screen and widget construction, with no ESP32 headers.
- `shared/app`: future application flow and use cases, independent of UI and hardware.
- `shared/models`: future plain data structures shared by application modules.
- `simulator`: SDL2/LVGL startup and desktop event loop.
- `firmware`: ESP-IDF startup and, later, hardware/platform adapters.

Only directories needed by the current milestone are populated. Future modules
such as messages, camera, power, storage, and network should be introduced when
their first real behavior is developed, rather than as empty abstractions.

## Dependency direction

Platform entry points call portable code. Portable GUI and application code must
not include ESP-IDF or SDL headers. Platform services will later expose small,
purpose-specific interfaces to application code where a real use case requires
them.

```text
simulator (SDL2) ----\
                     +--> GUI (LVGL) --> callback API --> future application layer
firmware (ESP-IDF) --/                         |
                                                +--> future platform services
```

The public boundary is `shared/include/falafel_gui.h`. Status and message data
enter the GUI through small setter functions. User actions leave it through
platform-independent callbacks carrying plain C values and strings. The GUI
does not access the Internet, networking APIs, storage, FreeRTOS, ESP-IDF, or
SDL; a future application layer will decide how to handle each callback.

The GUI is split by responsibility into theme, status bar, personality home,
message list, composer, and a small coordinator. LVGL remains an implementation
detail behind the public header.

For the current desktop milestone only the simulator branch is connected to
LVGL. Firmware LVGL integration waits for the official board documentation and
driver choice.

## Device-to-device communication (future decision)

The devices are separated by the public Internet, so direct discovery alone is
not sufficient. A reachable service will likely relay messages and presence:

```text
Falafel A
    |
 Internet
    |
service / backend
    |
 Internet
    |
Falafel B
```

Alternatives to evaluate later:

- MQTT broker: mature pub/sub semantics, retained state and quality-of-service
  options; requires careful identity, authorization, and topic design.
- Small HTTPS/WebSocket backend: more control over message storage, presence,
  media, and product-specific APIs; more server code and operations work.
- Managed realtime service: quickest infrastructure start, but adds vendor,
  pricing, and embedded-client constraints.

The decision must consider authentication, offline delivery, presence accuracy,
reconnection, message ordering, photo size/storage, TLS memory cost on ESP32-S3,
and service ownership. No protocol is selected in Milestone 1.

## Configuration and secrets

Wi-Fi credentials, device keys, tokens, and certificates must never be committed.
The future provisioning and secure-storage design will be documented before
networking is implemented.
