/*
 * WebInterface.h - async HTTP + WebSocket control surface.
 * Serves the single-page black & white UI from PROGMEM.
 */
#pragma once

#include <Arduino.h>

class WebInterface {
public:
    void begin();
    void loop();                       // housekeeping + status push
    void pushLog(const char *line);    // called by Logger's sink
    bool clientsConnected() const;
};

extern WebInterface Web;
