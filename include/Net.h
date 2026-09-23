/*
 * Net.h - Wi-Fi transport, one network shared by everything.
 *
 *   ROLE_LAMP  hosts the AP "INNOV-IOT-SIGN" at 192.168.4.1. The Mac running
 *              the RFID bridge joins it, and so does the sign.
 *   ROLE_SIGN  joins that AP with an ordinary DHCP lease, then announces its
 *              address to the lamp so the trigger can find it. Nothing is
 *              pinned: .2 is the first address the AP gives out, so a static
 *              claim there collides with whatever joined first.
 *
 * Both serve the same UI (webapp/index.html, gzipped into flash at build
 * time) and a WebSocket at /ws carrying the same line protocol as the USB
 * serial console, so every transport shares one command set.
 */
#pragma once

#include <Arduino.h>

class NetInterface {
public:
    void begin();
    void loop();

    /* Lamp -> sign trigger. Fire-and-forget HTTP so a sign that is off or
       still booting can never stall the lamp mid-event. */
    bool sendToSign(const String &command);
    bool signSeen() const { return _signSeen; }

    void   announceToLamp();                  // sign -> lamp, "I am here"
    void   setSignAddr(const String &ip);     // lamp remembers where the sign is
    String signAddr() const { return _signAddr; }

    void broadcastLog(const char *line);      // Logger sink
    bool connected() const;
    uint8_t clients() const;
    String  ip() const;
    String  modeName() const;
    void    restart();                        // re-apply stored settings

private:
    bool   _signSeen = false;
    String _signAddr;                         // learned, not hardcoded
};

extern NetInterface Net;
