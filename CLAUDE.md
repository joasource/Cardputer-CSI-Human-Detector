# CLAUDE.md — WiFi-CSI Radar

> Context handoff for continuing this project in Claude Code / VS Code.
> Standalone project — a handheld WiFi-CSI people sensor with a dual-screen UI.

## Goal
A **handheld device that senses people through WiFi** using Channel State
Information (CSI). Realistic target: on-device **presence + motion**.

## Architecture (current: everything on the Cardputer)
- **Device:** **Cardputer ADV** (ESP32-**S3**, StampS3, no PSRAM). It joins the
  home WiFi as a station and measures CSI itself (promiscuous mode, frames of
  every transmitter on the channel). One build env: `cardputer-radar-csi`
  (`RADAR_CSI=1`). WiFi creds are picked on-device and stored in NVS
  (`wificreds`); no creds in the repo.
- **CSI pipeline** (`src/main.cpp`, `csiCallback` → `serviceCsi`): mean
  amplitude + mean sin(phase) per frame over a 50-frame window → variance
  (`gCsiAmpVar`, `gCsiPhaVar`). `serviceCsi` scores each against an
  empty-room baseline: `score = ln(var / ceiling) / ln(20)`, clamped 0..1,
  motion = max(amplitude, phase). 0 % = as quiet as the empty room; threshold
  35 % = 2.9x the ceiling, 100 % = 20x (`kCsiFullScale`).
  Presence = motion > `gThreshold` (user-set, 5–95 % in 5 % steps, NVS
  `radar/thr`), then held 10 s. Reported motion is the raw score (no fade),
  so the graph shows exactly what the threshold line is compared against.
  `serviceCsi` injects `R,<seq>,<pres>,<motion>,<rssi>,RUN|CAL` lines into
  `RadarLink` at 15 Hz (legacy wire format, now internal only).
