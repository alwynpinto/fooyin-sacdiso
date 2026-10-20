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
# The runtime the app runs on, e.g. org.kde.Platform/x86_64/6.9. (`flatpak info -r`
# prints the app's own ref, app/org.fooyin.fooyin/x86_64/stable, which is not it.)
RUNTIME="$(flatpak info --show-runtime "$APP")"
ARCH="$(echo "$RUNTIME" | cut -d/ -f2)"
BRANCH="${RUNTIME##*/}"
SDK="org.kde.Sdk/$ARCH/$BRANCH"
# Install the SDK where fooyin itself is installed (system or user), so flatpak
# does not stop to ask when both installations have a flathub remote.
WHERE="$(flatpak list --app --columns=application,installation | awk -v a="$APP" '$1 == a {print $2; exit}')"
case "$WHERE" in system) WHERE=--system ;; *) WHERE=--user ;; esac
echo "fooyin $VERSION, runtime $RUNTIME -> $SDK ($WHERE)"

if ! flatpak info "$SDK" >/dev/null 2>&1; then
  flatpak install "$WHERE" -y --noninteractive flathub "$SDK" \
    || { echo "Could not install $SDK from flathub"; exit 1; }
fi

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
