# Building Moonlight PS3

This project targets the **PSL1GHT** open-source homebrew SDK (no official PS3 SDK).
It cannot be built or run on a normal PC; you need the PPU/SPU cross-toolchain and,
for final validation, a PS3 running CFW/HEN (or RPCS3 for everything except hardware
AV decode — which this project does not use anyway, since decode is software).

## 1. Install the toolchain

```sh
# ps3toolchain builds a full PPU/SPU GCC cross-compiler (can take 20-40 min).
git clone https://github.com/ps3dev/ps3toolchain
cd ps3toolchain
./toolchain.sh

# PSL1GHT headers + libs (libsysutil, libgcm, libnet, libpad, PSGL, ...).
git clone https://github.com/ps3dev/PSL1GHT
cd PSL1GHT && make && make install
```

This sets up `/usr/local/ps3dev` with `bin/ppu-gcc` and `psl1ght/`. Export:

```sh
export PS3DEV=/usr/local/ps3dev
export PSL1GHT=$PS3DEV/psl1ght
```

## 2. Cross-build dependencies into `deps/`

All three must be built with `ppu-gcc` and installed under `deps/<lib>/{include,lib}`.

### mbedTLS (TLS + RSA/DH/AES for pairing/HTTPS)
> **The project compiles against the mbedTLS that `ps3libraries` already
> installs into `$(PORTLIBS)` (currently **2.28.x LTS**).** Because
> `$(PORTLIBS)/include` and `$(PORTLIBS)/lib` come **first** in the Makefile's
> `-I`/`-L` order, that copy shadows `deps/mbedtls` entirely — so the 3.6.2 we
> cross-built into `deps/mbedtls` is currently unused. Don't depend on
> `deps/mbedtls`; if you rebuild mbedTLS, install it into `$(PORTLIBS)` instead.

The bare PS3 has no OS entropy source or `mbedtls_ms_time`, so the build uses a
custom config (`ml_mbedtls_config.h`, on the include path via
`-DMBEDTLS_CONFIG_FILE`) that disables the Unix-only `timing`/`net` modules and
platform entropy. `src/common/crypto.c` supplies `mbedtls_ms_time` and a
**placeholder** entropy source (xorshift PRNG) that MUST be replaced with a real
RNG before production.

**2.28.x API quirks** (trip up porting code written against 3.x):
- `mbedtls_pk_parse_key()` takes **5 args** (`ctx, key, keylen, pwd, pwdlen`).
- `mbedtls_pk_rsa()` takes the `mbedtls_pk_context` **by value**, not by pointer.

```sh
cat > ml_mbedtls_config.h <<'EOF'
#ifndef ML_MBEDTLS_CONFIG_H
#define ML_MBEDTLS_CONFIG_H
#include "mbedtls/mbedtls_config.h"
#undef MBEDTLS_TIMING_C   /* Unix/Windows only */
#undef MBEDTLS_NET_C       /* we use our own socket layer */
#endif
EOF
# (Only needed if you rebuild mbedTLS into $(PORTLIBS); the supplied copy works.)
make lib CC=$PS3DEV/ppu/bin/ppu-gcc AR=$PS3DEV/ppu/bin/ppu-ar \
  LD=$PS3DEV/ppu/bin/ppu-gcc RANLIB=$PS3DEV/ppu/bin/ppu-ranlib \
  CFLAGS="-DMBEDTLS_CONFIG_FILE='\"ml_mbedtls_config.h\"' \
          -DMBEDTLS_NO_PLATFORM_ENTROPY -DMBEDTLS_PLATFORM_MS_TIME_ALT -O2"
```

### libxml2 (XML for pairing/serverinfo payloads)
```sh
curl -sL -o libxml2.tar.gz https://github.com/GNOME/libxml2/archive/refs/tags/v2.12.9.tar.gz
tar xzf libxml2.tar.gz && cd libxml2-2.12.9
./configure --host=powerpc64-unknown-elf --prefix=$PWD/../deps/libxml2 \
  CC=$PS3DEV/ppu/bin/ppu-gcc AR=$PS3DEV/ppu/bin/ppu-ar RANLIB=$PS3DEV/ppu/bin/ppu-ranlib \
  --without-python --without-zlib --without-lzma --without-iconv --without-http --disable-shared
make && make install
```

