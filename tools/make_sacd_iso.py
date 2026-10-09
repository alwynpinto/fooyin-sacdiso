#!/usr/bin/env python3
"""
Builds a small synthetic SACD (Scarletbook) ISO for testing the isoaudio plugin.

- Area 1 (stereo, TWOCHTOC): two tracks. Left = 1 kHz sine, right = 2 kHz sine (track 1);
  left = 500 Hz, right = 3 kHz (track 2). Audio is uncompressed DSD (second order
  sigma-delta), densely packed across sectors so frames share sectors.
- Optionally Area 2 (5ch, MULCHTOC) with one track where channel N carries (N+1)*500 Hz.

Usage: make_sacd_iso.py out.iso [--no-multi] [--seconds-per-track N]
"""
import argparse
import math
import struct
import sys

SECTOR = 2048
FRAME_RATE = 75
FRAME_BYTES_PER_CH = 4704  # 588 * 64 / 8
DSD_RATE = 2822400


def sigma_delta_2nd(freq, nbits, amp=0.5, phase=0.0):
    """Second order sigma-delta modulator, returns list of bits (0/1)."""
    bits = bytearray(nbits)
    i1 = 0.0
    i2 = 0.0
    y = 1.0
    w = 2.0 * math.pi * freq / DSD_RATE
    sin = math.sin
    for n in range(nbits):
        x = amp * sin(w * n + phase)
        i1 += x - y
        i2 += i1 - y
        y = 1.0 if i2 >= 0.0 else -1.0
        bits[n] = 1 if y > 0 else 0
    return bits


