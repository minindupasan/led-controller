/*
 * DebugConsole.h - interactive serial menu + machine protocol.
 *
 * Humans : type `h` for the menu; every action is a one-line command.
 * Machine: `json` / `stat` / `hello` reply with a single line that starts
 *          with "#J" followed by compact JSON, so webapp/index.html can
 *          parse replies while ignoring the human-readable chatter.
 */
#pragma once

#include <Arduino.h>

class DebugConsole {
public:
    void begin();
    void loop();
    String execute(const String &cmdline);
    void   printMenu();

    /* Output routing: a transport can capture what a command prints instead
       of letting it go to the USB serial port. Defaults to Serial. */
    void   setOut(Print *p) { _out = p ? p : &Serial; }
    Print *out() { return _out; }

    /* protocol emitters (each prints exactly one "#J..." line) */
    void emitHello();
    void emitState();                 // full config: globals + segments + anims
    void emitStatus();                // telemetry, polled by the UI
    void emitAck(const String &cmd, const String &out);

private:
    Print *_out = &Serial;
    String _buf;
    String cmdSeg(const String &args);
    String cmdTest(const String &args);
    String cmdStatus();
    String cmdList();
    String cmdPin(const String &args);
};

extern DebugConsole Console;
