/*
 * DebugConsole.h - serial menu plus the machine protocol.
 *
 * Humans : `h` for the menu; every action is a one-line command.
 * Machine: `json` / `stat` / `hello` reply with one line starting "#J"
 *          followed by compact JSON, which webapp/index.html parses while
 *          ignoring the human-readable output.
 */
#pragma once

#include <Arduino.h>

class DebugConsole {
public:
    void begin();
    void loop();
    String execute(const String &cmdline);
    void   printMenu();

    void setOut(Print *p) { _out = p ? p : &Serial; }
    Print *out() { return _out; }

    void emitHello();
    void emitState();
    void emitStatus();

private:
    Print *_out = &Serial;
    String _buf;
    String cmdLetter(const String &args);
    String cmdWord(const String &args);
    String cmdTest(const String &args);
    String cmdShow(const String &args);
    String cmdPin(const String &args);
    String cmdStatus();
    String cmdList();
};

extern DebugConsole Console;
