/*
 * Net.h - Wi-Fi transport.
 *
 * Hosts the UI (webapp/index.html, gzipped into flash at build time) and a
 * WebSocket at /ws that carries exactly the same line protocol as the USB
 * serial console, so both transports share one command set.
 *
 * Boot order: stored STA credentials if any, otherwise our own AP
 * "INNOV-IOT-SIGN" at http://192.168.4.1 (also http://ledsign.local).
 */
#pragma once

#include <Arduino.h>

class NetInterface {
public:
    void begin();
    void loop();

    void broadcastLog(const char *line);      // Logger sink
    bool connected() const;
    uint8_t clients() const;
    String  ip() const;
    String  modeName() const;
    void    restart();                        // re-apply stored settings
};

extern NetInterface Net;
