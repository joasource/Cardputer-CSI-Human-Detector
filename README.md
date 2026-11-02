# Cardputer Human Detector

This tiny cyber deck is detecting people using Wi-Fi. No camera. No IR sensor. No thermal camera. Just Wi-Fi and this deeply cursed little radar screen.

It uses Channel State Information (CSI), specifically the same Wi-Fi your router is already blasting everywhere, to detect when people are moving around. The human body is mostly water, and water messes with radio waves in very measurable ways. So we measure them.

The whole build is around $50. Cardputer ADV is $30, grab [two of these TFT screens for $20](https://a.co/d/0bZbfTqO), find somebody with a 3D printer, and you've got yourself a handheld human radar that looks absolutely unhinged. I'm really, really happy with how this came out.

---

## How it works

Wi-Fi signals bounce off everything in a room: walls, furniture, doors, your cat, and that weird little flesh bag you call your body. CSI data captures the *fingerprint* of those reflections. When something moves, that fingerprint changes. This firmware tracks those changes and uses them to detect presence and motion in real time.

It runs entirely on the Cardputer ADV. No external sensor, no second device. The ESP32-S3 is dual-core, so one core handles the sensing while the other drives the screens. Everything is self-contained.

---

## Hardware

| Board | Role |
|-------|------|
| M5Stack Cardputer ADV (ESP32-S3 / StampS3) | Console UI + CSI sensor |

An external [ILI9341 TFT display](https://a.co/d/0bZbfTqO) is wired to the Cardputer ADV GPIO header for the radar scope screen.

---

## Displays

Both screens are running at the same time showing different things (not a clone, actual different content):

- **Top, external ILI9341:** PPI-style radar scope. Rotating sweep with a phosphor trail. When motion crosses the threshold it paints contact blips; radius from RSSI, heat and size from motion intensity.
- **Bottom, built-in screen:** WiFi IP, status pill, PRESENCE / CLEAR banner, scrolling motion graph, threshold value, and key hints.

---

## No external screen? Use your phone

The Cardputer also serves the radar scope as a web page, so your phone can be the second screen. No app to install: it's just a page in the phone's browser.

1. Make sure your phone is on the **same WiFi** as the Cardputer.
2. After boot, the built-in screen shows a **QR code**. Scan it with the phone camera (or press `q` to bring it back).
3. You can also type the IP shown in the title bar, e.g. `http://192.168.1.42/`, or try `http://radar.local/`.

The phone draws the full scope itself (PPI sweep and the vaporwave 3D view, so tap the scope to switch) plus the motion graph, threshold buttons, palette switch, and an optional sonar sound. Only a few numbers per frame cross the WiFi (~15 updates/s over Server-Sent Events), so it stays smooth.

If you don't have the external TFT at all, open the settings menu (`` ` ``) and turn **EXT PANEL** down to **OFF**. The Cardputer then stops rendering the top scope and frees its ~86 KB frame buffer.

Want to tweak the page without flashing? Open `web/index.html` straight from disk in a browser (or add `?demo` to the URL) and it runs on fake data.

---

## Flash the pre-built binary (easiest)

Don't want to deal with PlatformIO? Grab the latest `.bin` from the [Releases page](../../releases) and flash it in your browser using [ESP Web Flasher](https://esptool.spacehuhn.com/):

1. Plug in your Cardputer ADV via USB-C
2. Go to [esptool.spacehuhn.com](https://esptool.spacehuhn.com/)
3. Click **Connect** and select your device
4. Upload the `.bin` from the latest release at address `0x0`
5. Done

On first boot it'll scan for networks, let you pick one, and ask for the password. Credentials get saved to the device so it won't ask again.

---

## Build from source

Requires [PlatformIO](https://platformio.org/).

```sh
pio run -e cardputer-radar-csi -t upload
```

---

## Keyboard controls

| Key | Action |
|-----|--------|
| `c` | Trigger calibration (empty-room baseline) |
| `,` | Decrease detection threshold |
| `/` | Increase detection threshold |
| `.` | Switch the top scope between PPI and 3D view |
| `q` | Show the QR code / URL for the phone view |
| `` ` `` | Open settings menu |

---

## Project structure

```
├── platformio.ini          Build config
├── partitions.csv          8MB dual-OTA partition table
├── include/
│   ├── radar_link.h        Protocol parser, state, 240-sample motion history
│   ├── ext_panel.h         External ILI9341 panel driver
│   └── web_view.h          Tiny non-blocking HTTP + SSE server for the phone view
├── web/index.html          Phone view page (embedded into the firmware at build time)
└── src/main.cpp            Dual-screen radar UI, CSI callbacks, key handling
```

---

## Dev notes

- `ARDUINO_USB_CDC_ON_BOOT=1`: `Serial` is the USB-C port.
- Platform: `espressif32@6.12.0`, board `m5stack-stamps3`, Arduino framework.

---

## Roadmap

1. Calibration wizard with on-screen progress indicator
2. Dual-antenna bearing estimation toward directional blips on the radar scope
3. Presence event log with optional SD card persistence

---

## The terrifying part

There's almost nothing you can do to protect yourself from this. Wi-Fi goes through walls, floors, and pretty much anything else, because that's the whole point of Wi-Fi. This technology holds a ton of promise for things like elderly fall detection and hospital monitoring, but the privacy implications are real.

I built this to show that you don't need a $60 billion government contract. You just need $50 and a little bit of tinkering time to build something that does what seemingly should be impossible.

Are we okay with this? Drop a comment on the video and let me know.
