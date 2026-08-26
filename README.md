# moonlight-ps3

A Moonlight (NVIDIA GameStream) client for the PlayStation 3.

Targets the open-source **PSL1GHT** homebrew SDK (no official PS3 SDK). Video/audio
are **software-decoded** (FFmpeg) and rendered via the RSX (PSGL), since the PS3 has
no accessible hardware H.264/AAC decoder in homebrew. Baseline target is **480p30**,
with 720p30 as a stretch goal behind an SPU-accelerated decode path.

See [`plan.md`](plan.md) for the full architecture and milestone breakdown, and
[`BUILD.md`](BUILD.md) for toolchain + dependency setup.

## Status

Scaffold (M0/M1): build system, common layer (crypto/CSPRNG, persisted host store),
and the module interface headers that define the architecture. Networking, pairing,
session, decode, render, input, and UI are stubbed as interfaces to be implemented
in later milestones.

## Layout

```
src/
  common/   crypto, host store, logging
  net/      http, mdns, rtsp, udp_stream   (interfaces)
  proto/    pairing, connection, input     (interfaces)
  av/       video_decoder, audio_decoder   (interfaces)
  render/   rsx_renderer + yuv2rgb shader  (interface)
  input/    pad                            (interface)
  ui/       menu state machine             (interface)
  main.c    PSL1GHT entry point
```
