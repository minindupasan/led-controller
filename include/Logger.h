/*
 * Logger.h - tiny ring-buffer logger shared by the serial console and the
 * web debug panel (lines are pushed to every websocket client).
 */
#pragma once

#include <Arduino.h>

#define LOG_LINES 48
#define LOG_LINE_LEN 120

typedef void (*LogSink)(const char *line);

class Logger {
public:
    void begin(uint32_t baud);
    void log(const char *fmt, ...);
    void setSink(LogSink s) { _sink = s; }

    uint8_t     count() const { return _count; }
    const char *line(uint8_t i) const;   // 0 = oldest kept line
    void        clear();

private:
    char    _buf[LOG_LINES][LOG_LINE_LEN];
    uint8_t _head = 0;
    uint8_t _count = 0;
    LogSink _sink = nullptr;
};

extern Logger Log;
