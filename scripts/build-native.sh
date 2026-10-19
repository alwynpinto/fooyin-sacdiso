#!/bin/sh
# Build and install the SACD ISO plugin for a natively installed fooyin
# (.deb from the fooyin releases page, distro package, or a self-built fooyin).
#   sh scripts/build-native.sh            # auto-detects the installed version
#   FOOYIN_VERSION=0.13.1 sh scripts/build-native.sh
set -eu

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${SACDISO_WORK:-$HOME/.cache/fooyin-sacdiso-build}"
DEST="$HOME/.local/lib/fooyin/plugins"

if [ -z "${FOOYIN_VERSION:-}" ]; then
  if command -v dpkg-query >/dev/null 2>&1 && dpkg-query -W fooyin >/dev/null 2>&1; then
    FOOYIN_VERSION="$(dpkg-query -W -f='${Version}' fooyin | sed 's/^[0-9]*://; s/^\([0-9][0-9.]*\).*/\1/')"
  elif command -v rpm >/dev/null 2>&1 && rpm -q fooyin >/dev/null 2>&1; then
    FOOYIN_VERSION="$(rpm -q --qf '%{VERSION}' fooyin)"
  elif command -v pacman >/dev/null 2>&1 && pacman -Q fooyin >/dev/null 2>&1; then
    FOOYIN_VERSION="$(pacman -Q fooyin | sed 's/^fooyin //; s/^[0-9]*://; s/^\([0-9][0-9.]*\).*/\1/')"
  elif command -v fooyin >/dev/null 2>&1; then
    FOOYIN_VERSION="$(fooyin --version 2>/dev/null | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
  fi
fi
[ -n "${FOOYIN_VERSION:-}" ] || { echo "Could not detect the fooyin version; set FOOYIN_VERSION=x.y.z"; exit 1; }
echo "Building for fooyin $FOOYIN_VERSION"

for tool in git cmake ninja g++ pkg-config; do
  command -v "$tool" >/dev/null || { echo "Missing build tool: $tool (see README for the package list)"; exit 1; }
done

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d fooyin ] || [ "$(git -C fooyin describe --tags 2>/dev/null)" != "v$FOOYIN_VERSION" ]; then
  rm -rf fooyin build
  git clone --depth 1 --branch "v$FOOYIN_VERSION" https://github.com/fooyin/fooyin.git fooyin
fi

rm -rf fooyin/src/plugins/sacdiso
cp -r "$REPO_DIR/plugin" fooyin/src/plugins/sacdiso
if ! grep -q sacdiso fooyin/src/plugins/CMakeLists.txt; then
  sed -i 's/^\(\s*\)sndfile$/\1sndfile\n\1sacdiso/' fooyin/src/plugins/CMakeLists.txt
  sed -i 's/^fooyin_add_selected_plugin(sndfile)/&\nfooyin_add_selected_plugin(sacdiso)/' fooyin/src/plugins/CMakeLists.txt
fi

cmake -G Ninja -S fooyin -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
ninja -C build sacdiso

SO="$(find build -name fyplugin_sacdiso.so | head -1)"
[ -n "$SO" ] || { echo "Build produced no plugin"; exit 1; }
mkdir -p "$DEST"
cp "$SO" "$DEST/"
echo "Installed $DEST/fyplugin_sacdiso.so"
echo "Restart fooyin."
