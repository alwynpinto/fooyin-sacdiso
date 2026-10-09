# fooyin SACD ISO plugin

Plays Super Audio CD disc images (`.iso`) in [fooyin](https://github.com/fooyin/fooyin): stereo and multichannel areas, DSD and DST, track metadata, seeking.

Status: recognises and lists a real PS3 SACD rip (stereo, DST) in fooyin 0.13.1 (Flatpak); playback quality on real DST streams is still being verified. Multichannel needs more testing. DVD-Audio is not supported.

## Install
**Flatpak fooyin** (plugins must be built inside the matching KDE SDK):
```sh
git clone <this repo> && cd fooyin-sacdiso
sh scripts/build-flatpak.sh
flatpak override --user --filesystem=/path/to/your/isos:ro org.fooyin.fooyin
```
Rebuild after fooyin updates (the plugin is tied to the exact fooyin version).

**From source:** copy `plugin/` to `src/plugins/sacdiso`, add `sacdiso` to `FOOYIN_KNOWN_PLUGINS`/`FOOYIN_DEFAULT_PLUGINS` and `fooyin_add_selected_plugin(sacdiso)` in `src/plugins/CMakeLists.txt`, then build fooyin.

## Settings
Settings → Plugins → SACD ISO: area selection (both/stereo/multichannel), PCM rate (352.8/176.4/88.2 kHz), gain.

## Development
`tools/make_sacd_iso.py` generates synthetic SACD images (`--dst-raw`, `--no-multi`); `tools/test_backend.cpp` decodes them and checks tones, seeking and rates. See `docs/PULL_REQUEST.md` for the upstream proposal text.

## License
GPL-3.0-or-later, same as fooyin.
