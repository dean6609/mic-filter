#!/bin/sh
# Builds the Linux/macOS terminal app and the portable engine tests into dist/.
#   ./build.sh            build
#   ./build.sh --test     build and run the automated checks
#   ./build.sh --package  also create the archive that users download
# Needs only a C/C++ compiler (cc/c++, or set CC/CXX). Windows uses build.ps1 instead.
set -eu
root=$(cd "$(dirname "$0")" && pwd)
out="$root/dist"; obj="$root/build/posix"
mkdir -p "$out" "$obj"
CC=${CC:-cc}; CXX=${CXX:-c++}
system=$(uname -s); machine=$(uname -m)
rn="$root/vendor/rnnoise"; rs="$root/vendor/speex-resampler"; ma="$root/vendor/miniaudio"
base="-O2 -DNDEBUG -I$rn/include -I$root/src -I$rs -I$ma"
cflags="$base -DDISABLE_DEBUG_FLOAT -I$rn/src"
sources="celt_lpc denoise kiss_fft nnet nnet_default parse_lpcnet_weights pitch rnn rnnoise_tables rnnoise_data"
x86=""
case "$machine" in
  x86_64|amd64) cflags="$cflags -DRNN_ENABLE_X86_RTCD -DCPU_INFO_BY_ASM"; x86="x86cpu x86_dnn_map nnet_avx2 nnet_sse4_1" ;;
  # The vendored RNNoise subset has no NEON kernels; its portable C path is used on ARM.
  *) cflags="$cflags -DDISABLE_NEON" ;;
esac
objects=""; pids=""
compile() { # compile <object> <compiler and arguments...>; runs in parallel
  object=$1; shift; objects="$objects $object"
  "$@" -o "$object" & pids="$pids $!"
}
for name in $sources; do compile "$obj/$name.o" $CC $cflags -w -c "$rn/src/$name.c"; done
for name in $x86; do
  extra=""
  [ "$name" = nnet_avx2 ] && extra="-mavx2 -mfma"
  [ "$name" = nnet_sse4_1 ] && extra="-msse4.1"
  compile "$obj/$name.o" $CC $cflags $extra -w -c "$rn/src/x86/$name.c"
done
compile "$obj/resample.o" $CC $base -DOUTSIDE_SPEEX -DRANDOM_PREFIX=micfilter -DFLOATING_POINT -w -c "$rs/resample.c"
compile "$obj/audio_backend.o" $CC $base -w -c "$root/src/audio_backend.c"
for pid in $pids; do wait "$pid"; done

cxxflags="$base -std=c++20 -Wall -Wextra -Wno-misleading-indentation -DOUTSIDE_SPEEX -DRANDOM_PREFIX=micfilter -DFLOATING_POINT"
core="$root/src/dsp.cpp $root/src/voice.cpp $root/src/rate_processor.cpp"
engine=$(echo "$objects" | sed "s# $obj/audio_backend.o##")
if [ "$system" = Darwin ]; then
  libs="-framework CoreFoundation -framework CoreAudio -framework AudioToolbox -lpthread -lm"
else
  libs="-static-libstdc++ -static-libgcc -lpthread -lm -ldl"
fi
$CXX $cxxflags "$root/src/cli.cpp" $core $objects $libs -o "$out/micfilter"
$CXX $cxxflags -I"$root/tests" "$root/tests/portable_main.cpp" "$root/tests/dsp_tests.cpp" $core $engine -lpthread -lm -o "$out/micfilter-tests"
echo "Build ready: $out/micfilter"

for argument in "$@"; do
  case "$argument" in
    --test) "$out/micfilter-tests"; "$out/micfilter" --self-test; "$out/micfilter" --version ;;
    --package)
      stage="$obj/MicFilter"; rm -rf "$stage"; mkdir -p "$stage"
      cp "$out/micfilter" "$root/LICENSE" "$stage/"
      cp "$rn/COPYING" "$stage/RNNOISE-LICENSE.txt"; cp "$rs/COPYING" "$stage/SPEEX-LICENSE.txt"
      sed -n '/^This software is available as a choice of the following licenses/,$p' "$ma/miniaudio.h" > "$stage/MINIAUDIO-LICENSE.txt"
      if [ "$system" = Darwin ]; then
        cp "$root/packaging/MicFilter.command" "$root/packaging/README-macOS.txt" "$stage/"; chmod +x "$stage/MicFilter.command" "$stage/micfilter"
        (cd "$obj" && rm -f "$out/MicFilter-macOS-$machine.zip" && zip -qr "$out/MicFilter-macOS-$machine.zip" MicFilter)
        echo "Package ready: $out/MicFilter-macOS-$machine.zip"
      else
        cp "$root/packaging/README-Linux.txt" "$stage/"; chmod +x "$stage/micfilter"
        tar -C "$obj" -czf "$out/MicFilter-Linux-$machine.tar.gz" MicFilter
        echo "Package ready: $out/MicFilter-Linux-$machine.tar.gz"
      fi ;;
  esac
done
