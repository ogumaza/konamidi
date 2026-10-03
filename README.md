# supergbamidi

`supergbamidi` converts the music of Game Boy Advance games that use Konami's
or Rare's sound driver to MIDI files and SoundFonts.

These games don't use Nintendo's standard sound engine (MP2K, the "Sappy"
engine), so the usual tools such as Sappy, gba-mus-ripper and agbplay can't
read them. `supergbamidi` finds the game's driver in a ROM, converts each song
to a Standard MIDI File, and builds a matching SoundFont from the game's
samples and instruments. For Konami's driver, it also includes Game Boy PSG
waveforms.

## Supported games

### Konami's driver

This part of the tool was developed on *Yu-Gi-Oh! Ultimate Masters Edition:
World Championship Tournament 2006*, and it supports these games too:

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

The tool reports unrecognised revisions of Konami's driver.

### Rare's driver

* *Donkey Kong Country*
* *Donkey Kong Country 2*
* *Banjo-Kazooie: Grunty's Revenge*
* *Banjo-Pilot*
* *Sabre Wulf*
* *It's Mr. Pants*

### Other games

Each driver is located from its own code rather than from fixed addresses, so
other games that use the same driver revisions should work too. Their songs
may still use unsupported commands. An unknown command stops the track and
produces a warning. `--info` names the driver it found and lists the detected
tables and any assumptions made during detection. If detection fails, you can
name the driver and supply table addresses with the override options.

## Building

For 64-bit Windows, download the zip containing `supergbamidi.exe` from
GitHub Releases.

You need a C++20 compiler and CMake 3.20 or later. There are no other
dependencies.

* **Windows:** Visual Studio 2022 or later with the "Desktop development with
  C++" workload, which includes CMake. MinGW-w64 works too.
* **macOS:** the Xcode command line tools (`xcode-select --install`) and CMake,
  for example from Homebrew (`brew install cmake`).
* **Linux:** GCC 10 or Clang 10 or later, and CMake from your distribution,
  for example `sudo apt install build-essential cmake` on Debian and Ubuntu.

Then, in the source folder:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

The binary is `build/supergbamidi` (`supergbamidi.exe` on Windows). Visual
Studio and Xcode builds put it in `build/Release/` instead. On macOS the build
also makes `build/Supergbamidi.app`, which you can drop files on.

`ctest` runs the unit tests, and on macOS it also checks the report that
`Supergbamidi.app` puts together from the program's output. CI builds
supergbamidi and runs the tests on Windows, macOS and Linux.

## Usage

```sh
supergbamidi game.gba
```

This writes one `.mid` and one `.sf2` per song to a folder beside the input
file, with the same name: for example `game/game_00.mid` and
`game/game_00.sf2`. Pass several files to convert them together. Characters
that Windows forbids in filenames are replaced with underscores. Trailing dots and spaces are removed,
and reserved device names such as `CON` get an underscore prefix.

Input can be a raw `.gba` ROM or a GSF rip (`.gsflib`, `.minigsf` or `.gsf`).
A `.minigsf` selects a song from the library named in its `_lib` tag. The
library is loaded from the same folder and supplies the output name. If you
pass several `.minigsf` files from one set, the library is converted once.
Conversion includes all songs in the ROM, regardless of the GSF's selected song.

Use a ROM if you have one. A GSF rip keeps only the bytes used during playback,
so a song that the rip didn't play may be missing, and if every song cuts a
sample short, the unused part is silent in the SoundFont.

### Drag and drop

* **Windows:** drop `.gba`, `.gsflib` or `.minigsf` files on
  `supergbamidi.exe`. A console window shows what was converted and stays open
  until you press Enter.
* **macOS:** drop them on `Supergbamidi.app`, or open it and choose them. It
  then shows what was converted and offers to open the output folders.

Each file's results go in a folder next to it, as above.

### Options

