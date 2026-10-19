# fooyin SACD ISO plugin

An input plugin for the [fooyin](https://github.com/fooyin/fooyin) music player that plays **Super Audio CD disc images** (`.iso`). Add a folder of SACD ISOs to your library and every track appears as a normal playlist entry with its title, performer and other disc metadata.

## Features

- Recognises Scarletbook SACD images and lists every track of the **stereo** and/or **multichannel** area.
- Reads disc and track text: title, performer, composer, songwriter, arranger, ISRC, genre, year, catalogue number and area information.
- Decodes **DSD** and **DST**-compressed audio through FFmpeg (`libavcodec`), reassembling frames across sector boundaries.
- Converts DSD to PCM at **352.8, 176.4 or 88.2 kHz** (default 176.4 kHz) with optional gain.
- Seeking, using the track timecodes stored on the disc.
- Settings to choose which areas are added to the library, the PCM rate and the gain.

## Status

| Area | State |
|---|---|
| Stereo, DST-compressed (real PS3 rip) | Recognised and listed in fooyin 0.13.1 (Flatpak); audio quality still being verified |
| Stereo and 5-channel, DSD and DST (generated test discs) | Checked by the decode test harness: tones, seeking, sample rates |
| Multichannel on a real disc | Not yet tested |
| DVD-Audio | Not supported |

The plugin has so far been tried on a single real disc, so audio correctness on other discs (noise, seeking accuracy, channel order) still needs confirmation. Reports are welcome, see [Feedback](#feedback).

## Requirements

- fooyin 0.13.x, or the development branch.
- FFmpeg with the DST and DSD decoders. A default `libavcodec` build has them. The plugin is skipped at build time if FFmpeg is not found.

## Install

The plugin is a compiled `.so` that must be built against the **exact fooyin version** (and the same Qt/FFmpeg libraries) you run, so a prebuilt file cannot be shared between setups. The scripts below do that for you. **Rebuild after every fooyin update.**

First check how fooyin is installed: Flatpak (`flatpak list | grep fooyin`), Debian/Ubuntu package (`dpkg -l fooyin`), RPM (`rpm -q fooyin`), Arch (`pacman -Q fooyin`) or Snap (`snap list fooyin`).

### Flatpak (Flathub)

Flatpak apps use their own Qt and FFmpeg, so the plugin has to be built inside the matching KDE SDK. The script detects your installed fooyin version and KDE runtime automatically:

```sh
git clone https://github.com/herculepirate/fooyin-sacdiso.git
cd fooyin-sacdiso
sh scripts/build-flatpak.sh
```

It needs `flatpak`, `git` and network access. It installs the matching `org.kde.Sdk` from Flathub if missing, fetches the matching fooyin source, builds the plugin (10-20 minutes), and copies `fyplugin_sacdiso.so` to `~/.var/app/org.fooyin.fooyin/.local/lib/fooyin/plugins/`.

The Flatpak can only read `~/Music`, `~/Downloads` and `/run/media` by default. Give it access to the folder holding your ISOs, then restart fooyin:

```sh
flatpak override --user --filesystem=/path/to/your/isos:ro org.fooyin.fooyin
```

To hold fooyin updates until you are ready to rebuild, use `flatpak mask org.fooyin.fooyin` (undo with `flatpak mask --remove org.fooyin.fooyin`).

### Debian / Ubuntu (.deb from the releases page, or a distro package)

Install the build tools and libraries once (this is fooyin's own build dependency list from `BUILD.md`):

```sh
sudo apt update
sudo apt install g++ git cmake pkg-config ninja-build libglu1-mesa-dev libxkbcommon-dev zlib1g-dev \
    libasound2-dev libtag1-dev libicu-dev libpipewire-0.3-dev libpulse-dev \
    qt6-base-dev libqt6sql6-sqlite libqt6svg6-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
    libavcodec-dev libavfilter-dev libavformat-dev libavutil-dev libswresample-dev
```

Then build and install the plugin for the fooyin you have installed:

```sh
git clone https://github.com/herculepirate/fooyin-sacdiso.git
cd fooyin-sacdiso
sh scripts/build-native.sh
```

The script reads the installed version from `dpkg`, builds that release's source with the plugin added, and copies
`fyplugin_sacdiso.so` to `~/.local/lib/fooyin/plugins/`. If fooyin is not installed via `dpkg`, set the version yourself:
`FOOYIN_VERSION=0.13.1 sh scripts/build-native.sh`. Restart fooyin afterwards.

### Fedora (and RPM distros)

```sh
sudo dnf install gcc-c++ git cmake ninja-build pkgconf-pkg-config glib2-devel libxkbcommon-x11-devel libxkbcommon-devel \
    zlib-ng-compat-devel alsa-lib-devel qt6-qtbase-devel qt6-qtsvg-devel qt6-qttools-devel \
    libavcodec-free-devel libavfilter-free-devel libavformat-free-devel libavutil-free-devel libswresample-free-devel \
    taglib-devel kdsingleapplication-qt6-devel libicu-devel pipewire-devel pulseaudio-libs-devel

git clone https://github.com/herculepirate/fooyin-sacdiso.git
cd fooyin-sacdiso
sh scripts/build-native.sh
```

The script reads the version from `rpm -q fooyin`. Fedora's FFmpeg ("free" build) includes the DST and DSD decoders. On openSUSE and other RPM distros install the equivalent `-devel` packages.

### Arch Linux and derivatives (Manjaro, EndeavourOS)

```sh
sudo pacman -S --needed gcc git cmake pkgconf ninja alsa-lib pipewire libpulse icu zlib ffmpeg \
    qt6-base qt6-svg qt6-imageformats qt6-tools kdsingleapplication taglib

git clone https://github.com/herculepirate/fooyin-sacdiso.git
cd fooyin-sacdiso
sh scripts/build-native.sh
```

The script reads the version from `pacman -Q fooyin` (the repo or AUR package).

### Other distributions (Gentoo, Nix, Void, Alpine, ...)

Install fooyin's build dependencies from [BUILD.md](https://github.com/fooyin/fooyin/blob/master/BUILD.md) using your package manager, then run:

```sh
FOOYIN_VERSION=<your fooyin version, e.g. 0.13.1> sh scripts/build-native.sh
```

The plugin is copied to `~/.local/lib/fooyin/plugins/`, which fooyin always searches. You can use another folder by setting `FOOYIN_PLUGIN_PATH`.

### Windows and macOS

Not supported or tested yet. The code is portable C++/Qt/FFmpeg, but it has only been built and run on Linux.

### Snap

Not supported or tested. The Snap in the store is published by a third party, can lag behind fooyin releases, and runs in its own sandbox, so a plugin built outside it will usually fail to load. Use the `.deb` or Flatpak, or build fooyin with the plugin from source.

### AppImage

Not tested. An AppImage bundles its own libraries, so a plugin built on your system may not load. Prefer `.deb` or Flatpak.

### From the fooyin source tree

1. Copy `plugin/` to `src/plugins/sacdiso` in the fooyin source.
2. In `src/plugins/CMakeLists.txt`, add `sacdiso` to `FOOYIN_KNOWN_PLUGINS` and `FOOYIN_DEFAULT_PLUGINS`, and add `fooyin_add_selected_plugin(sacdiso)`.
3. Build fooyin as usual.

## Use

1. Restart fooyin, then open **Settings → Plugins** and check that **SACD ISO** is listed and enabled (*Initialised*, not *Invalid*).
2. Add the folder containing your `.iso` files to the library.
3. Tracks show up with their disc metadata. Play them like any other file.

## Settings

**Settings → Plugins → SACD ISO**

| Setting | Options | Default |
|---|---|---|
| Audio areas | Stereo and multichannel / Stereo only / Multichannel only | Both |
| DSD to PCM sample rate | 352.8 kHz / 176.4 kHz / 88.2 kHz | 176.4 kHz |
| Gain | −24 to +24 dB | 0 dB |

Area selection applies the next time the library is scanned.

## Troubleshooting

- **Plugin shows as *Invalid*:** it was built for a different fooyin version or library set. Rebuild with `scripts/build-flatpak.sh` (Flatpak) or `scripts/build-native.sh` (everything else), and remove any older `fyplugin_sacdiso*.so` from the plugins folder.
- **Plugin not found at all:** fooyin only looks in its plugin search paths. Run `fooyin 2>&1 | grep -i plugin` (Flatpak: `flatpak run org.fooyin.fooyin 2>&1 | grep -i plugin`) and check that the folder you installed to is listed. The loading error, if any, is printed there too.
- **ISOs do not appear (Flatpak):** the sandbox cannot see the folder. Add it with the `flatpak override` command above.

## Limitations

- DSD is converted to PCM. There is no native DSD output.
- SACD images contain no cover art, so none is shown.
- DVD-Audio is not implemented.

## Development

Everything under `tools/` is for testing without any copyrighted audio:

- `tools/make_sacd_iso.py` generates a small synthetic SACD image with known sine tones: `make_sacd_iso.py out.iso [--no-multi] [--seconds-per-track N] [--dst-raw]`.
- `tools/test_backend.cpp` and `tools/test_sacd_core.cpp` decode those images and check tones, seeking and sample rates.

`docs/PULL_REQUEST.md` holds the text prepared for proposing the plugin upstream to fooyin.

## Feedback

Please [open an issue](https://github.com/herculepirate/fooyin-sacdiso/issues) with your fooyin version (`fooyin --version`), how it was installed (Flatpak, deb, RPM, Arch or source), and what fooyin prints when started from a terminal.

Use this with disc images of discs you own. No copyrighted audio is included in this repository.

## License

GPL-3.0-or-later, the same as fooyin. See [LICENSE](LICENSE).
