# konamidi

`konamidi` converts music from Konami Game Boy Advance games to MIDI files
and SoundFonts.

Some of Konami's GBA titles don't use Nintendo's standard sound engine (MP2K,
the "Sappy" engine), so the usual tools such as Sappy, gba-mus-ripper and
agbplay can't read them. `konamidi` finds Konami's own driver in a ROM,
converts each song to a Standard MIDI File, and builds a matching SoundFont
from the game's samples and Game Boy PSG waveforms.

## Supported games

The tool was developed on *Yu-Gi-Oh! Ultimate Masters Edition: World
Championship Tournament 2006*, and it supports these games too:

* *Shaman King: Master of Spirits*
* *Shaman King: Master of Spirits 2*
* *Yu-Gi-Oh! Day of the Duelist: World Championship Tournament 2005*
* *Yu-Gi-Oh! GX: Duel Academy*

These games use variants of the *Ultimate Masters Edition* driver.

The following games use older driver revisions with different commands.
These are supported too:

* *Yu-Gi-Oh! World Championship Tournament 2004*
* *Rave Master: Special Attack Force*
* *Yu-Gi-Oh! The Eternal Duelist Soul*
* *Yu-Gi-Oh! Worldwide Edition: Stairway to the Destined Duel*
* *Yu-Gi-Oh! Dungeon Dice Monsters*

The driver is located from its own code rather than from fixed addresses, so
other Konami games that use the same driver revisions should work too. Their
songs may still use unsupported commands. An unknown command stops the track
and produces a warning. `--info` lists the detected tables and any assumptions
made during detection. You can also supply table addresses with the override
options.

If a game has a revision of the driver that the tool doesn't know, it says so.

## Building

For 64-bit Windows, download the zip containing `konamidi.exe` from GitHub
Releases.

You need a C++17 compiler and CMake 3.20 or later. There are no other
dependencies.

* **Windows:** Visual Studio 2022 or later with the "Desktop development with
  C++" workload, which includes CMake. MinGW-w64 works too.
* **macOS:** the Xcode command line tools (`xcode-select --install`) and CMake,
  for example from Homebrew (`brew install cmake`).
* **Linux:** GCC or Clang, and CMake from your distribution, for example
  `sudo apt install build-essential cmake` on Debian and Ubuntu.

Then, in the source folder:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

The binary is `build/konamidi` (`konamidi.exe` on Windows). Visual Studio
and Xcode builds put it in `build/Release/` instead. On macOS the build also
makes `build/Konamidi.app`, which you can drop files on.

`ctest` runs the unit tests, and on macOS it also checks the report that
`Konamidi.app` puts together from konamidi's output. CI builds konamidi and
runs the tests on Windows, macOS and Linux.

## Usage

```sh
konamidi game.gba
```

This writes one `.mid` and one `.sf2` per song to a folder named after the
input, next to it: for example `game/game_00.mid` and `game/game_00.sf2`. Give
several files to convert them together. Characters that Windows forbids in
filenames are replaced with underscores. Trailing dots and spaces are removed,
and reserved device names such as `CON` get an underscore prefix.

Input can be a raw `.gba` ROM or a GSF rip (`.gsflib`, `.minigsf` or `.gsf`).
A `.minigsf` selects a song from the library named in its `_lib` tag. The
library is loaded from the same folder and supplies the output name. If you
pass several `.minigsf` files from one set, the library is converted once.
Conversion includes all songs in the ROM, regardless of the GSF's selected song.

Use a ROM if you have one. A GSF rip keeps only the bytes used during playback.
If every song cuts a sample short, the unused part is silent in the resulting
SoundFont.

### Drag and drop

* **Windows:** drop `.gba`, `.gsflib` or `.minigsf` files on `konamidi.exe`.
  A console window shows what was converted and stays open until you press
  Enter.
* **macOS:** drop them on `Konamidi.app`, or open it and choose them. It then
  shows what was converted and offers to open the output folders.

Each file's results go in a folder next to it, as above.

### Options

| Option | Meaning |
|---|---|
| `-o, --output DIR` | output directory (default: a folder next to each input, named like the output files) |
| `-n, --name NAME` | base name of the output files (default: input name) |
| `-s, --songs LIST` | only these songs, e.g. `0,3,7-9` |
| `-l, --loops N` | play a looping song's loop section N times (default 2) |
| `-t, --tracks LIST` | only these tracks: 0-3 are the PSG channels, 4 and up the sample voices |
| `--single-sf2` | one SoundFont for all songs; each song's instruments are in bank = song number |
| `--dump` | also write a text listing of every command of each song (`NAME_NN.txt`) |
| `--info` | print the driver tables found and a list of songs, then exit |
| `--song-table ADDR`, `--song-count N`, `--sample-table ADDR`, `--mix-rate HZ` | override detection |
| `-q, --quiet` | only print warnings and errors |
| `--trace SONG`, `--trace-frames N` | print the sequencer's raw per-frame output (for `tools/compare_trace.py`) |

