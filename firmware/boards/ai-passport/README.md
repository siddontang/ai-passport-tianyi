# AI Passport

ESP32-C3, 8 MB flash, ST7789 240x320, ES8311 full duplex at 16 kHz, PCM16 mono.
The BLE build sends microphone audio to the phone and plays its replies.

The UI draws directly into the panel driver's single 240x4 RGB565 DMA stripe
(1,920 bytes). It allocates no framebuffer or LVGL heap. Colors, layout,
Montserrat 14/20/28px glyph coverage and kerning match the previous UI.
The read-only font assets in `boards/common/raster_montserrat_*.c` retain their
upstream license. Unsupported characters use a placeholder, as with the old fonts.

The UI acknowledges a session token only after its changed regions finish
transferring. A state change interrupts the previous drawing pass; a transfer
failure closes the playback gate and stops reuse of a buffer that may still
belong to DMA.

Voice state follows VoiceReply commands rather than audio availability:

| App action | UI | Playback |
| --- | --- | --- |
| Prelude directly over L2CAP | Thinking | Play without a state change |
| `0x05 status=2` | Speaking | Wait for the Speaking screen, then play |
| Audio pauses without a command | Speaking | Re-prime while keeping the state |
| `0x05 status=3` | Speaking, then Ready | Drain PCM and 90 ms of I2S DMA |

`status=3` outside Speaking is ignored. Start/end commands arriving before the
button task runs are retained together. `status=0` or a failure ends a pending
reply; a new press or disconnect cancels the old generation. An empty formal
reply drains immediately. Speaking has no silence timeout; an explicitly
requested drain has a 30 s limit. Session IDs are logged without validation.

The 32 KiB playback buffer and bounded 24 KiB voice queue remain allocated.
The BLE profile uses 96 blocks in each MSYS pool and checks their free counts
separately, reserving 12 blocks per pool for control traffic. VoiceChunk carries
up to 494 bytes of continuous PCM at ATT MTU 512, with smaller packets at lower
MTUs. A congested packet can wait up to 2 seconds; queue overflow still ends the
session to avoid sending audio with gaps. Per-second flow logs show capture and
submission rates, queue depth and both pool counts. Submission is not a phone
receipt acknowledgement.
The BLE profile omits WiFi provisioning and cloud sources. Its dependencies are
selected from the firmware profile before ESP-IDF expands component requirements.

Memory snapshots are logged after display/codec initialization, BLE startup and
before voice creation. Voice and UI tasks log their remaining stack space.

Build in `firmware/build` with ESP-IDF 5.5.4:

```bash
python tools/build_firmware.py ai-passport-ble
```

## WiFi voice

Build from the repository root in the existing ESP-IDF 5.5.x or 6.1.x environment:

```sh
python firmware/tools/build_firmware.py ai-passport-wifi --jobs 2
```

