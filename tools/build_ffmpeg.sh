#!/bin/bash
# Cross-build FFmpeg 3.4.13 for the PS3 (powerpc64-ps3-elf, PSL1GHT) with the
# STOCK ppu-gcc 7.2 toolchain.
#
# The ppu-gcc 7.2 backend ICEs on the full libavcodec/allcodecs.c (the codec
# registration file). We avoid it by configuring a *minimal* build (only the
# H.264 and AAC decoders/parsers we need) and then shrinking allcodecs.c to
# register ONLY H264/AAC. This sidesteps the compiler bug with no new toolchain.
#
# Incremental: object files persist, so a killed run resumes on re-invocation.
set -e

PS3DEV=${PS3DEV:-/usr/local/ps3dev}
PSL1GHT=${PSL1GHT:-$PS3DEV/psl1ght}
export PATH=$PS3DEV/ppu/bin:$PSL1GHT/bin:$PATH

CACHE=/workspaces/moonlight-ps3/.cache
DEST=/workspaces/moonlight-ps3/deps/ffmpeg
mkdir -p "$CACHE" "$DEST"
cd "$CACHE"

if [ ! -d ffmpeg-3.4.13 ]; then
    echo "[ff] downloading"
    curl -sL -o ffmpeg.tar.bz2 https://ffmpeg.org/releases/ffmpeg-3.4.13.tar.bz2
    tar xjf ffmpeg.tar.bz2
fi
cd ffmpeg-3.4.13

if [ ! -f config.status ]; then
    echo "[ff] configuring (minimal h264+aac)"
    ./configure --prefix="$DEST" \
        --cross-prefix=powerpc64-ps3-elf- --cc=powerpc64-ps3-elf-gcc \
        --arch=ppc64 --target-os=linux --enable-cross-compile \
        --disable-programs --disable-doc --disable-everything \
        --enable-decoder=h264 --enable-parser=h264 \
        --enable-decoder=aac --enable-parser=aac \
        --disable-pthreads --disable-network --disable-zlib --disable-bzlib \
        --disable-asm --disable-avdevice --disable-avfilter --disable-swscale \
        --disable-swresample --disable-postproc --disable-avformat
fi

# Shrink allcodecs.c: keep the REGISTER_* macro definitions and only the
# H264/AAC registration calls. This is what avoids the ppu-gcc 7.2 ICE.
tar xjf ../ffmpeg.tar.bz2 -O ffmpeg-3.4.13/libavcodec/allcodecs.c > libavcodec/allcodecs.c
sed -i '/^#define REGISTER_/b; /REGISTER_/!b; /H264/!{/AAC/!d}' libavcodec/allcodecs.c

echo "[ff] building"
make -j$(nproc)
make install-libs
make install-headers
echo "[ff] DONE -> $DEST"
