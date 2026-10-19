# FroggySM

**StepMania 3.9 for the Data Frog GB300 and SF2000.**

A from-scratch C port of PSPMania's StepMania 3.9, written for low-end MIPS32
hardware. Runs as a libretro core on the Deimos/Multicore firmware.

The PSPMania original is a C++/Rage-framework port that needs the PSP SDK,
DirectX, OpenGL, Lua, XML, threads and the PSP's hardware MP3 decoder. None of
that exists on a 200 MHz MIPS32 chip with 32 MB of RAM, so this is a fresh,
self-contained engine in plain C targeting 320x240 RGB565.

## Features

- `.sm` / MSD chart parsing, all five difficulties
- Hold notes and jumps
- `#BPMS`, `#STOPS`, `#OFFSET` with correct beat↔second mapping
- ScoreKeeperMAX2 scoring, judgment windows, `AAAA`…`D` grade tiers
- Song wheel with folder (album) browsing
- Song preview, pause/resume, quit back to the wheel
- MP3 streaming (whole file in RAM, no SD I/O during gameplay)
- PNG/JPG backgrounds, banners and note skins
- Song clock driven by decoded audio samples, so notes never drift

Not implemented: online scores, chart editing, battle mode, 3D lanes.

## Requirements

A Data Frog GB300 or SF2000 with the Deimos/Multicore firmware, and a FAT32
microSD card.

## Installation

### 1. Copy the core

Download **`froggysm.sf2k`** from the
[releases page](https://github.com/Synaps33/FroggySM/releases). One binary runs
on both consoles.

```bash
cp froggysm.sf2k /media/<CARD>/system/Deimos/cores/froggysm.sf2k
```

### 2. Create the ROM folder and the game file

The launcher derives the **core name from the folder name** and then looks for
`system/Deimos/cores/<folder>.sf2k`. The folder and the core file must therefore
be spelled **identically, including letter case** — a difference in case alone
is enough for the lookup to miss and the launch to fail silently.

```bash
mkdir -p /media/<CARD>/ROMS/froggysm/Songs
printf '' > /media/<CARD>/ROMS/froggysm/froggysm
```

That gives you:

```
system/Deimos/cores/froggysm.sf2k   ← core   (loaded)
ROMS/froggysm/froggysm              ← game   (passed to the core)
ROMS/froggysm/Songs/                ← songs
```

### 3. Optional: a menu stub

The stock file browser wants a `.gba` stub. The firmware reads its *contents* and
parses them as

```
<core id>;<ROM folder>;<ROM file>.<extension>
```

The third field **must contain a dot** — the extension is stripped, and a bare
filename makes the parse fail, leaving you with a `STOCK` screen.

```bash
printf 'froggysm;froggysm;froggysm.gba' > '/media/<CARD>/ROMS/froggysm;FroggySM.gba'
sync
```

Under the FrogUI menu you can skip this: it builds the same parameters from the
folder and file names in steps 1 and 2.

### 4. Launch

Boot the console, open the file browser and start **FroggySM**.

Two symptoms of a misconfigured install:

- **`STOCK` screen** — the stub in step 3 was rejected. Check it with
  `cat '/media/<CARD>/ROMS/froggysm;FroggySM.gba'`; it must print
  `froggysm;froggysm;froggysm.gba`.
- **Nothing happens at all** — the core lookup missed. Verify that the folder
  and the core file are spelled identically:
  `ls /media/<CARD>/ROMS/ | grep froggy` and
  `ls /media/<CARD>/system/Deimos/cores/ | grep froggy`.

## Adding songs

Standard StepMania layout — one folder per album, one per song:

```
/ROMS/froggysm/Songs/
└── <artist or pack>/
    └── <song>/
        ├── song.sm        # chart (required)
        ├── song.mp3       # referenced by #MUSIC
        ├── banner.png     # optional
        └── background.png # optional
```

`#MUSIC:` is resolved relative to the chart's folder. Charts without a `#TITLE:`
borrow the folder name, split on the first ` -`, ` ~`, ` (` or `[`, the same rule
StepMania uses. Sub-folders become albums — press **SELECT** on the select screen
to cycle through `[ ALL SONGS ]` and each one.

Charts are re-scanned from the SD card when the game starts, so there is no
separate refresh step after copying files.

## Audio format

The MP3 decoder is software (minimp3) on a soft-float CPU, so **bitrate directly
costs frame time**. A 320 kbps file will stutter; a 128 kbps file will not.
Re-encode your library once:

```bash
find /ROMS/froggysm/Songs -iname '*.mp3' -print0 |
while IFS= read -r -d '' f; do
    ffmpeg -y -i "$f" -vn -ar 44100 -ac 2 -b:a 128k -c:a libmp3lame "$f.tmp.mp3" &&
    mv "$f.tmp.mp3" "$f"
done
```

**44.1 kHz stereo 128 kbps CBR** is the sweet spot: it divides evenly into the
console's 22050 Hz output, so the resampler never drifts. `-vn` also drops
embedded cover art from the ID3 tag, which costs RAM and parser time.

## Controls

Nintendo Switch ABXY layout: **X** top, **B** bottom, **Y** left, **A** right.

**Select screen**

| Action | Input |
|---|---|
| Previous / next song | D-Pad Up / Down |
| Previous / next difficulty | D-Pad Left / Right, or L / R |
| Next folder / album | SELECT |
| Play | A or START |
| Stop preview | B |

**Gameplay**

| Lane | Input |
|---|---|
| Left | D-Pad Left or **Y** |
| Down | D-Pad Down or **B** |
| Up | D-Pad Up or **X** |
| Right | D-Pad Right or **A** |
| Pause / resume | START |
| Quit to the wheel | SELECT (while paused) |

## Building from source

### Host test runner

Builds and runs on Linux/macOS. The runner loads the library, decodes every
chart, plays a song, auto-plays the chart and checks audio/video integrity:

```bash
make test SONGS_PATH=/path/to/Songs
```

It dumps PPM screenshots of each screen into the working directory, which is
handy for checking layout changes without flashing a console.

### Cross-compiling the core

`make device` produces `dist/froggysm.sf2k`. It needs the Frog/MTI
`mipsel-mti-elf` toolchain and a checkout of the multicore frontend that wraps
the core into a `core_87000000` payload:

```bash
make device \
    MULTICORE_DIR=/path/to/sf2000_multicore \
    SONGS_PATH=/path/to/Songs
```

If you need the payload linked for the other firmware revision, pass
`FROGGY_TYPE=GB300V2` — the game code is identical either way, only the BIOS
syscall addresses and the LCD register value differ.

### Install to SD

With the card mounted:

```bash
./install_to_sd.sh
```

It installs the core, creates the folders and the ROM entry, and copies the song
library.

## Credits

- **StepMania** — the rhythm game engine this port re-implements
- **PSPMania** by **mafu9** — the PSP port this takes its feature set and note
  skin assets from
- **minimp3** by **lieff** — CC0 MP3 decoder
- **stb_image** — public domain image loader
- The Frog/MTI MIPS toolchain and the Deimos/Multicore frontend by the Data
  Frog / SF2000 community

MIT licensed. StepMania-derived logic and the bundled PSPMania note skin keep
their own terms — see [LICENSE](LICENSE).

## Links

- Source: <https://github.com/Synaps33/FroggySM>
- Releases: <https://github.com/Synaps33/FroggySM/releases>
- Issues: <https://github.com/Synaps33/FroggySM/issues>