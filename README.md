# AI Passport — Tianyi Pixel

Custom firmware for the **FoloToy AI Passport (ESP32-C3, 8 MB flash, no PSRAM)**,
with a Tianyi-inspired pixel companion, continuous voice conversations and Chinese
subtitles. The current device-tested version is **2.2.1-tianyi.pixel.16**.

[Chinese README](README.zh-CN.md) · [Board and controls](firmware/boards/ai-passport/README.md) ·
[Realtime implementation](firmware/components/agent_link/REALTIME.md)

<img src="firmware/apps/ai-passport-wifi/avatar/tianyi-pixel-expressions.png" width="320" alt="Four Tianyi-inspired pixel expressions: idle, blink and two speaking poses">

## What is included

- Four 64x64 pixel expressions, blinking, listening feedback and mouth movement
  driven by speaker audio. Rendering reuses a 240x4 DMA stripe without a framebuffer.
- The upstream C3 Opus audio path, shared codec workspace and incremental TLS I/O.
- A periodic WebSocket Pong that keeps long spoken replies alive with the gateway's
  Pong-based heartbeat, including when incoming control frames wait behind audio.
- Immediate user transcripts and assistant subtitles paced by released PCM samples,
  with queued audio and the I2S tail accounted for.
- Chinese font assets, avatar/font generation tools and host regression tests.

Subtitle pacing is approximate: the current protocol supplies no word timestamps.
The bounded text window holds 1,024 UTF-16 units and reclaims displayed prefixes
on cumulative updates; an unusually large single advance transcript can overflow it.
Mouth animation follows audio amplitude, not phonemes.

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
- Long-press OK for the menu, then choose **New conversation** to start a fresh
  conversation. Existing conversations remain available.
- Set the Agent's name and identity in its console persona. After changing persona,
  create a new conversation: existing sessions may retain their previous instructions.
  The device's new conversation was confirmed to use the chosen name, **Tiantian**.

The character artwork does not change the voice preset. A Luo Tianyi-like voice
has **not** been implemented: the actual voice is selected by the service provider,
and the current console exposes no per-Agent voice selector. A speaking-style prompt
alone does not select another preset. No Agent Stack server code is included here.

## Validation

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

The full upstream source remains available for building:
[firmware guide](firmware/README.md), [SDK](firmware/components/agent_link/README.md),
[board catalog](firmware/boards/README.md) and [assistant workflows](skills/README.md).
The custom version has been validated on the AI Passport WiFi profile only.

Source code retains the upstream [MIT license](LICENSE). Vendored code and fonts
retain their own notices, including the [SIL Open Font License](firmware/boards/common/fonts/OFL.txt).
Character artwork is Tianyi-inspired fan art; the code license does not grant
rights to third-party characters or trademarks. Generation prompts and source
atlases are in `firmware/apps/ai-passport-wifi/avatar/`.
