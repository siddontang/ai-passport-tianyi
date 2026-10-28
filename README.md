# AI Passport — Tianyi Pixel

Custom firmware for the **FoloToy AI Passport (ESP32-C3, 8 MB flash, no PSRAM)**,
with a Tianyi-inspired pixel companion, continuous voice conversations and Chinese
subtitles. The current source version is **2.2.1-tianyi.pixel.21**; its battery,
onboarding, settings and inherited reconnect changes still need physical-device testing.

[Chinese README](README.zh-CN.md) · [Board and controls](firmware/boards/ai-passport/README.md) ·
[Realtime implementation](firmware/components/agent_link/REALTIME.md)

<img src="firmware/apps/ai-passport-wifi/avatar/tianyi-pixel-expressions.png" width="320" alt="Four Tianyi-inspired pixel expressions: idle, blink and two speaking poses">

## What is included

- Four 64x64 pixel expressions, blinking, listening feedback and mouth movement
  driven by speaker audio. Rendering reuses a 240x4 DMA stripe without a framebuffer.
  Pixel.21 uses the approved twin crown-loop hairstyle, curved side locks and bows.
- The upstream C3 Opus audio path, shared codec workspace and incremental TLS I/O.
- A periodic WebSocket Pong that keeps long spoken replies alive with the gateway's
  Pong-based heartbeat, including when incoming control frames wait behind audio.
- Immediate user transcripts and assistant subtitles paced by released PCM samples,
  with queued audio and the I2S tail accounted for.
- Chinese font assets, avatar/font generation tools and host regression tests.
- Chinese/English menus, setup and activation guidance, including code expiry,
  using the existing subtitle font for Chinese and UTF-8 conversation titles.
- Persistent language selection and a Wi-Fi-only reset with a confirmation page;
  both settings are accessible before the device is online.
- Battery icon and percentage on the portrait, settings and setup screens, with
  a low-battery color at 20% or below. The CW2017 is sampled every five seconds;
  battery-only updates transfer eight header stripes without repainting captions.
- Bounded transport recovery that keeps the call open during retry and resumes
  capture after a fresh grant; safe pagination and server-event diagnostics.

Subtitle pacing is approximate: the current protocol supplies no word timestamps.
The bounded text window holds 1,024 UTF-16 units and reclaims displayed prefixes
on cumulative updates; an unusually large single advance transcript can overflow it.
Mouth animation follows audio amplitude, not phonemes.

Battery readings retain the gauge's existing cell profile. An unavailable,
uncalibrated or not-ready gauge displays `--%`; an invalid reading is never
converted to a false 0% or 100%. A configured sleeping gauge is woken at startup.
No new cell calibration or charging-state inference is applied. Sampling runs in
the control task with bounded I2C transactions, outside audio and rendering tasks.

## Build and flash

Use an activated **ESP-IDF 5.5.4** environment (the version used for device
validation), Python and the ESP-IDF build dependencies:

```sh
git clone https://github.com/siddontang/ai-passport-tianyi.git
cd ai-passport-tianyi
python firmware/tools/build_firmware.py ai-passport-wifi --jobs 2
```

Connect the AI Passport with a USB data cable, power it on and identify its serial
port. Flash the separate build segments to preserve existing NVS settings:

```sh
idf.py -C firmware -p PORT flash monitor
```