### FFmpeg (software H.264 video + AAC audio decode)
This is the heaviest dependency. **ppu-gcc 7.2 internal-compiler-errors on the
full `libavcodec/allcodecs.c`** (the codec-registration file) at *every*
optimization level — a compiler backend bug, not an FFmpeg-version/flag issue
(verified across 3.4.13 and 6.1.1, `-O0`/`-O1`/`-O2`). We avoid it **without a
new toolchain** by configuring a *minimal* build (only the H.264 + AAC
decoders/parsers we need) and shrinking `allcodecs.c` to register just H264/AAC.
The reproducible build is `tools/build_ffmpeg.sh`:

```sh
export PS3DEV=/usr/local/ps3dev PSL1GHT=/usr/local/ps3dev/psl1ght
bash tools/build_ffmpeg.sh    # downloads ffmpeg-3.4.13, configures minimal,
                              # shrinks allcodecs.c, builds + installs to deps/ffmpeg
```

It builds only `libavcodec` + `libavutil` (we feed packets from memory, so no
demuxers/muxers/swresample/swscale; AAC float→s16 conversion is done by hand in
`src/av/audio_ffmpeg.c`). The build is incremental, so a killed run resumes.

The Makefile auto-detects `deps/ffmpeg/lib/libavcodec.a`: when present it adds
`-DHAVE_FFMPEG` and links `-lavcodec -lavutil -lm`. The av modules in `src/av/`
compile to **no-op stubs** when FFmpeg is absent (so the project always builds).
Note: because `libavutil` references `libm` (llrint/sin/...), `-lm` is placed
*after* the FFmpeg libs in the link line.

## 3. Build

```sh
make            # produces moonlight-ps3.self
make clean
```

`make_fself` (from PSL1GHT) turns the ELF into a `.self` homebrew executable.

## 4. Test

- **RPCS3**: load `moonlight-ps3.self` as a homebrew app. Use for UI, networking,
  pairing, and session logic. Note RPCS3 may not exercise FFmpeg the same way as HW,
  but the decode path will still run.
- **Real PS3**: install the `.self` via a package manager on CFW/HEN for final
  480p30 streaming validation.

## 5. Gotchas (learned the hard way)

- **PPU GCC lives in `$PS3DEV/ppu/bin/`**, not `$PS3DEV/bin/`. The driver
  (`ppu-gcc`) **requires `PSL1GHT` to be set in its environment** or it errors
  with `environment variable 'PSL1GHT' not defined`. Export both before any
  cross-build or `make`.
- **PSL1GHT ships individual libs** (`liblv2`, `libsysutil`, `libnet`, `librsx`,
  `libgcm_sys`, `libssl`, `libvdec`, `libaudio`, …). There is **no `libpsl1ght.a`**
  and no `libpad.a` (pad is in `libsysutil`). The Makefile links the specific
  libs; do not add `-lpsl1ght`/`-lpad`.
- **`ps3toolchain` `009-ps3libraries` fails** fetching `libpng` from SourceForge.
  This only affects optional graphics libs we don't need; zlib installs fine and
  the core toolchain (compilers, PSL1GHT, zlib) is fully usable.
- **`sys_net_initialize_network` is not present** in these libs. PSL1GHT's BSD
  socket API (`<net/net.h>`) works as-is; `net_init()` in `src/net/sock.h` is a
  placeholder (bring the interface up via netctl on real HW if needed).
- **Don't name a header `net.h`** next to a file that includes `<net/net.h>` —
  the quoted include collides with the system header and silently resolves to the
  wrong file. This project uses `src/net/sock.h`.
- **mbedTLS entropy is a placeholder.** `src/common/crypto.c` seeds CTR-DRBG from
  an xorshift PRNG, which is NOT cryptographically secure. Replace it with real
  entropy (network timing / a PS3 RNG syscall) before real pairing.
- **The build uses portlibs' mbedTLS (2.28.x), not `deps/mbedtls`.** `$(PORTLIBS)`
  sits first in `-I`/`-L`, so `deps/mbedtls` (a 3.6.2 we built) is shadowed and
  unused. Port to the 2.28 API: `mbedtls_pk_parse_key` is **5-arg** and
  `mbedtls_pk_rsa()` takes the context **by value**. A native unit-test harness
  in `tests/` validates `src/common/crypto.c` against an x86_64 2.28.10 build.
- **FFmpeg ICE is a ppu-gcc 7.2 bug, not an FFmpeg issue.** It fails on the full
  `allcodecs.c` at *all* `-O` levels. The fix is to build a **minimal** FFmpeg
  (H.264+AAC only) and shrink `allcodecs.c` so the offending codegen never runs —
  no new compiler required (`tools/build_ffmpeg.sh`). The project still builds
  without FFmpeg via the no-op av stubs.

