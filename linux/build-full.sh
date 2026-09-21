#!/bin/bash
# Build PalSchema as a UE4SS C++ mod for the Linux port (companion repo ue4ss-linux-palworld, branch linux-palschema,
# built with GUI and input disabled). Usage: UE4SS_SRC=/path/to/ue4ss-linux-palworld linux/build-full.sh
# Output: build-full/main.so (install as Mods/PalSchema/libs/main.so). Objects are mtime-incremental; rm build-full/obj/*.o
# after a header or defs change. Third-party headers (nlohmann json, fmt) are expected under $UE4SS_SRC's deps or ./third.
set -u
L=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$L/.." && pwd); SRC=${UE4SS_SRC:?set UE4SS_SRC to the ue4ss-linux-palworld checkout}; B=$ROOT/build-full
# inc.txt holds include roots written with $SRC (the UE4SS checkout, built once so build_linux/_deps exists) and $L.
INC="$(sed "s#\$SRC#$SRC#g; s#\$L#$L#g" $L/inc.txt) -I$L/shim -I$ROOT/include -I$SRC/build_linux/_deps/funchook-src/include"
DEFS="$(cat $L/defs.txt) -DPALSCHEMA_API=__attribute__((visibility(\"default\")))"
EXCLUDE="${EXCLUDE:-src/SDK/PalSignatures.cpp}"
FILES="$L/shim/PalSignatures.cpp $L/shim/LinuxMirror.cpp $L/shim/OperatorNewForward.cpp $L/shim/ProgramBind.cpp"
for f in $(cd $ROOT && find src -name '*.cpp' | sort); do
  skip=0; for x in $EXCLUDE; do [ "$f" = "$x" ] && skip=1; done
  [ $skip -eq 0 ] && FILES="$FILES $ROOT/$f"
done
mkdir -p $B/obj
cd $SRC
rc=0; n=0
for f in $FILES; do
  o=$B/obj/$(echo $f | sed "s#$ROOT/##; s#/#_#g; s#\.cpp\$#.o#")
  [ -f $o ] && [ $o -nt $f ] && continue
  n=$((n+1))
  rm -f $o; g++-13 -std=c++23 -fPIC -O2 -g -fvisibility=hidden -fvisibility-inlines-hidden $DEFS $INC -c $f -o $o > $o.log 2>&1
  [ -f $o ] || { echo "FAILED: $(echo $f | sed "s#$ROOT/##")"; grep -m2 'error:' $o.log | sed "s#$ROOT/##" | cut -c1-160; rc=1; }
done
echo "compiled $n files"
[ $rc -ne 0 ] && exit 1
g++-13 -shared -fPIC $B/obj/*.o $SRC/build_linux/Game__Dev__Linux64/lib/libfunchook.a $SRC/build_linux/Game__Dev__Linux64/lib/libdistorm.a -static-libstdc++ -static-libgcc -Wl,-Bsymbolic -Wl,--exclude-libs,ALL -o $B/main.so -Wl,--version-script=$L/exports.map -ldl 2>&1 | grep -E 'error|undefined' | head -30
[ -f $B/main.so ] || { echo "link failed"; exit 1; }
echo "built: $(stat -c %s $B/main.so) bytes"
nm -D --undefined-only $B/main.so | awk '{print $2}' | grep -E '^_ZN(K)?2RC|^_ZN3fmt|^_ZN8Palworld|^_ZN8UECustom|^_ZN2PS|funchook|palhook' | sort -u > $B/need.txt
nm -D --defined-only $SRC/build_linux/Game__Dev__Linux64/lib/libUE4SS.so | awk '{print $3}' | sort -u > $B/have.txt
echo "--- unresolved against libUE4SS:"; comm -23 $B/need.txt $B/have.txt | c++filt | head -20