Replace `PORT` with this device's serial port. Do not erase the chip or substitute
another board's partition layout. Build outputs and device backups are excluded
from this repository. Configure WiFi through the device's `AIPassport-XXXX` hotspot
at `http://192.168.4.1`, then activate and bind the device in the
[Deotaland console](https://bot.deotaland.cn).

## Use and Agent configuration

- Short-press OK to start or end a call; UP/DOWN adjusts volume.
- Long-press OK for the settings menu, then choose its second entry (New conversation) to start a fresh
  conversation. Existing conversations remain available.
- Choose the third entry (Change Wi-Fi), then Clear and restart to reopen setup.
  Cancel is selected by default. This removes only saved Wi-Fi credentials; the
  device identity, Agent binding, saved conversation and language are retained.
- Choose the fourth entry (Language), then Chinese or English. Chinese is the
  default; selection survives restart and sets the default language for newly
  opened phone setup pages. The phone page also has its own language toggle.
  This changes interface labels, not the Agent's name, speech or transcripts.
- Set the Agent's name and identity in its console persona. After changing persona,
  create a new conversation: existing sessions may retain their previous instructions.
  The device's new conversation was confirmed to use the chosen name, **Tiantian**.

The character artwork does not change the voice preset. A Luo Tianyi-like voice
has **not** been implemented: the actual voice is selected by the service provider,
and the current console exposes no per-Agent voice selector. A speaking-style prompt
alone does not select another preset. No Agent Stack server code is included here.

## Validation

Pixel.21 changes the portrait atlas only, retaining the 32 KB flash asset size
and existing face-patch rendering. Production-renderer checks cover all four
expressions, incremental transfers, subtitles and battery updates in both languages.
Voice, recovery, battery sampling and settings behavior are inherited unchanged.

Pixel.20 passes the AI Passport WiFi ESP-IDF build, production CW2017 driver tests
(wake/read failures, retained calibration, 0/100% and invalid readings), control
polling tests and rendered battery cases in both languages. The real render loop
verifies that a battery-only update transfers exactly eight stripes (15,360 bytes)
and leaves all pixels below the header unchanged. Avatar, controls, captions,
recovery and settings regressions pass. Physical battery acceptance is pending.

Pixel.19 passed the AI Passport WiFi ESP-IDF build and host regressions for
language persistence, actual menu confirmation/cancellation, Wi-Fi namespace
isolation and storage failures. Production renderers cover bilingual onboarding,
activation expiry, offline menus and Tianyi subtitles; the real portal handler
is checked in both languages, including template-like SSIDs. Regression tests also
exercise the actual recovery worker, app call control and microphone loop, covering both
retry delays, resumed capture, exhaustion, grant failure, user cancellation and
state-transition races. Chinese menus are rendered with the production code and
checked for UTF-8 boundaries and available glyphs. Avatar, caption, keepalive,
platform pagination and codec lifecycle checks also pass. This version has not
been flashed or accepted on a physical device.

Transport recovery uses at most two retries per call, after 2 and 4 seconds;
interrupted speech is discarded. Explicit server termination, grant rejection
and loss of the platform connection still end the call. Recoverable server errors
marked with JSON `recoverable: true` keep the existing call alive.

Pixel.16 passed its ESP-IDF build and host checks for rendering, captions,
platform APIs, codec lifetime, keepalive and recovery. Physical testing confirmed
working audio, connection and progressive Chinese subtitles. The recorded acceptance
included a complete 43.36-second reply and at least 100.46 seconds of connection;
this is a bounded device test, not a claim of indefinite stability.

From an activated ESP-IDF environment, relevant checks include:

```sh
python firmware/tools/build_firmware.py --check
python tools/board_catalog.py --check
python tools/check_docs.py
python firmware/tests/test_passport_avatar.py
python firmware/tests/test_passport_menu.py
python firmware/tests/test_passport_battery.py
python firmware/tests/test_passport_settings.py
python firmware/tests/test_passport_wifi.py
python firmware/tests/test_passport_recovery.py
python firmware/tests/test_realtime_captions.py
python firmware/tests/test_realtime_keepalive.py
python firmware/tests/test_realtime_recovery.py
python firmware/tests/test_platform_api.py --idf-path "$IDF_PATH" --c3 --voice-memory
```

Avatar previews and asset regeneration additionally use Pillow; font regeneration
also uses fontTools. The generated C/C++ assets are already committed, so neither
is needed just to build the firmware. See the [board notes](firmware/boards/ai-passport/README.md)
for regeneration commands and implementation limits.

## Upstream and licenses

Based on [Deotaland/Agent_link](https://github.com/Deotaland/Agent_link), retaining
its history through `526674bd948946b47a4043dcd53e2f71490a16d4`. The C3 audio repair
incorporates upstream work from `9f664fc028a664a47789506802aecfb0b69b7e18`
(`fix(ai-passport-wifi):audio link fixed`), followed by the local keepalive and
subtitle fixes. This is a personal customization, not an official firmware release.
Pixel.18 also incorporates the pagination success diagnostic from
[upstream PR #4](https://github.com/Deotaland/Agent_link/pull/4) at
`cbb4ea6c847d516a7562590d3abe65517827d74b` (its cursor fixes were already present),
and adapts the upstream UTF-8 menu work while retaining the Tianyi renderer/font.
Pixel.19 selectively adapts onboarding, Wi-Fi settings and localization from
[deota-jonah/ai-passport](https://github.com/deota-jonah/ai-passport) at
`eaee36b687dcf4d20f752d975140e99205f80b0d`, with offline-menu access, namespace-only
reset and portal interpolation fixes. The existing voice engine, caption pacing,
Tianyi assets, board drivers and partition layout are retained. That integration
excluded battery support. Pixel.20 adds a CW2017 reader using the
[FoloToy battery driver's register map and wake sequence](https://github.com/FoloToy/ai-passport/blob/main/components/bsp/src/bsp_battery.c),
retaining the installed cell profile instead of importing a different calibration.

The full upstream source remains available for building:
[firmware guide](firmware/README.md), [SDK](firmware/components/agent_link/README.md),
[board catalog](firmware/boards/README.md) and [assistant workflows](skills/README.md).
The custom version has been validated on the AI Passport WiFi profile only.

Source code retains the upstream [MIT license](LICENSE). Vendored code and fonts
retain their own notices, including the [SIL Open Font License](firmware/boards/common/fonts/OFL.txt).
Character artwork is Tianyi-inspired fan art; the code license does not grant
rights to third-party characters or trademarks. Generation prompts and source
atlases are in `firmware/apps/ai-passport-wifi/avatar/`.
