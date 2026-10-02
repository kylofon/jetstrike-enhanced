#!/bin/sh
# Regenerate the flat images, start lists, merged symbols, Ghidra projects and decompiles.
#   sh tools/regen.sh [JS|INTRO|CONFIG ...]     (default: all three)
set -e
cd "$(dirname "$0")/.."
export JAVA_HOME="$(cygpath -w "$PWD/_tools/jdk-21.0.12.1+1" 2>/dev/null || echo "$PWD/_tools/jdk-21.0.12.1+1")"
H="$PWD/_tools/ghidra_12.1.3_PUBLIC/support/analyzeHeadless.bat"
mkdir -p work _ghidra port/decomp
for n in ${*:-JS INTRO CONFIG}; do
  case $n in JS) exe=Game/JS_CDROM.EXE;; INTRO) exe=Game/INTRO/INTRO.EXE;; CONFIG) exe=Game/CONFIG.EXE;; esac
  l=$(echo $n | tr A-Z a-z)
  python tools/lefile.py $exe work/$n
  python tools/leindex.py work/$n
  python tools/merge_symbols.py $l
  entry=$(python -c "import json;print('%x'%json.load(open('work/$n.json'))['entry'])")
  "$H" _ghidra $n -import work/$n.bin -overwrite -loader BinaryLoader -loader-baseAddr 0x10000 \
      -processor x86:LE:32:default -cspec gcc -scriptPath tools/ghidra \
      -preScript SetupLE.java $entry \
      -postScript ApplySymbols.java work/${l}_symbols_ghidra.txt \
      -postScript DecompileAll32.java work/${n}_starts.txt port/decomp/$l.c 120 > work/ghidra_$n.log 2>&1
  grep -E "applied|decompiled" work/ghidra_$n.log
done
