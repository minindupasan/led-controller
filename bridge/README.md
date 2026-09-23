# Laptop bridge

The Arduino reads the RFID cards but has no Wi-Fi, so the laptop it is plugged
into carries each tap across to the sign.

```
Arduino  --USB serial-->  laptop  --HTTP over the AP-->  ESP32
  CARD:A1B2C3D4                     /api/cmd?c=logo+card+A1B2C3D4
```

## Run it (macOS)

Install the two packages **before** joining the sign's Wi-Fi — once you are on
the AP that adapter has no internet, because the ESP32 is not a router.

```bash
python3 -m pip install --user pyserial requests
```

Then join **INNOV-IOT-SIGN** / **innoviot123** and start it:

```bash
python3 lamp_bridge.py
```

It finds the Arduino by itself; `ls /dev/cu.*` if you want to pass one
explicitly with `--port /dev/cu.usbmodem1401`. It reconnects on its own if the
Arduino is unplugged or the Wi-Fi drops.

If `pip` refuses with *externally-managed-environment* (Homebrew Python), use a
virtual environment instead:

```bash
python3 -m venv ~/lampenv
~/lampenv/bin/pip install pyserial requests
~/lampenv/bin/python lamp_bridge.py
```

This project already ships a Python with pyserial in it, so this also works
without installing anything new:

```bash
~/.platformio/penv/bin/pip install requests
~/.platformio/penv/bin/python bridge/lamp_bridge.py
```

**Keeping internet while on the AP:** in System Settings the Mac will warn the
sign's network has no internet. That is expected. If you need internet at the
same time, plug in ethernet or a USB-C dock — macOS will use the wired link for
internet and Wi-Fi for the sign.

## Assigning cards

Cards are bound to segments from the sign's LOGO tab, or over its console:

```
logo learn 3        # then tap a card - it is assigned to segment 3 and saved
logo list           # shows every segment, its card and whether it is lit
```

Nothing has to change on the Arduino or in this script when cards are
reassigned - the mapping lives on the ESP32.

## Checking it by hand

Any browser on the sign's network can do what the bridge does:

```
http://192.168.4.1/api/cmd?c=logo+card+A1B2C3D4
http://192.168.4.1/api/cmd?c=logo+reset
http://192.168.4.1/api/cmd?c=s
```
