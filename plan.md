# Plan: Moonlight (GameStream) Client for PS3

## 1. Goal & Context
Build a **full** Moonlight client for the PlayStation 3 that streams games from a host
running NVIDIA GameStream (GeForce Experience / Sunshine).

- **Toolchain reality:** The official PS3 SDK is not obtainable (Sony no longer licenses it,
  and `cellVdec`/`cellAdec` hardware decoders are proprietary to that SDK). This project targets
  **PSL1GHT + ps3toolchain** (open-source homebrew) only.
- **AV decode:** No hardware H.264/AAC. Decode must be **software** (FFmpeg/libavcodec,
  ideally SPU-accelerated) and rendered via PSGL shaders.
- **Baseline target:** **480p30** (achievable with software decode). **720p30 is a stretch goal**
  contingent on an SPU-optimized H.264 decoder proving fast enough.
- **Testing target:** RPCS3 emulator for everything except final hardware validation.

## 2. Reference Material (port, don't reinvent)
- **moonlight-common-c** — protocol, crypto, pairing, RTSP, input serialization. Port its
  `src/` (Limelight) modules rather than reimplementing from memory.
- **moonlight-embedded / moonlight-qt** — RTSP/SDP handling and session lifecycle reference.
- **PSL1GHT + ps3toolchain** — PPU/SPU GCC, libnet (BSD sockets), cellPad, PSGL (RSX),
  libxml2, zlib.
- **mbedTLS** — TLS + RSA/DH/AES for pairing/HTTPS (portable to PPU).
- **FFmpeg (libavcodec/libavutil)** — software H.264 video + AAC audio decode.
- **RPCS3** — primary dev/test emulator (pad, RSX via Vulkan); real HW for final validation.

## 3. Architecture
Decoder/render/audio are behind interfaces so an SPU-optimized path can replace the generic
FFmpeg one later without touching the protocol layer.

```
src/
  common/   crypto (mbedTLS wrappers), xml, pairing state, persisted host store
  net/      http_client, mdns (or manual IP), rtsp_client, udp_stream (video/audio/control)
  proto/    pairing.c, connection.c, input_packets.c
  av/       video_decoder.h  -> video_ffmpeg.c (generic) | video_spu.c (stretch)
            audio_decoder.h  -> audio_ffmpeg.c (AAC) | audio_pcm.c (passthrough)
  render/   rsx_renderer.c, yuv2rgb.frag (PSGL fragment shader)
  input/    pad.c (cellPad -> Moonlight gamepad/keyboard/mouse)
  ui/       menus (host list, pair, app select, settings, in-stream overlay)
  main.c    thread bootstrap + top-level state machine
```
**Threads (PPU):** network/rx, decoder, audio, render/present, UI/input. SPUs offload
colorspace conversion and (stretch) IDCT/deblock in the decoder.

## 4. GameStream Protocol Summary (for implementation)
- **Discovery:** mDNS `_nvstream._tcp.local` **plus** manual IP entry (mDNS optional, manual guaranteed).
- **Pairing (port 47989 HTTPS):** generate x509 client cert, RSA + DH shared secret, PIN phrase,
  derive persistent 16-byte `key` + `iv`. Persist host record (key, iv, uuid, mac, cert).
- **Server info / app list (HTTPS):** `GET /serverinfo`, `GET /applist`.
- **Launch:** `POST /launch?appid=...&...` → returns session URL.
- **Session (RTSP):** `DESCRIBE/SETUP/PLAY` to host:48010 → SDP with UDP ports
  video 47998, audio 47997, control 47999. Whole frames arrive in UDP datagrams
  (H.264 Annex-B / AAC-ADTS), control channel encrypted with session `key`+`iv`.
- **Input:** controller/keyboard/mouse events serialized and sent on the control channel.
- **Keepalive:** periodic RI/RA reports + QoS; request IDR on packet loss.

## 5. Milestones