The product is `FoloToy-AI-Passport`, platform product ID **1**, chip identifier
`esp32-c3`, at [bot.deotaland.cn](https://bot.deotaland.cn). Firmware keeps the
existing `AI-PASSPORT` model string. No account API key belongs in this profile.

After an authorized flash, join the `AIPassport-XXXX` hotspot shown on the screen
and open `http://192.168.4.1`. Configure WiFi, claim the six-digit activation code
in the platform console, and bind an Agent supporting realtime voice.

- Press OK to start/end a continuous conversation. The microphone starts only
  after `session.ready`. There is no hold-to-send interaction in the WiFi profile.
- UP/DOWN change volume by 5%, repeating while held. Volume is saved locally.
- Hold OK to end the call and open the menu. UP/DOWN select an entry, OK confirms;
  hold OK again to return home. Chinese menus include conversation selection,
  creation, previous/next pages and Back; UTF-8 conversation titles are retained.
- The first call uses the latest returned conversation, or creates one if none
  exist. The selection survives restart only for the same bound Agent. The menu
  can select another conversation or create one; it does not delete conversations.
- A voice transport interruption pauses capture while the SDK retries with a
  fresh grant, at most twice per call after 2 and 4 seconds. The portrait shows
  Reconnecting and capture resumes after READY. Interrupted speech must be repeated.
  Loss of WiFi/platform connectivity, terminal server events and exhausted retries
  still end the call; press OK again after connectivity returns.
- Speaker playback and its echo tail suppress microphone feedback. The official
  C3 voice worker sends cached Opus silence during replies and resumes microphone
  encoding after the speaker drains. This is half-duplex feedback prevention.

The pixel.14 build uses the upstream audio implementation from
`9f664fc028a664a47789506802aecfb0b69b7e18` (`fix(ai-passport-wifi):audio link fixed`).
That implementation corresponds to the shared codec workspace, compressed input
and output, and C3 network path found in the official 2.2.1 Build 2 package.
The earlier main-based pixel builds omitted those official fixes.
Pixel.15 adds the C3 unsolicited-Pong keepalive described in the SDK notes. It
addresses the gateway's Pong-based timeout during long paced replies, without
changing the pixel.14 codec, buffer or display configuration.

Pixel.17 distinguishes recoverable server errors from terminal session events.
An explicit `recoverable: true` preserves the call; other errors and
`session.ended` remain terminal. Diagnostics record known error/end-reason values
without server messages or identifiers. The audio, keepalive, caption and avatar
paths are unchanged from pixel.16.

Pixel.18 fixes the app-layer cancellation of transport recovery when READY drops.
The SDK exposes recovery for the current request before notifying the app of an
interruption; the controller keeps the call open until READY returns or recovery
ends. Transient microphone stream errors no longer disable capture intent.
Chinese menus reuse the existing subtitle font and stripe renderer. Pagination
logs contain only row counts, offsets and the more-pages flag. Production-code
host tests cover retry/cancel/resume behavior and menu rendering; this build has
not yet been flashed or accepted on a physical AI Passport.

Pixel.19 adds bilingual setup/activation guidance, activation-code expiry,
persistent Chinese/English settings and a confirmed Wi-Fi reset. Long-press OK
opens settings even during setup or activation. The third entry changes Wi-Fi;
the fourth changes language. Reset stops the link and clears only the `al_wifi`
namespace before restarting; identity, binding, conversation and language are
retained. Storage errors do not trigger a reboot. New provisioning pages default
to the saved device language. Host tests exercise the real controller, storage
functions, portal handlers and stripe renderer. Physical setup/restart acceptance
is still pending. Battery display/calibration and board drivers are unchanged.

Pixel.20 adds the CW2017 percentage and icon to all WiFi screens. The gauge
attaches at `0x63` on the existing ES8311 I2C bus at 100 kHz; no second bus or
task is created. Initialization checks the profile-update flag and wakes a
configured sleeping gauge, preserving its cell profile and alert threshold.
There is no five-second startup wait for SOC: invalid readings remain `--%`
and are sampled again by the control task every five seconds. Each transfer has
a 20 ms timeout. Missing/uncalibrated gauges remain unavailable until restart.
Values of 0 through 100 are valid; values above 100 and failed reads are unknown.
The display uses a warning color at 20% or below. Charging is not inferred.

A separate battery revision limits battery-only refreshes to eight 240x4 header
stripes (15,360 bytes) in portrait, menu and onboarding views. Voice processing,
subtitle timing and avatar assets are unchanged. Run
`python firmware/tests/test_passport_battery.py` for the real gauge driver and
polling tests, and `python firmware/tests/test_passport_menu.py` for bilingual
rendering and incremental-transfer checks. Physical battery acceptance is pending.

Both directions use Opus in 20 ms packets; output is 24 kHz/24 kbps and is decoded
at the speaker's 16 kHz. One resident C3 worker owns network and codec processing,
with a 25 KB reserved codec workspace shared between encoder and decoder. The
profile disables MQTT while retaining HTTP activation, binding and voice grants.
TLS writes progress without blocking the receiver; the full 16 KB receive record
is reserved after the handshake. See the SDK [realtime notes](../../components/agent_link/REALTIME.md)
for the upstream memory, framing, recovery and playback-buffer contract.

The screen renders in a shared 240x4 stripe without LVGL. Pixel.16 keeps user
transcripts immediate and advances assistant text with released PCM samples,
accounting for the software queue and a bounded 90 ms I2S tail. Audio stalls
therefore stop subtitle advancement. Response IDs and interruption reset prevent
old replies from replacing current input. The current protocol has no word
timestamps: speech rate is estimated and refined from plausible incremental
text/audio anchors, so this is approximate synchronization, not word alignment.
The allocation-free scheduler occupies 2,216 bytes and holds 1,024 UTF-16 units;
cumulative updates reclaim already shown prefixes. An unusually large single
advance transcript can exceed that window; the diagnostic reports overflow
and retains its beginning instead of jumping to its end. Glyphs use the bundled OFL Noto Sans SC font,
including all 6,763 GB2312 Hanzi, stored in mapped flash. Layout is calculated once
per text update and subtitle updates share the 8 fps display cap. Menu items use
the same Chinese-capable font; Latin-only headings retain the small Latin fonts.
Experimental pixel.13 flow control, caption timing and audio
allocation changes were archived before importing the official audio path.

The WiFi home screen uses a Tianyi-inspired pixel portrait with blinking,
listening feedback and amplitude-driven mouth patches. Mouth movement follows PCM
successfully written to the speaker and expires after silence or reset; it is not
phoneme-level lip synchronization. Four 64x64 RGB565 sprites occupy 32,768 bytes
of mapped flash, scale 3x with nearest-neighbor sampling, and reuse the existing
1,920-byte DMA stripe without a framebuffer. The body remains fixed. Mouth and
blink transitions transfer 11,520 and 17,280 display bytes respectively, compared
with 107,520 bytes for the previous portrait transition. Animation is capped at
8 fps. Caption/status changes redraw text regions without repainting the portrait.
Periodic logs count actual display stripes and bytes; this UI optimization does
not establish that screen load caused the realtime voice send timeout.
Menus, actionable errors and provisioning keep their text view. Build version
`2.2.1-tianyi.pixel.1` introduced this custom UI. Later pixel builds retain it.
The prior full-color source atlas
remains in `apps/ai-passport-wifi/avatar/` alongside the pixel atlas and its prompt.

The pixel source atlas is `apps/ai-passport-wifi/avatar/tianyi-pixel-expressions.png`.
Pixel.21 replaces the hairstyle with the approved twin crown loops, curved side
locks and bows, retaining the existing 64x64 sprites and face-patch rectangles.
Regenerate the immutable pixel asset with `python firmware/tools/pack_passport_avatar.py` from
the repository root (Pillow required). Run `python firmware/tests/test_passport_avatar.py`
for host checks of silence expiry, audio levels, animation states, incremental
regions, text layout and bounds of the actual renderer. Add `--preview` to export
an illustrative idle/listening/speaking sequence under the ignored `.cache/` folder.
Regenerate the subtitle font with `python firmware/tools/pack_passport_font.py`
(Pillow and fontTools required), or use `--check` to verify its generated source.
The font license remains in `firmware/boards/common/fonts/OFL.txt`.

The WiFi application contains no BLE exchange, NFC web integration or contact
management. CW2017 battery display is local; platform battery/charging reporting
is not advertised by this version.

## Acceptance

Host checks cover button gestures, PCM packet assembly/conversion, playback
gating and device-authenticated platform APIs, including the C3 PCM16 path.
A build is not hardware acceptance. Verify hotspot setup, activation, binding,
both audio directions, volume, call stop, network recovery, conversation paging
and repeated calls on the physical board. For pixel.20, also check battery
readings while unplugged and on USB power, plus uninterrupted speech during
indicator updates. Keep the matching ELF for diagnosis.