To listen, load the pair into any SoundFont player, for example:

```sh
fluidsynth -ni -F song10.wav game/game_10.sf2 game/game_10.mid
```

## Output

### MIDI

Each MIDI file has a conductor track and one track per game track that plays
notes. Tracks are named `Square 1`, `Square 2`, `Wave`, `Noise`, and
`Voice 0`-`Voice 11` (game tracks 4-15, which play the driver's voices 0-11).
The older Rave Master, Eternal Duelist and Dungeon Dice Monsters revisions have
8, 6 and 4 voices.
MIDI channel 10 is used only if a song needs all 16 channels.

The game counts time in frames, so one MIDI tick is one frame (1/59.73 s) and
every event is exactly on the frame the game plays it. The game doesn't store
a tempo, so `konamidi` estimates the beat length from the note spacing. This
only affects how bars line up in an editor.

Songs that loop are written with the loop played twice by default. The loop is
also marked with `loopStart` and `loopEnd` marker events, which loop-aware
players honour.

The game's controls map to MIDI like this:

| Game | MIDI |
|---|---|
| note | note on/off; velocity is always 127 |
| volume and pan | CC11 (expression) for loudness, CC10 for pan (see below) |
| pitch bend, vibrato | pitch bend, with the bend range set per channel by RPN 0 |
| echo | CC91 (reverb send) on the voices routed to it |
| instrument changes | program changes |

In the game, volume can change during a note. CC11 is used instead of velocity
so that such changes carry over.

CC11 and CC10 together reproduce the game's per-side levels under the
constant-power pan law that most SoundFont players use.

In the Eternal Duelist revision, a PSG note at the current volume changes the
channel's frequency without restarting it. The MIDI file represents this as a
pitch bend of the current note. A series of these notes can span more than
24 semitones; the bend range is adjusted to fit.

In the Dungeon Dice Monsters revision, songs set separate left and right PSG
volumes. These are included in each PSG track's CC10 and CC11 values.

### SoundFont

Each song's SoundFont contains exactly the instruments that song uses.

* **Sample tracks** get one preset per track (sometimes more, for samples
  whose keys would collide). The preset maps MIDI keys to the samples the
  track plays:
  * Samples played at several pitches are placed at their musical pitch, which
    is estimated from the waveform, so melodies read as the right notes.
  * Samples played at a single pitch, typically drums, get a key of their own.
* **Square channels** get one preset per duty cycle.
* **Wave channel** gets one preset per wave and volume. The driver stores a
  scaled copy of each wave for every volume. Quiet notes have coarser waveforms,
  which the presets preserve. The Dungeon Dice Monsters revision keeps each
  wave at full scale and sets the volume in the hardware, so its songs get one
  preset per wave.
* **Noise channel** gets one preset with a key per noise note, synthesised from
  the Game Boy's LFSR, and more presets if a song plays more noise notes than
  there are keys.

Samples keep the game's rate and loop points.

### Sequence listing (`--dump`)

The listing shows every command of every track with its address, frame, raw
bytes, meaning and following delay. It's the quickest way to study a song or
to check the format documentation against real data.

## Accuracy

Some things differ from the hardware, or can't be expressed in MIDI and
SoundFonts:

* **PSG tuning.** The Game Boy's 11-bit frequency registers can't hit every
  pitch, so the game plays its high PSG notes slightly out of tune. The
  driver's frequency table is within about 3 cents of equal temperament below
  C4, but up to about 7 cents off in the C4 octave, 13 in the C5 octave, 25
  in the C6 octave and 50 in the C7 octave. The wave channel plays an octave
  lower than the squares from the same table. The MIDI files keep the
  equal-tempered pitch.
* **Echo.** The driver's echo is a feedback delay (up to about 190 ms). It's
  approximated with the reverb send; the delay and feedback are written to the
  conductor track as text events.
* **Mixer character.** The GBA mixer resamples without interpolation at
  20 or 21 kHz and writes 8-bit output. The Dungeon Dice Monsters revision has
  no mixer, and plays 8-bit samples at their own rate of 10 kHz. A SoundFont
  player plays the same samples more cleanly.
* **PSG DC offset.** The Game Boy channels output a unipolar signal. The
  SoundFont uses the same waveforms without the DC offset, which the hardware's
  output capacitor removes anyway.

## Internals

`docs/FORMAT.md` documents the driver and its data formats in full, including
how the tool locates them.

`tools/` holds the scripts used to reverse engineer the driver and validate the
conversion: a disassembler, a harness that runs the game's own driver under an
ARM emulator, and comparison scripts. See `tools/README.md`.

## License

`konamidi` is released under the MIT License; see `LICENSE`. The DEFLATE
decoder in `src/inflate.cpp` is adapted from Mark Adler's puff under the zlib
license. Its notice is in that file and in `THIRD_PARTY_NOTICES`. Game data is
not covered by these licenses.
