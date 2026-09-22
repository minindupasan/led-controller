/*
 * DebugConsole.h - interactive serial menu.
 * Type `h` for help, `m` for the menu; every action is also a one-line
 * command so it can be scripted or pasted.
 */
#pragma once

#include <Arduino.h>

class DebugConsole {
public:
    void begin();
    void loop();
    String execute(const String &cmdline);   // shared with the web console
    void   printMenu();

private:
    String _buf;
    String cmdSeg(const String &args);
    String cmdTest(const String &args);
    String cmdStatus();
    String cmdList();
    String cmdWifi(const String &args);
};

extern DebugConsole Console;
