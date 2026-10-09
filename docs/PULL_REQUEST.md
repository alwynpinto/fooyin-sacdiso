# Add SACD ISO input plugin

Adds an input plugin (`sacdiso`) that plays Super Audio CD disc images (`.iso`).

## What it does
- Detects Scarletbook images (`SACDMTOC` at LSN 510) and exposes every track of the stereo and/or multichannel area as a subsong.
- Reads disc and track text (title, performer, composer, songwriter, arranger, ISRC, genre, year, catalogue) and area info.
- Decodes DSD and DST through libavcodec (`dsd_msbf`, `dst`), reassembling frames across sectors; seeking via the track timecodes.
- Outputs float PCM from DSD at 352.8/176.4/88.2 kHz via swresample (setting; default 176.4 kHz), with optional gain.
- Settings: area selection (both / stereo / multichannel), PCM rate, gain.

## Notes for reviewers
- Needs FFmpeg with the DST and DSD decoders (present in a default libavcodec build); the plugin is skipped if FFmpeg is not found.
- Works on both the development branch and v0.13.x (the two small API differences are handled with a macro and a `requires` check; drop them when merging).
- Includes a synthetic-image generator and a decode test harness under `tools/`; no copyrighted audio is included.
- Tested with generated discs (DSD and DST layout, stereo and 5-channel). A real PS3 rip (DST stereo) is recognised and listed with correct metadata; audio correctness on real DST streams still needs confirmation (TODO before submitting).
- DVD-Audio is not implemented.

## Not yet done
- Hooking the tools into fooyin's test suite.
- Cover art (SACD images have none).