- **Calibration** (`startCalibration` / `finishCalibration`): at boot, after
  joining a network (`enableCsi`), on `c` and on the phone's CAL button.
  5 s settle (step away; also waits for 50 new frames) + 8 s collect (≥ 30
  ticks that saw new frames). Ceiling = level + max(2σ, ln 1.5) from the
  10th/30th percentiles of ln(var): motion only adds variance, so the low
  side tolerates someone walking off for the first ~3 s (median/MAD broke
  there in simulation). Without CSI frames it waits ("waiting for WiFi
  traffic"). Progress in `gCalPct` (-1 = idle); screens, scope status bar and
  phone show CALIBRATING + %. Serial logs the ceilings/σ when done.
  Tunables still unvalidated on hardware: `kCsiFullScale` (20),
  `kCalMinSpread` (0.40), settle/collect times.
- **Spy-cam markers:** `sniffCallback` matches transmitter OUIs (ESP32, Wyze,
  Ring, Hikvision…) and shows them as amber camera icons.
- **Displays (different content per screen, NOT a clone):**
  - Top = external **2.8" ILI9341** (320x240 @ rotation 7): PPI **radar scope**
    or vaporwave 3D view (`.` toggles). Blips: radius from RSSI, size/heat
    from motion; angle is random (single antenna = no bearing).
  - Bottom = built-in 1.14" (240x135): WiFi IP, status pill, CONTACT/CLEAR
    banner, motion graph, footer + phone-viewer count.
  - **Phone view:** the Cardputer serves `web/index.html` on port 80
    (`http://<ip>/`, `http://radar.local/`, QR card at boot and on `q`) and
    streams frames over SSE (`/ev`); the phone redraws the top scope itself.
    For units with no external TFT: menu EXT PANEL = OFF (no top rendering,
    86 KB top canvas freed).
- **Removed** (commit 46a038e): the separate S3 DevKit sensor (`SQUACHNET` AP,
  UDP 4210) and `tools/`. Leftover `RADAR_UDP` / UART code paths in
  `main.cpp` have no build env.

## Build / run
- `pio run -e cardputer-radar-csi -t upload` — the only env.
- Release: pushing a `v*` tag runs `.github/workflows/release.yml`, which
  builds and merges a flash-at-0x0 `.bin` for the web flasher.
- Phone page preview without hardware: open `web/index.html` from disk
  (or add `?demo`) — runs on fake data.
- Cloud sessions: the PlatformIO registry (`api.registry.platformio.org`,
  `dl.registry.platformio.org`) has been blocked by the environment network
  policy. Workaround used on 2026-11-02: platform-espressif32 v6.12.0, the
  Espressif toolchains (crosstool-NG esp-2021r2-patch5), arduino-esp32 2.0.17,
  esptool v4.9.0, SCons 4.11.1 and the M5 libs fetched from GitHub/PyPI into
  `~/.platformio`, plus a scratch ini that `extends` this env with
  `symlink://` lib_deps. Or allow those hosts in the environment settings.

## Hard constraints (don't regress these)
- Build sets `ARDUINO_USB_CDC_ON_BOOT=1` → **`Serial` is USB-C**. Any sensor
  link MUST use a HardwareSerial (`Serial1`), never `Serial`.
- **No PSRAM** (StampS3). Don't allocate a full 320x240 sprite. Pattern: render
  at 240x180 and scale-to-fit (`pushRotateZoom`). Budget: bottom canvas
  240x135 (~65 KB) + top canvas 240x180 (~86 KB, only while EXT PANEL > OFF).
- External panel = `include/ext_panel.h`: ILI9341, rotation 7,
  CS5/RST3/DC6/MOSI14/SCK40, SPI2_HOST **shared with SD** (`bus_shared=true`),
  `rgb_order=false`, 27 MHz.
- Display libs: **M5Unified + M5Cardputer (M5GFX/LovyanGFX)**, Arduino framework,
  platform espressif32@6.12.0, board m5stack-stamps3.
- Web server must never block `loop()`: every recv/send is `MSG_DONTWAIT`.

## Files
- `src/main.cpp` — CSI callbacks, `serviceCsi()`, spy-cam sniffer, WiFi picker,
  settings menu, `drawBottom()`, `drawTop()` (PPI), `drawRaycaster()` (3D),
  phone-view glue (`serviceWeb()`, `drawQr()`), `serviceKeys()`.
  Bottom @30 fps, top @~8 fps.
- `include/radar_link.h` — line parser + state + 240-sample motion history.
  `injectLine()` is how `serviceCsi` feeds it. Its UART command senders
  (`calibrate()`, `setThreshold()`) are no-ops in the CSI build; there `c`
  and the threshold act on-device (`requestCalibration()`, `gThreshold`).
- `include/ext_panel.h` — external ILI9341 panel class.
- `include/web_view.h` — dependency-free HTTP + SSE server on raw lwIP sockets.
  Routes: `/`, `/ev`, `/cmd?k=dn|up|pal|cal`. Max 5 sockets / 3 streams (oldest
  evicted), idle preconnects recycled, streams stalled > 6 s dropped.
- `web/index.html` — phone page, linked into flash via
  `board_build.embed_txtfiles` (symbol `_binary_web_index_html_start`).
  SSE events: default frame `{"q":seq,"p":0|1,"m":motion‰,"r":rssi,"t":thr%}`
  (+ `"c":progress%,"cw":0|1` while calibrating; `cw` = no CSI frames),
  `st` (1 Hz: ip, ssid, ch, bat, chg, palette, CSI fps, viewers, cams),
  `hist` (240 samples, 0..100, on connect).
- `src/DC34.html` — unrelated standalone page ("Talking Sasquach // Field
  Unit"), not part of the firmware.

## Status
✅ On-device CSI presence/motion, dual-screen UI, spy-cam markers, WiFi picker.
✅ Real threshold + calibration (2026-11-02): compiles (RAM 20.7 %, flash
   32.5 %). `serviceCsi`/calibration code tested on Linux in a harness with
   synthetic CSI frames (empty room, walking, small motion vs thr 20/95 %,
   disturbed calibration, no traffic, 8 fps, WiFi drop). Phone CAL button /
   CALIBRATING banner tested in headless Chromium. NOT tested on hardware:
   real CSI noise may need `kCsiFullScale` / `kCalMinSpread` retuned (read
   the `# CSI calibration done` serial line).
✅ Phone view (2026-11-02): compiles for ESP32-S3 (RAM 20.3 %, flash 32.4 %);
   `web_view.h` tested on Linux with a socket shim + Chromium (live stream,
   commands, reconnect, stalled/idle/excess/RST clients). Page tested in
   headless Chromium, portrait + landscape.
⚠️ Phone view NOT yet tested on a real Cardputer / real phones: QR scan,
   `radar.local` (often fails on Android → use the IP), iOS Safari SSE, heap
   headroom with the external panel ON.
⚠️ CSI ignores frames from stations that send to the Cardputer (the phone),
   so a phone moving in hand doesn't read as motion. Effect not measured on
   hardware. If the Cardputer joins the phone's own hotspot, the phone IS
   the AP and its frames are still used → keep it still.

## Known issues (found, not fixed)
- Phone page overflows horizontally on small landscape phones (667x375,
  568x320): the 4-column `#stats` grid ("CONTACTS"/"THRESH" labels) is wider
  than the panel. Pre-existing; 844x390 and portrait are fine.
- CSI is taken from every transmitter on the channel; a change in the
  traffic mix (a new device starts talking) can raise the variance without
  motion. If false CONTACTs show up on hardware, a per-transmitter baseline
  (or AP-only CSI) is the next fix.
- `RADAR_FAKE=1` doesn't compile: `serviceFake()` uses `camBlips` before its
  declaration (pre-existing).
- After a WiFi drop, `serviceCsi()` sets `wifiReady = false` and nothing
  re-enables CSI until the WiFi picker runs or the device reboots.

## Next steps (priority order)
1. **Hardware test of the phone view** (see ⚠️ above); check free heap.
2. **Hardware test of calibration/threshold:** empty-room vs walking scores,
   tune `kCsiFullScale` / `kCalMinSpread` from the serial ceilings/σ.
3. **Auto-recover CSI after WiFi reconnects** (`enableCsi()` already starts
   a fresh calibration).
4. **Port sensor to ESP32-C5** (MonsterC5): better CSI PHY (Wi-Fi 6), but
   `wifi_csi_acquire_config_t` is different — missing fields like `lltf_en`,
   `sig_mode`, `secondary_channel`, `ant`.
5. **Real bearing** via a 2nd antenna (RSSI/phase delta) → blip angle becomes
   meaningful, turning the scope into an actual direction finder.
6. **Presence event log** on the bottom screen / phone, optional SD persistence.

## Hardware inventory (chip cheat-sheet)
- C5 (best CSI): MonsterC5, AWOK Dual Touch (2x each), Marauder v8.
- C3: BLEShark Nanos (many; mesh-capable).
- S3: Cardputer ADV (console, COM18); S3 DevKit (former sensor, COM44).
- Classic ESP32: Cheap Yellow Displays, HaleHound (CYD-based).
- Not ESP32: Cardputer Zero (Raspberry Pi CM Zero), Hackberry Pi, HackRF/PortaPack.
