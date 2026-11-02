# CLAUDE.md — WiFi-CSI Radar

> Context handoff for continuing this project in Claude Code (local CLI with
> the Cardputer on USB, or a cloud session) / VS Code.
> Standalone project — a handheld WiFi-CSI people sensor with a dual-screen UI.

## Handoff — where things stand (2026-11-02)
- The user writes in **Portuguese (pt-BR)**; answer in Portuguese. Keep code,
  commits and this file in English.
- `main` @ `7ed3bc9` = PR #1 merged: phone view + real threshold/calibration.
  Everything below describes `main`. Start new work on a branch off `main`.
- **Nothing in PR #1 has run on hardware yet.** It compiled for ESP32-S3 and
  was tested on Linux harnesses + headless Chromium only. A local session
  with the Cardputer plugged in can finally flash it and read the serial
  log: do the **Hardware test checklist** below before new features.
- **Release `v0.2.0` exists** (tag on `7ed3bc9`, asset
  `cardputer-csi-v0.2.0.bin` for the web flasher). To rebuild it without
  touching the tag: `gh workflow run release.yml -f tag=v0.2.0`
  (`workflow_dispatch`; re-running updates the existing release asset).
  A force-push of a tag at the same SHA does NOT trigger a new run.

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
- `cardputer-radar-csi` is the only env (and the default):
  - build: `pio run -e cardputer-radar-csi`
  - flash: `pio run -e cardputer-radar-csi -t upload --upload-port <PORT>`
    (find the port with `pio device list`; the native USB of the S3 shows up
    as a USB-CDC/JTAG device, no driver needed on Windows 10+)
  - serial: `pio device monitor -b 115200`
- **Local CLI gotchas:**
  - PlatformIO installed only through the VS Code extension is not on PATH.
    On Windows use `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"`
    (PowerShell), on Linux/macOS `~/.platformio/penv/bin/pio`, or
    `pip install platformio`. First build downloads the toolchain (minutes).
  - `pio device monitor` never exits: run it as a background command (or with
    a timeout) and read its output. It holds the port, so **stop the monitor
    before uploading** or the upload fails with "port busy".
  - If upload can't connect: hold **G0** while plugging the USB-C cable
    (download mode), upload, then unplug/replug to run.
  - The first boot after a fresh flash opens the on-device WiFi picker and
    waits for the keyboard: the user has to pick the network on the device.
- Serial lines worth grepping (`#` prefix = log, not data):
  `# CSI WiFi: <ssid>  IP:<ip>`, `# CSI active`,
  `# Phone view: http://<ip>/  (http://radar.local/)`,
  `# CSI calibration: keep the room still`,
  `# CSI calibration done: N samples, amp ceil X (sigma Y), pha ceil X (sigma Y)`.
- Release: pushing a `v*` tag (or running the workflow manually with a `tag`
  input) runs `.github/workflows/release.yml`, which
  builds and merges a flash-at-0x0 `.bin` for the web flasher
  (esptool.spacehuhn.com, address `0x0`).
- Phone page preview without hardware: open `web/index.html` from disk
  (or add `?demo`) — runs on fake data.
- Cloud sessions: the PlatformIO registry (`api.registry.platformio.org`,
  `dl.registry.platformio.org`) has been blocked by the environment network
  policy. Workaround used on 2026-11-02: platform-espressif32 v6.12.0, the
  Espressif toolchains (crosstool-NG esp-2021r2-patch5), arduino-esp32 2.0.17,
  esptool v4.9.0, SCons 4.11.1 and the M5 libs fetched from GitHub/PyPI into
  `~/.platformio`, plus a scratch ini that `extends` this env with
  `symlink://` lib_deps. Or allow those hosts in the environment settings.
  Cloud sessions can push only their assigned branch (no tags, no `main`).

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
- No free-heap logging exists anywhere in the firmware (needed for the
  hardware test, see checklist).
- Stale leftovers from the original setup: `.claude/settings.json` allows
  only two PowerShell `pio.exe` commands for envs that no longer exist
  (`sensor-s3`, `cardputer-radar-wifi`) under another machine's path; the
  `platformio.ini` header still mentions a `credentials.ini` that is no
  longer used (creds live in NVS). Safe to clean up.

## Hardware test checklist (do first, needs the device + a phone)
1. Flash `main`, open the monitor, pick the WiFi on the device (2.4 GHz).
   Expect `# CSI WiFi`, `# CSI active`, `# Phone view: http://<ip>/`.
2. **Calibration:** leave the room during the ~13 s CALIBRATING. Record the
   `# CSI calibration done` line (ceilings + σ). Repeat 2–3 times (`c`) and
   check the ceilings are stable.
3. **Threshold:** empty room → motion stays well under 35 % (no CONTACT);
   walking 2–5 m away → clearly above. If empty-room noise reaches the line
   or walking barely crosses it, tune `kCsiFullScale` (20) /
   `kCalMinSpread` (0.40) in `src/main.cpp` from the recorded σ.
4. **Phone view:** scan the boot QR (Android Chrome + iOS Safari if
   available), try the IP and `http://radar.local/`, press THR−/THR+/CAL/
   palette on the phone and confirm the Cardputer follows; footer shows the
   phone count; reload/lock the phone and check it reconnects.
5. **Heap:** add a temporary 1 Hz log of `ESP.getFreeHeap()` /
   `ESP.getMinFreeHeap()` (e.g. next to the `st` event in `serviceWeb()`),
   run with EXT PANEL ON and 3 phones connected, note the minimum, then
   remove it or keep it behind a flag.
6. Write the results into **Status** below (replace the ⚠️ lines).

## Next steps (priority order)
1. **Hardware test of the phone view** (checklist steps 1, 4, 5).
2. **Hardware test of calibration/threshold** (checklist steps 2, 3):
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
  COM numbers are from the original dev machine; check `pio device list`.
- Classic ESP32: Cheap Yellow Displays, HaleHound (CYD-based).
- Not ESP32: Cardputer Zero (Raspberry Pi CM Zero), Hackberry Pi, HackRF/PortaPack.
