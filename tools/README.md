# Research and validation tools

These Python scripts run the game's sound driver from a ROM you supply. Use
them to study the driver or check a change to `konamidi` against the game's
own code. They aren't needed for conversion.

Setup (Python 3.10 or later):

```sh
python3 -m venv .venv
. .venv/bin/activate        # on Windows: .venv\Scripts\activate
pip install -r tools/requirements.txt
```

`compare_audio.py` also needs `ffmpeg`, and rendering a MIDI file for comparison
needs a SoundFont player such as `fluidsynth`.

| Script | Description |
|---|---|
| `gbarom.py` | loads a `.gba` ROM or a GSF rip; used by the other scripts |
| `gbadis.py` | recursive-descent Thumb/ARM disassembler that resolves literal pools and marks code a GSF rip has zeroed out |
| `driver_emu.py` | runs the game's own sound driver under the Unicorn ARM emulator: per-frame track output records, DirectSound mixer output, PSG register writes |
| `psg_model.py` | Game Boy APU model that renders the driver's PSG register writes to audio |
| `compare_trace.py` | diffs `konamidi --trace` against the emulated driver, frame by frame, for every song |
| `compare_notes.py` | checks a conversion's MIDI files and SoundFonts against the emulated driver, note by note |
| `compare_audio.py` | compares two renders: loudness envelopes, spectra and level ratio |
| `test_song.py` | writes a copy of a ROM with a test song that uses the commands and outputs its own songs don't |

## Checking konamidi against the driver

```sh
cd tools
python compare_trace.py ../rom.gba ../build/konamidi           # the sequencer: every song, 12000 frames
python compare_notes.py ../rom.gba ../build/konamidi ../rom    # the conversion in ../rom, note by note
python driver_emu.py ../rom.gba render-ds 10 3000 ref.wav       # driver's sample mix, song 10
../build/konamidi -q -o out -s 10 -t 4-15 ../rom.gba           # the same tracks as MIDI + SF2
fluidsynth -ni -R 0 -C 0 -F mine.wav out/rom_10.sf2 out/rom_10.mid
python compare_audio.py ref.wav mine.wav
```

`compare_notes.py` checks every note in a folder of MIDI files against what
the driver does with the hardware on the same frame: the note's start and stop,
the sample, duty cycle, wave or noise setting that plays, its pitch, its level
on each side and its echo send. If a MIDI track or an entire song is absent from the conversion, the
corresponding channel or song must also be silent in the driver. It lists the differences and the largest pitch and level
deviations. PSG notes are written at their equal-tempered pitch, while the
driver's frequency table can only get within one step of the Game Boy's 11-bit
frequency register, so that much is allowed for.

`render-ds` leaves the echo out unless you pass `--echo`, because MIDI can't
reproduce it, and fluidsynth's reverb and chorus are turned off to match.
`render-psg` covers the four PSG channels, and `--channel N` renders one of
them. `--voices` on `render-ds` renders chosen sample voices.

## Working out the driver

A GSF rip is a ROM from which `gsfopt` has zeroed every byte that the songs
never touched, so it holds only the code and data the music uses. That made
the driver easy to isolate:

1. **Find the entry points.** In a GSF rip, the ripper's patch calls the
   driver's "play song" and "start song" routines. Main's IRQ table points at
   the VBlank handler, which calls the per-frame routine.
2. **Disassemble.** `gbadis.py` was run from those entry points. The per-frame
   routine's command dispatch gave the byte code, and the output stage showed
   how each track's record reaches the PSG registers and the DirectSound
   voices. The mixer was found as ARM code copied to IWRAM.
3. **Model and compare.** The sequencer was reimplemented from that reading. It
   was then compared, record for record, with the driver running in
   `driver_emu.py`, until the two matched, loops included.
4. **Check the audio.** The converted MIDI and SoundFonts were rendered and
   compared with the driver's own mixer output, and with `psg_model.py`, for
   timing, pitch and level.

`driver_emu.py` picks the routine addresses by the ROM's game code. Those of
Yu-Gi-Oh! Ultimate Masters Edition: World Championship Tournament 2006 (`BY6J`,
`BY6E`), Shaman King: Master of Spirits 1 and 2 (`BSOE`, `B2ME`), Yu-Gi-Oh! Day
of the Duelist (`BY7E`), Yu-Gi-Oh! GX: Duel Academy (`BYGE`), Yu-Gi-Oh! World
Championship Tournament 2004 (`BYWP`), Rave Master: Special Attack Force
(`BRME`), Yu-Gi-Oh! The Eternal Duelist Soul (`AY5E`), Yu-Gi-Oh! Worldwide
Edition (`AYWE`) and Yu-Gi-Oh! Dungeon Dice Monsters (`AYDE`) are built in. For
another game, find the same routines, starting from the patterns in
`docs/FORMAT.md`, and add them to `GAMES`. A game whose driver has the same code
as one of these usually places the routines at different addresses. Match
the code while ignoring literal pools and call targets. Read the init arguments
from the game's call to the init routine, and the RAM addresses from its literal
pools.

An entry in `GAMES` also says how many tracks and voices the driver has,
where the track output records are and where their pan bytes are, how the
mixer scales a voice's level, whether it has echo and which voices it takes
from echo bus 0, and whether a pitch change reloads the wave RAM. Those differ
in the older revisions of `BYWP`, `BRME`, `AY5E`, `AYWE` and `AYDE`, whose
output stage is part of the sequencer. `compare_trace.py` and `compare_notes.py`
then check them the same way as the others. In `AY5E` and `AYWE`, a voice has
one level for both sides, and the per-frame entry runs the mixer itself, which
fills a ring buffer as far as the DMA 1 interrupt has moved its position. The
emulator skips that call, and when it renders, it moves the position on and
runs the mixer itself.

`AYDE` has no mixer. Its DMA 1 and DMA 2 interrupt routines each mix two voices
into a FIFO, 16 samples at a time, and each FIFO plays at its timer's rate,
which the note started last on it sets. When the emulator renders, it plays each
FIFO out at its timer's rate, runs the routine whenever a FIFO has 16 samples or
fewer left, and samples the two at 32768 Hz. `compare_notes.py` takes a voice's
pitch from its FIFO's timer period, and the entry says how its voice records and
sample table are laid out. Every write to a square channel restarts it, including vibrato updates.
The wave channel restarts only when it loads a new wave. `compare_notes.py`
therefore allows a PSG note to start on any frame that writes to its channel.
A note must start when a silent channel begins playing, the wave changes or
the noise restarts. DMA transfers into the sound registers, such as `AYDE`'s wave
RAM loads, count as register writes.

A game's songs may not use every command and output its driver has, so
`test_song.py` covers the others with a test song. It writes a copy of the ROM
with the test song in place of song 0, and its description says how to check it.