**M0 — Toolchain & target**
- Install `ps3toolchain` + PSL1GHT; build a hello-world `.self` that runs in RPCS3.
- Set up the build for mbedTLS, libxml2, and FFmpeg (PPU cross-build).

**M1 — Skeleton & crypto**
- Makefile (ppu-gcc) wiring mbedTLS + libxml2 + moonlight-common-c `common/`+`crypto/`.
- Implement persisted **host store** (`/dev_hdd0` app data dir).

**M2 — Discovery & HTTP**
- `http_client` over mbedTLS (mutual TLS using the pairing client cert).
- mDNS client **and** manual IP add.
- `serverinfo` + `applist` requests.

**M3 — Pairing**
- Port `pair.c`: x509 client cert, RSA, DH shared secret, PIN, derive/persist `key`+`iv`.

**M4 — Session & transport**
- `rtsp_client`: DESCRIBE/SETUP/PLAY → SDP.
- `udp_stream`: frame reassembly + control-channel AES decryption; RI/RA keepalive + QoS.

**M5 — Audio**
- `audio_decoder` (FFmpeg AAC → PCM), playback via PS3 audio out. PCM passthrough path if host sends raw.

**M6 — Video decode + render (hardest)**
- `video_decoder` (FFmpeg H.264 → YUV420).
- Upload YUV as RSX textures; `yuv2rgb.frag` PSGL shader converts to RGB; present vsync'd.
- Jitter buffer for A/V sync.
- **Stretch:** SPU-accelerated decode (`video_spu.c`) to chase 720p30.

**M7 — Input**
- `cellPad` (up to 7 pads) → Moonlight gamepad/keyboard/mouse packets on control channel.
  USB mouse/keyboard optional stretch.

**M8 — UI**
- Host list (discover/manual), pair flow, app list, launch/resume, settings
  (resolution/fps/bitrate), in-stream overlay + quit.

**M9 — Integration & validation**
- RPCS3 for all non-HW-AV logic; real PS3 (CFW/HEN) for final 480p30 validation. Tune buffers/thread priority.

**M10 — Stretch**
- 720p30 (SPU decode), HEVC, rumble feedback, keyboard/mouse, app icons.

## 6. Risks & Mitigations
| Risk | Impact | Mitigation |
|---|---|---|
| No hardware AV decode | 720p30 infeasible | Baseline 480p30; SPU-optimized decode as stretch |
| Software H.264 perf on Cell | Stutter / dropped frames | SPU offload; resolution/bitrate scaling; jitter buffer |
| RSX texture upload bandwidth | Present stalls | Benchmark early; 480p safe, 720p stretch |
| mbedTLS DH/TLS on PPU | Slow pairing/HTTPS | One-time cost; non-blocking |
| UDP loss/jitter | Artifacts | Jitter buffer + IDR-on-loss |
| RPCS3 lacks real decode path | Can't validate decode in emu | Validate decode on real HW only |

## 7. Success Criteria
- Pair with a Sunshine/GFE host, browse apps, launch, and stream **480p30** with gamepad input
  and audio on a real PS3. Deliver the decoder abstraction so an SPU path can later raise the
  ceiling toward 720p30. 720p30 achieved = full stretch success.

## 8. Status (as of this build)
- **Toolchain**: ps3toolchain (PPU/SPU GCC 7.2.0) + PSL1GHT installed. libxml2 2.12.9 and
  mbedTLS (portlibs 2.28.10, used by build) cross-built. **FFmpeg RESOLVED**: built 3.4.13
  minimal (H.264+AAC) with the stock ppu-gcc 7.2 by shrinking `allcodecs.c` (avoids the
  compiler ICE). `HAVE_FFMPEG` is active; the project links `-lavcodec -lavutil -lm`.
  Reproducible via `tools/build_ffmpeg.sh`.