def pack_msb_first(bits):
    out = bytearray(len(bits) // 8)
    for i in range(len(out)):
        b = 0
        base = i * 8
        for k in range(8):
            b = (b << 1) | bits[base + k]
        out[i] = b
    return out


def tc_bytes(frame_index):
    m = frame_index // (60 * FRAME_RATE)
    s = (frame_index // FRAME_RATE) % 60
    f = frame_index % FRAME_RATE
    return bytes([m, s, f])


def tc_with_flags(frame_index):
    return tc_bytes(frame_index) + b"\x00"


def build_frames(freqs, nframes, first_frame_index, dst_raw=False):
    """Returns list of (timecode_index, bytes) uncompressed DSD frames, byte interleaved per channel."""
    chans = []
    total_bits = nframes * FRAME_BYTES_PER_CH * 8
    for freq in freqs:
        chans.append(pack_msb_first(sigma_delta_2nd(freq, total_bits)))
    frames = []
    for fi in range(nframes):
        lo = fi * FRAME_BYTES_PER_CH
        hi = lo + FRAME_BYTES_PER_CH
        data = bytearray(FRAME_BYTES_PER_CH * len(freqs))
        for ch, c in enumerate(chans):
            data[ch::len(freqs)] = c[lo:hi]
        frames.append((first_frame_index + fi, (b"\x00" + bytes(data)) if dst_raw else bytes(data)))
    return frames


def pack_sectors(frames, dst=False):
    """Densely packs frames into audio sectors. Returns (sectors, frame_start_sector list, frame_end_sector list)."""
    sectors = []
    starts = []
    ends = []
    fi = 0
    offset = 0  # offset into the current frame
    fsz = 4 if dst else 3  # frame info entry size
    while fi < len(frames):
        parts = []  # (frame_start, data, tc)
        used = 0
        nfi = 0
        cur_fi, cur_off = fi, offset
        sector_index = len(sectors)
        while cur_fi < len(frames) and len(parts) < 7:
            tc, data = frames[cur_fi]
            start_here = cur_off == 0
            np_ = len(parts) + 1
            nfi_ = nfi + (1 if start_here else 0)
            cap = SECTOR - (1 + 2 * np_ + fsz * nfi_) - used
            if cap <= 0:
                break
            cap = min(cap, 2045)
            take = min(len(data) - cur_off, cap)
            parts.append((start_here, data[cur_off:cur_off + take], tc))
            if start_here:
                nfi += 1
                starts.append(sector_index)
            used += take
            cur_off += take
            if cur_off == len(data):
                ends.append(sector_index)
                cur_fi += 1
                cur_off = 0
        fi, offset = cur_fi, cur_off

        # padding if the sector isn't full (end of stream)
        np_ = len(parts)
        header = 1 + 2 * np_ + fsz * nfi
        free = SECTOR - header - used
        pad_part = None
        if free > 0:
            if np_ < 7:
                np2 = np_ + 1
                header2 = 1 + 2 * np2 + fsz * nfi
                free2 = SECTOR - header2 - used
                if free2 > 0:
                    pad_part = (False, bytes(min(free2, 2045)), None)
                    np_ = np2
                    header = header2
                    free = free2
        hdr = bytes([(np_ << 5) | (nfi << 2) | (1 if dst else 0)])
        pinfo = bytearray()
        finfo = bytearray()
        body = bytearray()
        for start_here, data, tc in parts:
            b0 = (1 << 7 if start_here else 0) | (2 << 3) | ((len(data) >> 8) & 7)
            pinfo += bytes([b0, len(data) & 0xFF])
            body += data
            if start_here:
                finfo += tc_bytes(tc)
                if dst:
                    finfo += bytes([(5 << 2)])  # sector_count=5, stereo bits clear
        if pad_part:
            _, data, _ = pad_part
            b0 = (7 << 3) | ((len(data) >> 8) & 7)
            pinfo += bytes([b0, len(data) & 0xFF])
            body += data
        sector = bytearray(hdr + bytes(pinfo) + bytes(finfo) + bytes(body))
        sector += bytes(SECTOR - len(sector))
        assert len(sector) == SECTOR, len(sector)
        sectors.append(bytes(sector))
    return sectors, starts, ends


def text_sector(ident, strings=None):
    s = bytearray(SECTOR)
    s[0:8] = ident
    return s


def build_master_toc(area1_toc, area1_toc2, area1_size, area2_toc, area2_toc2, area2_size, album, artist):
    toc = bytearray(SECTOR * 10)
    toc[0:8] = b"SACDMTOC"
    toc[8] = 1
    toc[9] = 0x14
    struct.pack_into(">H", toc, 16, 1)  # album set size
    struct.pack_into(">H", toc, 18, 1)  # sequence number
    toc[24:24 + 9] = b"TEST-0001"
    toc[40 + 3] = 5  # album genre: classical
    struct.pack_into(">I", toc, 64, area1_toc)
    struct.pack_into(">I", toc, 68, area1_toc2)
    struct.pack_into(">I", toc, 72, area2_toc)
    struct.pack_into(">I", toc, 76, area2_toc2)
    struct.pack_into(">H", toc, 84, area1_size)
    struct.pack_into(">H", toc, 86, area2_size)
    toc[88:88 + 9] = b"TEST-0001"
    struct.pack_into(">H", toc, 120, 2026)
    toc[122] = 10
    toc[123] = 8
    toc[128] = 1
    toc[136:138] = b"en"
    toc[138] = 2  # ISO 8859-1
    # SACDText sectors 1..8
    for i in range(8):
        base = SECTOR * (1 + i)
        toc[base:base + 8] = b"SACDText"
    base = SECTOR
    pos = 0x60

    def put(slot, text):
        nonlocal pos
        struct.pack_into(">H", toc, base + 16 + 2 * slot, pos)
        raw = text.encode("latin-1") + b"\x00"
        toc[base + pos:base + pos + len(raw)] = raw
        pos += len(raw) + 1

    put(0, album)
    put(1, artist)
    put(2, "Synthetic Records")
    base_man = SECTOR * 9
    toc[base_man:base_man + 8] = b"SACD_Man"
    return bytes(toc)


DST_MODE = False


def build_area_toc(ident, channels, loudspeaker, area_start, area_end, tracks, size_sectors, titles, performers):
    """tracks: list of dicts with start_lsn, length_lsn, start_frame, dur_frames."""
    toc = bytearray(SECTOR * size_sectors)
    t = toc
    t[0:8] = ident
    t[8] = 1
    t[9] = 0x14
    struct.pack_into(">H", t, 10, size_sectors)
    t[20] = 4  # sample frequency code
    t[21] = 0 if DST_MODE else 3  # frame format: 0 = DST, 3 = DSD 3-in-16
    t[32] = channels
    t[33] = loudspeaker << 3
    t[34] = channels
    t[69] = len(tracks)
    struct.pack_into(">I", t, 72, area_start)
    struct.pack_into(">I", t, 76, area_end)
    t[80] = 1  # text area count
    t[88:90] = b"en"
    t[90] = 2
    struct.pack_into(">H", t, 144, 152)
    t[152:152 + 12] = b"Test area ST"[:12]
    # sector 1: SACDTTxt
    base = SECTOR
    t[base:base + 8] = b"SACDTTxt"
    pos = 8 + 2 * 255 if False else 8 + 2 * len(tracks) + 8
    for i, (title, perf) in enumerate(zip(titles, performers)):
        struct.pack_into(">H", t, base + 8 + 2 * i, pos)
        blob = bytearray()
        blob += bytes([2, 0, 0, 0])
        blob += bytes([0x01, 0x20]) + title.encode("latin-1") + b"\x00"
        blob += bytes([0x02, 0x20]) + perf.encode("latin-1") + b"\x00"
        t[base + pos:base + pos + len(blob)] = blob
        pos += len(blob) + 2
    # sector 2: SACDTRL1
    base = SECTOR * 2
    t[base:base + 8] = b"SACDTRL1"
    for i, tr in enumerate(tracks):
        struct.pack_into(">I", t, base + 8 + 4 * i, tr["start_lsn"])
        struct.pack_into(">I", t, base + 8 + 255 * 4 + 4 * i, tr["length_lsn"])
    # sector 3: SACDTRL2
    base = SECTOR * 3
    t[base:base + 8] = b"SACDTRL2"
    for i, tr in enumerate(tracks):
        t[base + 8 + 4 * i:base + 8 + 4 * i + 4] = tc_with_flags(tr["start_frame"])
        t[base + 8 + 255 * 4 + 4 * i:base + 8 + 255 * 4 + 4 * i + 4] = tc_with_flags(tr["dur_frames"])
    return bytes(t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--no-multi", action="store_true")
    ap.add_argument("--dst-raw", action="store_true", help="DST sector layout with uncompressed (DstCoded=0) frames")
    ap.add_argument("--seconds-per-track", type=float, default=0.6)
    args = ap.parse_args()
    global DST_MODE
    DST_MODE = args.dst_raw

    per_track = max(4, int(round(args.seconds_per_track * FRAME_RATE)))
    pregap = 2

    AREA1_TOC1 = 540
    AREA1_TOC2 = 560
    AREA1_SIZE = 4
    AREA2_TOC1 = 580
    AREA2_TOC2 = 600
    AREA2_SIZE = 4
    AREA1_START = 700

    print("generating stereo audio...", file=sys.stderr)
    pre = build_frames([440.0, 440.0], pregap, 0, args.dst_raw)
    t1 = build_frames([1000.0, 2000.0], per_track, pregap, args.dst_raw)
    t2 = build_frames([500.0, 3000.0], per_track, pregap + per_track, args.dst_raw)
    frames = pre + t1 + t2
    sectors, starts, ends = pack_sectors(frames, args.dst_raw)

    fidx = {tc: i for i, (tc, _) in enumerate(frames)}
    tracks = []
    for first_tc, count in ((pregap, per_track), (pregap + per_track, per_track)):
        s = starts[fidx[first_tc]]
        e = ends[fidx[first_tc + count - 1]]
        tracks.append(
            dict(start_lsn=AREA1_START + s, length_lsn=e - s + 1, start_frame=first_tc, dur_frames=count)
        )
    # first track range includes the pregap
    tracks[0]["length_lsn"] += tracks[0]["start_lsn"] - AREA1_START
    tracks[0]["start_lsn"] = AREA1_START

    area1_end = AREA1_START + len(sectors) - 1
    area1_toc = build_area_toc(
        b"TWOCHTOC", 2, 0, AREA1_START, area1_end, tracks, AREA1_SIZE,
        ["Alpha tone", "Bravo tone"], ["Test Ensemble", "Test Ensemble"],
    )

    multi_sectors = []
    area2_toc = None
    area2_start = area1_end + 20
    if not args.no_multi:
        print("generating 5ch audio...", file=sys.stderr)
        mframes = build_frames([500.0 * (i + 1) for i in range(5)], per_track, 0, args.dst_raw)
        multi_sectors, mstarts, mends = pack_sectors(mframes, args.dst_raw)
        mtracks = [dict(start_lsn=area2_start, length_lsn=len(multi_sectors), start_frame=0, dur_frames=per_track)]
        area2_toc = build_area_toc(
            b"MULCHTOC", 5, 3, area2_start, area2_start + len(multi_sectors) - 1, mtracks, AREA2_SIZE,
            ["Surround tone"], ["Test Ensemble"],
        )

    master = build_master_toc(
        AREA1_TOC1, AREA1_TOC2, AREA1_SIZE,
        0 if args.no_multi else AREA2_TOC1, 0 if args.no_multi else AREA2_TOC2, 0 if args.no_multi else AREA2_SIZE,
        "Synthetic SACD", "Test Ensemble",
    )

    total_sectors = (area2_start + len(multi_sectors) + 8) if not args.no_multi else (area1_end + 8)
    image = bytearray(SECTOR * total_sectors)
    image[510 * SECTOR:510 * SECTOR + len(master)] = master
    image[AREA1_TOC1 * SECTOR:AREA1_TOC1 * SECTOR + len(area1_toc)] = area1_toc
    image[AREA1_TOC2 * SECTOR:AREA1_TOC2 * SECTOR + len(area1_toc)] = area1_toc
    if area2_toc:
        image[AREA2_TOC1 * SECTOR:AREA2_TOC1 * SECTOR + len(area2_toc)] = area2_toc
        image[AREA2_TOC2 * SECTOR:AREA2_TOC2 * SECTOR + len(area2_toc)] = area2_toc
    for i, s in enumerate(sectors):
        image[(AREA1_START + i) * SECTOR:(AREA1_START + i + 1) * SECTOR] = s
    for i, s in enumerate(multi_sectors):
        image[(area2_start + i) * SECTOR:(area2_start + i + 1) * SECTOR] = s

    with open(args.out, "wb") as f:
        f.write(image)
    print("wrote", args.out, len(image), "bytes;", len(sectors), "stereo sectors;", len(multi_sectors),
          "multichannel sectors; tracks:", tracks, file=sys.stderr)


if __name__ == "__main__":
    main()
