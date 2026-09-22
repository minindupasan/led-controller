#include "Logger.h"
#include <stdarg.h>

Logger Log;

void Logger::begin(uint32_t baud) {
    Serial.begin(baud);
    delay(300);
    clear();
}

void Logger::clear() {
    _head = 0;
    _count = 0;
    ::memset(_buf, 0, sizeof(_buf));
}

void Logger::log(const char *fmt, ...) {
    char msg[LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char line[LOG_LINE_LEN];
    snprintf(line, sizeof(line), "[%8lu] %s", (unsigned long)millis(), msg);

    ::strncpy(_buf[_head], line, LOG_LINE_LEN - 1);
    _buf[_head][LOG_LINE_LEN - 1] = 0;
    _head = (_head + 1) % LOG_LINES;
    if (_count < LOG_LINES) _count++;

    Serial.println(line);
    if (_sink) _sink(line);
}

const char *Logger::line(uint8_t i) const {
    if (i >= _count) return "";
    uint8_t oldest = (_head + LOG_LINES - _count) % LOG_LINES;
    return _buf[(oldest + i) % LOG_LINES];
}