- **M0 Skeleton/crypto**: done (Makefile, host store, mbedTLS wrapper, native `tests/` harness).
- **M1**: n/a (folded into M0/M2).
- **M2 Discovery & HTTP**: done (`sock.h/net.c`, `http.c` mbedTLS client, `mdns.c`).
- **M3 Pairing**: done (`pairing.c` two-phase `/pair`, RSA-2048 keypair + self-signed cert,
  DH-derived `key`+`iv`; `hoststore` persists `key`/`iv`/`cert_pem`/`key_pem`).
 - **M4 Session & transport**: done (`rtsp.c` DESCRIBE/SETUP/PLAY/TEARDOWN + SDP parse incl.
   resolution `a=framesize`, `udp_stream.c` frame recv + control AES-128-CBC, `connection.c`
   session start/stop). UDP reassembly now goes through `reassembly.c` (per-channel buffers +
   frame callback); the real GameStream fragment header still needs decoding (TODO).
 - **M5 Audio**: done — `audio_decoder` (FFmpeg AAC→s16, compiled/linked under `HAVE_FFMPEG`)
   plus PS3 audio output (`audio_playback_ps3.c`, `libaudio`). Sample-rate
   conversion is now implemented (linear resample to 48000 in `stage()`).
 - **M6 Video decode + render**: done — `video_decoder` (FFmpeg H.264→YUV420, compiled/linked
   under `HAVE_FFMPEG`), `jitter` buffer (now presented with a 2-frame latency to absorb jitter;
   true A/V sync via PTS vs. audio clock is a TODO), and `pipeline` tying decode→jitter→RSX render
   / audio. RSX render path is a CPU YUV→RGBA blit; GPU texturing + `yuv2rgb.frag` is a TODO.
 - **M7 Input**: done (`pad.c` `cellPad`→Moonlight button/stick mapping; `input_packets.c`
   gamepad/keyboard/mouse NV packets, AES control send). Wire details flagged TODO pending
   `moonlight-common-c` verification.
 - **M8 UI**: scaffolded (`ui.c` menu state machine — host discover/list (ST_HOSTS), pair
   (ST_PAIR), app list (ST_APPS, via `rtsp_applist` + `rtsp_launch`), stream (ST_STREAM); pad
   navigation, solid-color RSX render placeholder; real on-screen text via PSL1GHT font lib is a
   TODO). The app-list/launch HTTPS endpoints (port 47989) may need verification against
   `moonlight-common-c`.

### Remaining work before real streaming (buildable, needs verification on HW/reference)
 - **#1 UDP reassembly** — wire-format fragment header (frame index / fragment offset / last
   flag) must be filled in from `moonlight-common-c` (currently each datagram = one frame).
 - **#2 SDP resolution** — done (parses `a=framesize`); verify the exact SDP attribute name
   used by Sunshine/moonlight-common-c.
 - **#3 A/V sync** — latency buffer in place; replace with PTS-vs-audio-clock presentation.
 - **#4 RSX GPU render** — implement texture upload + `yuv2rgb.frag` shader + flip (currently a
   CPU blit that is never actually presented). Hardware-only verification.
 - **#5 On-screen text** — render menu text via PSL1GHT `font/` lib (needs a font asset).
 - **#6 Audio resample** — done (linear); swap for higher-quality resampler if needed.
 - **#7 App list/launch** — done structurally; verify HTTPS endpoint/port + XML schema.
 - **#8 Threading** — single-threaded menu/stream loop; add network/decode/render concurrency.
 - **#9 Entropy** — improved (seeded from `clock()`); still a placeholder PRNG, not
   cryptographically secure. Replace with a real PS3 entropy source before real use.
 - **#10 Cert verification** — `MBEDTLS_SSL_VERIFY_NONE` placeholder; implement GameStream cert
   pinning (verify server cert against the pairing-stored client cert).
 - **Wire formats** — exact control AES mode/padding, input packet framing, and RTSP Transport
   string need verification against `moonlight-common-c`.
