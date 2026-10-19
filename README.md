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
| Stereo, DST-compressed (real PS3 rip) | Recognised, listed and played in fooyin 0.13.1 (Flatpak) |
| Stereo and 5-channel, DSD and DST (generated test discs) | Checked by the decode test harness: tones, seeking, sample rates |
| Multichannel on a real disc | Not yet tested |
| DVD-Audio | Not supported |

The plugin has so far been tried on a single real disc, so audio correctness on other discs (noise, seeking accuracy, channel order) still needs confirmation. Reports are welcome, see [Feedback](#feedback).

## Requirements

- fooyin 0.13.x, or the development branch.
- FFmpeg with the DST and DSD decoders. A default `libavcodec` build has them. The plugin is skipped at build time if FFmpeg is not found.

## Install

### Flatpak fooyin

Flatpak apps use their own Qt and FFmpeg, so the plugin has to be built inside the matching KDE SDK. The script detects your installed fooyin version and KDE runtime automatically:

```sh
git clone https://github.com/herculepirate/fooyin-sacdiso.git
cd fooyin-sacdiso
sh scripts/build-flatpak.sh
```

It needs `flatpak`, `git` and network access. It installs the matching `org.kde.Sdk` from Flathub if missing, fetches the matching fooyin source, builds the plugin, and copies `fyplugin_sacdiso.so` to `~/.var/app/org.fooyin.fooyin/.local/lib/fooyin/plugins/`.

Then let fooyin read your ISOs, and restart it:

```sh
flatpak override --user --filesystem=/path/to/your/isos:ro org.fooyin.fooyin
```

**Rebuild after every fooyin update.** The plugin is tied to the exact fooyin version and Flatpak runtime. To hold updates until you are ready, use `flatpak mask org.fooyin.fooyin`.

### From the fooyin source tree

1. Copy `plugin/` to `src/plugins/sacdiso` in the fooyin source.
2. In `src/plugins/CMakeLists.txt`, add `sacdiso` to `FOOYIN_KNOWN_PLUGINS` and `FOOYIN_DEFAULT_PLUGINS`, and add `fooyin_add_selected_plugin(sacdiso)`.
3. Build fooyin as usual.

## Use

1. Restart fooyin, then open **Settings → Plugins** and check that **SACD ISO** is listed and enabled (not marked *Invalid*).
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

- **Plugin shows as *Invalid*:** it was built for a different fooyin version. Rebuild with `scripts/build-flatpak.sh`, and remove any older `fyplugin_sacdiso*.so` from the plugins folder.
- **Plugin not found at all:** fooyin only looks in its plugin search paths. Run `flatpak run org.fooyin.fooyin 2>&1 | grep -i plugin` and check that the plugins folder above is listed.
- **ISOs do not appear:** the Flatpak sandbox cannot see the folder. Add it with the `flatpak override` command above.

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

Please [open an issue](https://github.com/herculepirate/fooyin-sacdiso/issues) with your fooyin version (`fooyin --version`), how it was installed (Flatpak or source), and what fooyin prints when started from a terminal.

Use this with disc images of discs you own. No copyrighted audio is included in this repository.

## License

GPL-3.0-or-later, the same as fooyin. See [LICENSE](LICENSE).
