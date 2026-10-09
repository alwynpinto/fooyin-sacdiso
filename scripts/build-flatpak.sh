#!/bin/sh
# Build and install the SACD ISO plugin for the Flatpak version of fooyin.
# Detects the installed fooyin version and KDE runtime automatically.
#   sh scripts/build-flatpak.sh
set -eu

APP=org.fooyin.fooyin
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${SACDISO_WORK:-$HOME/.cache/fooyin-sacdiso-build}"
DEST="$HOME/.var/app/$APP/.local/lib/fooyin/plugins"

command -v flatpak >/dev/null || { echo "flatpak not found"; exit 1; }
flatpak info "$APP" >/dev/null 2>&1 || { echo "$APP is not installed"; exit 1; }

VERSION="$(flatpak info "$APP" | awk '/Version:/ {print $2; exit}')"
RUNTIME="$(flatpak info -r "$APP")"          # e.g. org.kde.Platform/x86_64/6.11
BRANCH="${RUNTIME##*/}"
SDK="org.kde.Sdk//$BRANCH"
echo "fooyin $VERSION, runtime $RUNTIME -> $SDK"

flatpak install -y --noninteractive --or-update flathub "$SDK" || true

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d fooyin ] || [ "$(git -C fooyin describe --tags 2>/dev/null)" != "v$VERSION" ]; then
  rm -rf fooyin build
  git clone --depth 1 --branch "v$VERSION" https://github.com/fooyin/fooyin.git fooyin
fi

rm -rf fooyin/src/plugins/sacdiso
cp -r "$REPO_DIR/plugin" fooyin/src/plugins/sacdiso
if ! grep -q sacdiso fooyin/src/plugins/CMakeLists.txt; then
  sed -i 's/^\(\s*\)sndfile$/\1sndfile\n\1sacdiso/' fooyin/src/plugins/CMakeLists.txt
  sed -i 's/^fooyin_add_selected_plugin(sndfile)/&\nfooyin_add_selected_plugin(sacdiso)/' fooyin/src/plugins/CMakeLists.txt
fi

flatpak run --command=sh --share=network --filesystem="$WORK" "$SDK" -c "
  set -e
  cd '$WORK'
  cmake -G Ninja -S fooyin -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
  ninja -C build sacdiso
"

SO="$(find "$WORK/build" -name fyplugin_sacdiso.so | head -1)"
[ -n "$SO" ] || { echo "build produced no plugin"; exit 1; }
mkdir -p "$DEST"
cp "$SO" "$DEST/"
echo "Installed $DEST/fyplugin_sacdiso.so"
echo "To read ISOs outside Music/Downloads:  flatpak override --user --filesystem=/path/to/isos:ro $APP"
echo "Restart fooyin."