| Option | Meaning |
|---|---|
| `-o, --output DIR` | output directory (default: a folder next to each input, named like the output files) |
| `-n, --name NAME` | base name of the output files (default: input name) |
| `-s, --songs LIST` | only these songs, e.g. `0,3,7-9` |
| `-l, --loops N` | play each song's loop N times (default 2) |
| `-t, --tracks LIST` | only these tracks, e.g. `4-15`; in Konami's driver, 0-3 are the PSG channels and 4 and up the sample voices |
| `--single-sf2` | one SoundFont for all songs (see [Output](#output) for its banks) |
| `--dump` | also write a text listing of every command of each song (`NAME_NN.txt`) |
| `--info` | print the driver and tables found and a list of songs, then exit. The track count, length and loop reflect the current conversion options |
| `--driver NAME` | use `konami` or `rare` instead of detecting the driver |
| `--song-table ADDR`, `--song-count N` | override detection: the song table's address (hex) and the number of songs |
| `--sample-table ADDR`, `--mix-rate HZ` | override detection in Konami's driver: the sample table's address (hex) and the mixer's rate |
| `-q, --quiet` | only print warnings and errors |
| `--trace SONG`, `--trace-frames N` | print the driver model's state after each frame (for the driver's `compare_trace.py` in `tools/`) |

Detection looks for Rare's driver first, because it's found from its code
alone, and then for Konami's. With `--song-table` but no `--driver`, the table
goes to whichever driver detection finds, so a game whose driver isn't
recognised needs `--driver` as well. `--sample-table` and `--mix-rate` imply
`--driver konami`.

To listen, load the pair into any SoundFont player, for example:

```sh
fluidsynth -ni -F song10.wav game/game_10.sf2 game/game_10.mid
```

## Output

Songs that loop are written with the loop played twice by default. The loop is
also marked with `loopStart` and `loopEnd` marker events, which loop-aware
players honour.

### MIDI from Konami's driver

Each MIDI file has a conductor track and one track per game track that plays
notes. Tracks are named `Square 1`, `Square 2`, `Wave`, `Noise`, and
`Voice 0`-`Voice 11` (game tracks 4-15, which play the driver's voices 0-11).
The older Rave Master, Eternal Duelist and Dungeon Dice Monsters revisions have
8, 6 and 4 voices. MIDI channel 10 is used only if a song needs all 16
channels.

The game counts time in frames, so one MIDI tick is one frame (1/59.73 s) and
every event is exactly on the frame the game plays it. The game doesn't store
a tempo, so `supergbamidi` estimates the beat length from the note spacing.
This only affects how bars line up in an editor.

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

### SoundFont from Konami's driver

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

With `--single-sf2`, each song's instruments are in the bank numbered after the
song.

### MIDI from Rare's driver

The tune format resembles MIDI: note, controller, program and pitch bend
commands on 16 MIDI channels, separated by delays in ticks. The MIDI file
keeps the tune's ticks per quarter note, channels, keys, velocities, programs
and volumes (controller 7). Each source track that plays notes gets a MIDI
track named `Track 0`, `Track 1` and so on. A separate first track holds the
tempo and loop markers.

The driver counts each frame as 1/60 s, but the GBA shows 59.73 frames a
second, so the games play every tune 0.46% slower than its tempo says. The
MIDI file's tempos are slowed down to match.

The MIDI file follows what the driver plays, where that differs from the
tune's commands:

* The driver plays a note in one of a few slots for its channel, and drops a
  note that finds them all playing. The MIDI file leaves such notes out, and
  ends each note where the driver's slot stops playing it, such as when a note
  in mono mode (controller 126) cuts off the one before.
* A note off releases only one note with that key on the channel. Since a
  MIDI channel can't play the same key twice at once, the conversion ends
  the older note when the newer one starts.
* The driver's vibrato (controller 1) is written out as pitch bends, a frame
  at a time, together with the tune's bends. Each channel's bend range is
  set with RPN 0 to fit the bends and the vibrato.
* A note that a track plays on the same tick as a program change gets the
  program that the driver plays it with.

### SoundFont from Rare's driver

Each tune's SoundFont has a preset for each program the tune plays, named
`Program N` and with the same number, in bank 0. Programs played on channel 10,
which General MIDI players keep for drums, are in bank 128 as well, so that
such players play the same instruments there.

* **Samples** are the game's 8-bit samples, widened to 16 bits, with their
  rate, root key, fine tune and loop. A very short loop is repeated until it's
  at least 32 points long, since some players can't play shorter ones.
* **Drum kits and key splits** become a zone for each range of keys that plays
  the same instrument. A drum kit's zones play their samples at their own
  pitch on every key, as the driver does.
* **Envelopes** follow the driver's attack, decay, sustain and release. The
  driver's fades are straight lines in level, where a SoundFont's are straight
  lines in decibels, so decays and releases are stretched to keep the
  loudness close.
* **Volume and velocity** scale the level in a straight line, as in the
  driver, through modulators in each instrument. The SoundFont default would
  square them.

With `--single-sf2`, the tunes that use the first tune's instruments use bank
0, and each other set of instruments gets a bank of its own, which the tunes
select with a bank change at the start.

### Sequence listing (`--dump`)

The listing shows every track command with its address, raw bytes and meaning.
For Konami's driver, it includes the frame and the delay after the command;
for Rare's, it includes the tick. Use it to study a song or check the format
documentation against real data.

## Accuracy

Some things differ from the hardware, or can't be expressed in MIDI and
SoundFonts.

### Konami's driver

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

### Rare's driver

* **Voice limit.** The driver mixes at most 8 voices at a time and leaves the
  rest silent until voices free up. A SoundFont player plays every note, so a
  busy passage can have notes in the MIDI file that the game doesn't let you
  hear.
* **Envelopes.** The SoundFont's envelopes approximate the driver's straight
  fades, and some players treat very short envelope phases differently.
* **Timing.** The driver runs once a frame, so it plays every event on a frame
  boundary. The MIDI file keeps each event on its own tick, which can be up to
  a frame earlier.
* **Mixer character.** The driver mixes in mono at 13379 Hz with linear
  interpolation, and writes 8-bit output. A SoundFont player plays the same
  samples more cleanly, and the MIDI files leave every channel in the centre.
* **Sound effects** aren't converted. The driver plays them from a separate
  table, which only the game's code uses.

## Internals

`docs/konami.md` and `docs/rare.md` document the two drivers and their data
formats in full, including how the tool locates them. The code for each is in
`src/konami/` and `src/rare/`, behind the interface in `src/music.h`.

`tools/` holds the scripts used to reverse engineer the drivers and validate
the conversion: a disassembler, harnesses that run each game's driver
under an ARM emulator, and comparison scripts. See `tools/README.md`.

## License

`supergbamidi` is released under the MIT License; see `LICENSE`. The DEFLATE
decoder in `src/inflate.cpp` is adapted from Mark Adler's puff under the zlib
license. Its notice is in that file and in `THIRD_PARTY_NOTICES`. Game data is
not covered by these licenses.
