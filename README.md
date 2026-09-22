# INNOV+IOT LED Sign Controller

ESP32 + WS2812B controller for the **INNOV+IOT** sign (letters + logo), with
per-letter LED ranges, a divided logo that builds up piece by piece, an animation
engine, a debug console, and a black & white browser UI.

Control is over USB serial — one line-based command protocol, spoken both by the
browser UI (Web Serial API) and by any plain serial monitor.

```
webapp/index.html  ──USB serial, 115200, line protocol──▶  ESP32 ──▶ WS2812B strip
```

A Wi-Fi transport for the same protocol is written and parked in `extras/wifi/`;
see [Re-enabling Wi-Fi](#re-enabling-wi-fi).

## Layout

| Path | What it is |
|---|---|
| `webapp/index.html` | the control UI — open it in Chrome/Edge/Opera |
| `src/main.cpp` | boot + main loop |
| `include/Config.h` | pins, LED count, segment/animation types |
| `src/SegmentManager.cpp` | segment table, validation, NVS persistence, JSON |
| `src/AnimationEngine.cpp` | the 13 animation renderers |
| `src/LedController.cpp` | FastLED output, frame composition, test overlay |
| `src/DebugConsole.cpp` | serial menu + machine protocol |
| `src/Logger.cpp` | ring-buffer logger |
| `extras/` | the original ESP-NOW master sketch, kept for reference |
| `extras/wifi/` | the Wi-Fi transport, parked (not built) |

## Wiring

| Signal | ESP32 |
|---|---|
| WS2812B DIN | GPIO13 through a 330 Ω resistor |
| WS2812B 5 V / GND | external 5 V PSU — **tie PSU GND to ESP32 GND** |
| 1000 µF cap | across the strip's 5 V / GND, close to the first LED |

Defaults: 300 LEDs, 8 A power budget (`ma` command). FastLED scales brightness
down automatically to stay inside that budget.

## Build & flash

```bash
pio run -t upload
```

## Using the UI

1. Plug the ESP32 in, close any serial monitor (only one program can hold the port).
2. Open `webapp/index.html` in Chrome, Edge or Opera.
3. Click **CONNECT SERIAL PORT** and pick the `usbserial` / `CP210x` / `CH340` port.

The four tabs:

- **CONTROL** — power, brightness, speed, intensity, animation picker, play mode.
- **SEGMENTS** — proportional preview of the strip plus the editable segment table
  (name, start, end, colours, per-letter animation, brightness, enable, reverse).
- **MAPPING** — the tool for step 1 below.
- **DEBUG** — live telemetry, full serial console with history, config save/export/import.

> Opening the port resets the ESP32 (DTR/RTS auto-reset), so the sign restarts
> when you connect. That is normal.
>
> While another program holds the port the board gets no commands and the strip
> keeps showing its last frame — WS2812s latch. A sign frozen on one colour is
> usually this, or a test overlay (below), not a dead controller.

## 1. Defining each letter's LED range

The sign's default layout is `I N1 N2 O V PLUS I2 O2 T LOGO`, 30 LEDs each.
To match the real board:

1. **MAPPING → START WALK** — one LED marches along the strip, with dim markers
   every 10 LEDs. Watch for where a letter begins and ends.
2. Note the index (shown live as *walk index*), or step manually with the
   single-LED ← / → buttons.
3. Pick the segment, hit **USE CURRENT AS START** / **USE CURRENT AS END**, then
   **ASSIGN RANGE**. **PREVIEW** lights the range, **BLINK SEGMENT** flashes it.
4. **SAVE TO FLASH** when the whole map is right.

Ranges are inclusive: `I = 20-100` lights LEDs 20 through 100. The SEGMENTS tab
flags overlaps and out-of-range values as warnings under the table.

Equivalent console commands:

```
test walk 250
seg range I 20 100
seg range N1 101 150
save
```

## 2. Animations

`OFF SOLID BREATHE PULSE TRAVERSE COMET WIPE THEATER SPARKLE RAINBOW GRADIENT
STROBE FIRE TWINKLE BUILD STEPS`

`BUILD` and `STEPS` drive the sub-sections described below.

Each segment either follows the global animation (`INHERIT`) or runs its own, so
the logo can pulse while the letters traverse. Three play modes decide how
segments relate in time:

- **PARALLEL** — every letter in phase.
- **STAGGER** — each letter offset by `stagger` ms, giving a letter-by-letter wave.
- **SEQUENCE** — one letter lit at a time, round-robin.

`speed` sets the rate, `intensity` the animation-specific depth (breath depth,
band width, comet tail, spark density, fire liveliness), `tint on` overlays a
rainbow hue sweep on top of whatever is running.

## 3. The logo's sub-sections

Any segment can be split into up to 16 sub-sections; the logo defaults to **10**.
`BUILD` and `STEPS` walk those pieces one slot at a time and then finish on an
all-glow step that holds for two slots before repeating:

```
slot:    0    1    2   ...   9      10  11
piece:  [0]  [1]  [2]  ...  [9]    all glow, breathing
```

- **BUILD** keeps each piece lit as the next arrives — the logo draws itself in.
- **STEPS** shows one piece at a time — a chase around the logo.

Set it up in **SEGMENTS → Sub-sections**: pick the segment, set the count, hit
**SET & SPLIT EVENLY**. Pieces start as an even split; edit any start/end to match
the real artwork and the seam with its neighbour moves with it. **LIGHT** previews
one piece on the board.

Console equivalents:

```
seg div LOGO 10              # 10 pieces, split evenly
seg anim LOGO BUILD          # or STEPS
seg divrange LOGO 3 288 292  # hand-place piece 3
seg divlist LOGO             # show the current split
seg divauto LOGO             # back to an even split
test div LOGO 3              # light piece 3 only
```

Step timing follows the global `speed`.

## 4. Debug console

Same command set over USB serial (115200) or the DEBUG tab. `h` prints the menu.

```
STATE     s (status)   l (list segments)   v (validate)
POWER     on | off | bright <0-255> | speed <1-255>
LOOK      anim <name> | intensity <0-255> | tint on|off
          mode <parallel|stagger|sequence> | stagger <ms>
SEGMENTS  seg add <name> <start> <end>       seg range <i|name> <start> <end>
          seg color <i|name> <#RRGGBB|red|r,g,b>   seg color2 ...
          seg anim <i|name> <name|inherit>   seg bright <i|name> <0-255>
          seg on|off|rev|del|id|name <i|name>      seg sort
SECTIONS  seg div <i|name> <count>          seg divlist <i|name>
          seg divrange <i|name> <k> <start> <end>   seg divauto <i|name>
MAPPING   test index <n> | test range <a> <b> | test div <seg> <k>
          test walk [ms] | test all | test off
CONFIG    save | load | defaults | ledcount <n> | ma <n>
PROTOCOL  json (full state) | stat (telemetry) | hello
SYSTEM    h | m | clear | reboot
```

Segments can be addressed by index *or* name: `seg color LOGO #00A8FF`.

## Serial protocol (what the web app speaks)

Plain text in, plain text out. Machine-readable replies are a single line
starting with `#J` followed by compact JSON — everything else is human log
output, so a normal serial monitor stays readable.

```
> hello
#J{"type":"hello","device":"INNOV+IOT SIGN","fw":"1.0.0","protocol":1,...}
> stat
#J{"type":"status","fps":96,"ma":3174,"heap":315736,"testMode":"none",...}
> json
#J{"type":"state","global":{...},"segments":[...],"animations":[...],"issues":[...]}
```

The UI polls `stat` every 800 ms and re-reads `json` after any change.

## Persistence

Config lives in NVS. Changes autosave 30 s after the last edit, or immediately on
`save` / **SAVE TO FLASH**. `defaults` restores the factory layout (not saved
until you save). **EXPORT JSON** downloads the config; **IMPORT JSON** replays it
as console commands.

## Test overlays

`test index / range / div / walk / all` paint over the animation on every frame
until cleared — that is the point of them, but a forgotten overlay looks exactly
like a broken controller. Two guards: the UI shows a
**TEST OVERLAY — CLICK TO CLEAR** banner in the header whenever one is running,
and the firmware releases any overlay by itself after two minutes
(`TEST_TIMEOUT_MS`). `test off` clears one immediately.

## Re-enabling Wi-Fi

`extras/wifi/` holds a complete Wi-Fi transport for this same protocol: AP with
STA fallback, the UI served from flash gzipped, and a websocket at `/ws` routed
through the same `Console.execute()`, so there is no second API to maintain. To
switch it on:

1. `cp extras/wifi/Net.cpp src/` and `cp extras/wifi/Net.h include/`
2. `cp extras/wifi/embed_webapp.py scripts/`
3. In `platformio.ini` add `board_build.partitions = huge_app.csv`,
   `extra_scripts = pre:scripts/embed_webapp.py`, and the `AsyncTCP` +
   `ESPAsyncWebServer` deps
4. Call `Net.begin()` / `Net.loop()` from `main.cpp`

`WifiSettings` is still in the config struct, so turning it back on will not
invalidate saved settings.

BLE was the other option and is the weaker one here: its ~20-byte MTU makes the
JSON state chatty enough to need chunking, Web Bluetooth is Chromium-desktop-only,
and BT Classic SPP isn't reachable from a browser at all.

## Notes & limits

- Web Serial is Chromium-only — Chrome, Edge, Opera and Arc. Not Firefox or Safari.
- `MAX_LEDS` is 1200 (buffer ceiling); the active count is set at runtime with
  `ledcount` and only that many LEDs are clocked out, which is what keeps the
  frame rate up.
- `MAX_SEGMENTS` is 24, `MAX_DIVISIONS` is 16 per segment.
