#!/usr/bin/env python3
"""
lamp_bridge.py - the link between the RFID reader and the oil lamp.

The Arduino has no Wi-Fi, so it prints each tap over USB serial and this
script (on the Mac joined to the lamp's access point) forwards it:

    Arduino  --USB serial-->  Mac  --HTTP-->  LAMP (ESP #2, 192.168.4.1)
      CARD:A1B2C3D4                 logo card A1B2C3D4
                                            |
                                            | when the last guest taps
                                            v
                                    SIGN (ESP #1)  ->  TRAVERSE, then BREATHE

The lamp hosts the Wi-Fi and owns the guest list; the sign joins as a client.
This script only ever talks to the lamp - the trigger to the sign is the
lamp's own business, so there is nothing to configure here about ESP #1.

Setup on the Mac the Arduino is plugged into:
    1. python3 -m pip install --user pyserial requests      (do this first,
       while you still have internet)
    2. join Wi-Fi  INNOV-IOT-SIGN  /  innoviot123
    3. python3 lamp_bridge.py
       (it finds the Arduino itself; add --port /dev/cu.usbmodem1401 to force one)

Leave it running for the duration of the event. It reconnects on its own if
the Arduino is unplugged or the Wi-Fi drops.
"""

import argparse
import sys
import time
import urllib.parse

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial missing:  pip install pyserial")

try:
    import requests
except ImportError:
    sys.exit("requests missing:  pip install requests")


def find_port(explicit=None, avoid=()):
    """
    Use the port we were given, else guess the Arduino.

    Careful: the ESP32 sign is usually /dev/cu.usbserial-XXXX, which matches
    the same loose patterns an Arduino clone does. So candidates are ranked -
    a native-USB Arduino (usbmodem) wins over a generic serial bridge - and
    anything in `avoid` is skipped entirely.
    """
    if explicit:
        return explicit

    ranked = []
    for p in list_ports.comports():
        dev = p.device
        if dev in avoid or "bluetooth" in dev.lower() or "debug-console" in dev:
            continue
        blob = f"{dev} {p.description} {p.manufacturer or ''}".lower()

        if "arduino" in blob:            score = 0     # says so outright
        elif "usbmodem" in blob:         score = 1     # Uno R3 / Leonardo / Mega
        elif any(k in blob for k in ("ch340", "wch", "ftdi")): score = 2
        elif "usbserial" in blob or "silicon labs" in blob:    score = 3
        else:
            continue
        ranked.append((score, dev, p.description))

    if not ranked:
        return None
    ranked.sort()

    if len(ranked) > 1:
        print("  serial ports seen:")
        for score, dev, desc in ranked:
            print(f"    {dev}  ({desc})")
        print(f"  using {ranked[0][1]} - pass --port to override")
    return ranked[0][1]


def send(host, command, timeout=2.5):
    """Fire one console command at the sign. Returns its reply, or None."""
    url = f"http://{host}/api/cmd?c=" + urllib.parse.quote_plus(command)
    try:
        r = requests.get(url, timeout=timeout)
        return r.text.strip()
    except requests.RequestException as e:
        print(f"  ! sign unreachable ({e.__class__.__name__}) - is the laptop on "
              f"the INNOV-IOT-SIGN network?")
        return None


def main():
    ap = argparse.ArgumentParser(description="RFID reader -> oil lamp bridge")
    ap.add_argument("--port", help="serial port, e.g. /dev/cu.usbmodem1401 (COM5 on Windows)")
    ap.add_argument("--baud", type=int, default=9600, help="must match the sketch (9600)")
    ap.add_argument("--host", default="192.168.4.1",
                    help="the lamp's address (ESP #2, which hosts the wi-fi)")
    ap.add_argument("--quiet", action="store_true", help="only print taps")
    ap.add_argument("--not-port", action="append", default=[], metavar="PORT",
                    help="never auto-pick this port (e.g. the ESP32's)")
    ap.add_argument("--reset", action="store_true",
                    help="clear the lamp before starting (use between runs)")
    args = ap.parse_args()

    if args.reset:
        print(f"resetting the lamp: {send(args.host, 'logo reset')}")

    print(f"lamp at http://{args.host}")
    hello = send(args.host, "hello")
    if hello:
        print(f"  {hello.splitlines()[0][:90]}")
        status = send(args.host, "s") or ""
        for line in status.splitlines():
            if "sign link" in line or "lamp " in line:
                print(f"  {line.strip()}")
    else:
        print("  NOT reachable yet - check the Mac is on the INNOV-IOT-SIGN "
              "network and the lamp is powered. Will keep trying.")

    while True:
        port = find_port(args.port, avoid=set(args.not_port))
        if not port:
            print("waiting for the Arduino ...")
            time.sleep(2)
            continue

        try:
            ser = serial.Serial(port, args.baud, timeout=1)
        except serial.SerialException as e:
            print(f"cannot open {port}: {e}")
            time.sleep(2)
            continue

        print(f"reader on {port} @ {args.baud}")
        time.sleep(2)                      # the Arduino resets when we open it
        ser.reset_input_buffer()

        try:
            while True:
                line = ser.readline().decode("utf-8", "replace").strip()
                if not line:
                    continue

                if line.startswith("CARD:"):
                    uid = line[5:].strip().upper()
                    print(f"tap {uid}", end="  ->  ")
                    reply = send(args.host, f"logo card {uid}")
                    print(reply or "(no reply)")

                    # tell the lamp how the sign judged it, so its confirmation
                    # flash matches: gold accepted, blue learned, red unknown
                    if reply is None:          verdict = b"ERR\n"
                    elif "learned" in reply:   verdict = b"NEW\n"
                    elif "unknown" in reply:   verdict = b"ERR\n"
                    else:                      verdict = b"OK\n"

                    # the moment the whole thing is built around
                    if reply and "LAMP FULL" in reply.upper():
                        print("  *** LAMP FULL - the sign should be running "
                              "its opener now ***")
                    try:
                        ser.write(verdict)
                    except serial.SerialException:
                        pass

                elif not args.quiet:
                    print(f"  [arduino] {line}")

        except serial.SerialException:
            print("reader disconnected - waiting for it to come back")
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(2)
        except KeyboardInterrupt:
            print("\nbye")
            ser.close()
            return


if __name__ == "__main__":
    main()
