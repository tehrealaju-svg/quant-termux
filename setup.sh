#!/data/data/com.termux/files/usr/bin/bash
# One-shot installer for Termux: installs build tools, builds qnt, puts it on your PATH.
set -e
echo "== Quant phone wallet setup =="
if command -v pkg >/dev/null 2>&1; then
  # Update first: a new clang with an old libc++ runtime fails to link.
  yes | pkg upgrade -y -o Dpkg::Options::=--force-confnew
  pkg install -y clang cmake git make
fi
cd "$(dirname "$0")"
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc 2>/dev/null || echo 2)"
BIN="${PREFIX:-/usr/local}/bin"
cp build/qnt "$BIN/qnt" 2>/dev/null || { mkdir -p "$HOME/bin"; cp build/qnt "$HOME/bin/qnt"; BIN="$HOME/bin"; }
echo
echo "Done! Run:  qnt        (installed to $BIN)"
