// SPDX-License-Identifier: MIT

// konamidi: converts the music of Konami GBA games to MIDI + SoundFont.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX // MinGW's libstdc++ defines it already
#define NOMINMAX
#endif
#include <windows.h>
// shellapi.h needs the types from windows.h, so it has to come second.
#include <shellapi.h>
#endif

#include "convert.h"
#include "driver.h"
#include "files.h"
#include "rom.h"
#include "seqformat.h"
#include "sequencer.h"
#include "sf2.h"
#include "soundfont.h"

#ifndef KONAMIDI_VERSION
#define KONAMIDI_VERSION "dev"
#endif

namespace fs = std::filesystem;
using namespace konamidi;

namespace
{

void Usage(FILE* f)
{
    std::fprintf(f,
                 "konamidi %s - Konami GBA sound driver music ripper\n"
                 "\n"
                 "usage: konamidi [options] <file> [<file> ...]\n"
                 "\n"
                 "Converts every song of each GBA ROM (.gba) or GSF rip (.gsflib, .minigsf) to\n"
                 "a MIDI file and a matching SoundFont. The results go in a folder named after\n"
                 "the file, next to it. Files can also be dropped on the program.\n"
                 "\n"
                 "options:\n"
                 "  -o, --output DIR      output directory (default: <file's folder>/<base name>)\n"
                 "  -n, --name NAME       base name of the output files (default: file name)\n"
                 "  -s, --songs LIST      songs to convert, e.g. 0,3,7-9 (default: all)\n"
                 "  -l, --loops N         play looping songs' loop section N times (default: 2)\n"
                 "  -t, --tracks LIST     only convert these tracks, e.g. 4-15 (0-3 are the PSG\n"
                 "                        channels, 4 and up the sample voices; default: all)\n"
                 "      --single-sf2      write one SoundFont for all songs (bank = song number)\n"
                 "      --dump            also write a text listing of each song's sequence data\n"
                 "      --info            print the detected driver tables and songs, then exit\n"
                 "      --song-table ADDR    use this song table address (hex)\n"
                 "      --song-count N       use this many songs\n"
                 "      --sample-table ADDR  use this sample table address (hex)\n"
                 "      --mix-rate HZ        use this DirectSound mixer rate\n"
                 "      --trace SONG      print the sequencer's per-frame track output for SONG\n"
                 "                        (frame track pitch b2 vol trig flags key, and 2 pan\n"
                 "                        fields, which depend on the revision of the driver)\n"
                 "      --trace-frames N  frames to trace (default: 3000)\n"
                 "  -q, --quiet           only print warnings and errors\n"
                 "  -h, --help            show this help\n"
                 "      --version         show the version\n",
                 KONAMIDI_VERSION);
}

struct Options
{
    std::vector<std::string> inputs;
    std::string out_dir, name;
    std::set<int> songs;
    ConvertOptions convert;
    DriverOverrides overrides;
    bool info = false, dump = false, quiet = false, single_sf2 = false;
    int trace_song = -1;
    long long trace_frames = 3000;
};

// Parses the whole of `s` as a number.
bool ParseNumber(const std::string& s, long long& out, int base)
{
    char* end = nullptr;
    out = std::strtoll(s.c_str(), &end, base);
    return !s.empty() && *end == 0;
}

// Parses the whole of `s` as a decimal number.
bool ParseNumber(const std::string& s, double& out)
{
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return !s.empty() && *end == 0;
}

// Parses a list such as "0,3,7-9" into `values`.
bool ParseList(const std::string& s, std::set<int>& values)
{
    size_t start = 0;
    while (start <= s.size())
    {
        size_t comma = s.find(',', start);
        if (comma == std::string::npos)
        {
            comma = s.size();
        }

        // An item is a number or a range: "7" or "7-9".
        const std::string item = s.substr(start, comma - start);
        const size_t dash = item.find('-');
        const std::string first = item.substr(0, dash);
        const std::string last = dash == std::string::npos ? first : item.substr(dash + 1);
        long long a = 0, b = 0;
        if (!ParseNumber(first, a, 10) || !ParseNumber(last, b, 10) || a < 0 || b < a || b > 4096)
        {
            return false;
        }

        for (long long i = a; i <= b; i++)
        {
            values.insert(int(i));
        }

        start = comma + 1;
    }

    return true;
}

// Returns 0 to go on, -1 if there's nothing more to do (after --help or --version), or the exit code.
int ParseArgs(const std::vector<std::string>& args, Options& o)
{
    for (size_t i = 1; i < args.size(); i++)
    {
        auto value = [&](const char* what, std::string& out)
        {
            if (i + 1 >= args.size())
            {
                std::fprintf(stderr, "konamidi: %s needs a value\n", what);
                return false;
            }

            out = args[++i];

            return true;
        };

        const std::string& a = args[i];
        std::string v;
        long long n;
        if (a == "-h" || a == "--help")
        {
            Usage(stdout);
            return -1;
        }
        else if (a == "--version")
        {
            std::printf("konamidi %s\n", KONAMIDI_VERSION);
            return -1;
        }
        else if (a == "-o" || a == "--output")
        {
            if (!value("--output", o.out_dir))
            {
                return 2;
            }
        }
        else if (a == "-n" || a == "--name")
        {
            if (!value("--name", o.name))
            {
                return 2;
            }
        }
        else if (a == "-s" || a == "--songs")
        {
            if (!value("--songs", v))
            {
                return 2;
            }
            if (!ParseList(v, o.songs))
            {
                std::fprintf(stderr, "konamidi: bad song list\n");
                return 2;
            }
        }
        else if (a == "-l" || a == "--loops")
        {
            if (!value("--loops", v))
            {
                return 2;
            }
            if (!ParseNumber(v, n, 10) || n < 1 || n > 100)
            {
                std::fprintf(stderr, "konamidi: --loops must be between 1 and 100\n");
                return 2;
            }

            o.convert.loops = int(n);
        }
        else if (a == "-t" || a == "--tracks")
        {
            if (!value("--tracks", v))
            {
                return 2;
            }

            std::set<int> tracks;
            if (!ParseList(v, tracks) || tracks.empty() || *tracks.rbegin() >= kTracks)
            {
                std::fprintf(stderr, "konamidi: bad track list (tracks are 0-15)\n");
                return 2;
            }

            o.convert.track_mask = 0;
            for (int t : tracks)
            {
                o.convert.track_mask |= uint16_t(1u << t);
            }
        }
        else if (a == "--single-sf2")
        {
            o.single_sf2 = true;
        }
        else if (a == "--dump")
        {
            o.dump = true;
        }
        else if (a == "--info")
        {
            o.info = true;
        }
        else if (a == "--song-table" || a == "--sample-table")
        {
            if (!value(a.c_str(), v) || !ParseNumber(v, n, 16) || n <= 0 || n > 0xFFFFFFFF)
            {
                std::fprintf(stderr, "konamidi: %s needs a hex address\n", a.c_str());
                return 2;
            }

            (a == "--song-table" ? o.overrides.song_table : o.overrides.sample_table) = uint32_t(n);
        }
        else if (a == "--song-count")
        {
            if (!value("--song-count", v) || !ParseNumber(v, n, 10) || n < 1 || n > std::numeric_limits<int>::max())
            {
                std::fprintf(stderr, "konamidi: --song-count needs a positive number\n");
                return 2;
            }

            o.overrides.song_count = int(n);
        }
        else if (a == "--mix-rate")
        {
            // The same range DetectDriver accepts from the driver's timer setting.
            double& rate = o.overrides.mix_rate;
            if (!value("--mix-rate", v) || !ParseNumber(v, rate) || !(rate >= 4000 && rate <= 65536))
            {
                std::fprintf(stderr, "konamidi: --mix-rate needs a rate from 4000 to 65536 Hz\n");
                return 2;
            }
        }
        else if (a == "--trace")
        {
            if (!value("--trace", v) || !ParseNumber(v, n, 10) || n < 0 || n > std::numeric_limits<int>::max())
            {
                std::fprintf(stderr, "konamidi: --trace needs a song number\n");
                return 2;
            }

            o.trace_song = int(n);
        }
        else if (a == "--trace-frames")
        {
            if (!value("--trace-frames", v) || !ParseNumber(v, o.trace_frames, 10) || o.trace_frames < 1)
            {
                std::fprintf(stderr, "konamidi: --trace-frames needs a positive number\n");
                return 2;
            }
        }
        else if (a == "-q" || a == "--quiet")
        {
            o.quiet = true;
        }
        else if (a.size() > 1 && a[0] == '-')
        {
            std::fprintf(stderr, "konamidi: unknown option %s (see --help)\n", a.c_str());
            return 2;
        }
        else
        {
            o.inputs.push_back(a);
        }
    }

    if (o.inputs.empty())
    {
        Usage(stderr);
        return 2;
    }
    if (o.inputs.size() > 1 && (!o.name.empty() || o.trace_song >= 0))
    {
        std::fprintf(stderr, "konamidi: --name and --trace take a single input file\n");
        return 2;
    }

    return 0;
}

// Returns a time as minutes and seconds, such as 1:35.23. It's rounded to hundredths before it's split, so that 59.999
// seconds reads 1:00.00 and not 0:60.00.
std::string TimeString(double seconds)
{
    const long long hundredths = std::llround(seconds * 100);
    char b[32];
    std::snprintf(b, sizeof b, "%lld:%05.2f", hundredths / 6000, double(hundredths % 6000) / 100);

    return b;
}

// Prints the --info report: the ROM's title, the detection log and warnings, and for each song the address of its data,
// the number of tracks that play notes, and its length and loop.
void PrintInfo(const Rom& rom, const DriverInfo& info)
{
    std::printf("ROM: %s (%s)%s\n", rom.Title().c_str(), rom.GameCode().c_str(), rom.FromGsf() ? ", from GSF" : "");
    for (const std::string& l : info.log)
    {
        std::printf("  %s\n", l.c_str());
    }
    for (const std::string& w : info.warnings)
    {
        std::printf("  warning: %s\n", w.c_str());
    }

    std::printf("\n song  sequence    tracks  length     loop\n");
    for (int s = 0; s < info.song_count; s++)
    {
        SongHeader h;
        ReadSongHeader(rom, info.song_table, s, info.revision, h);
        if (h.base == 0)
        {
            std::printf("  %3d  (empty)\n", s);
            continue;
        }

        const std::unique_ptr<Sequencer> sequencer = Sequencer::Create(rom, info, s);
        Sequencer& seq = *sequencer;
        const bool dungeon_dice = info.revision == Revision::kDungeonDiceMonsters;
        std::set<int> tracks;
        uint32_t end = 0;
        int loop_start = -1, loop_end = -1;
        for (uint32_t f = 0; f < kMaxFrames; f++)
        {
            const auto& out = seq.Step();
            if (seq.LoopedLastFrame())
            {
                loop_end = int(f);
                loop_start = seq.LoopStartFrame();
                end = f;
                break;
            }

            // A PSG note starts at a volume above 0, and so does a sample note in the Dungeon Dice Monsters revision,
            // whose note records have flag 1.
            for (int t = 0; t < kTracks; t++)
            {
                const TrackOutput& o = out[t];
                const bool psg_note = IsPsgTrack(t) && o.trig && (o.flags & kOutPsgNote) && (o.vol & 15);
                const bool sample_note = dungeon_dice ? o.trig && o.flags == 1 && o.vol : o.flags & kOutNoteOn;
                if (psg_note || (!IsPsgTrack(t) && sample_note))
                {
                    tracks.insert(t);
                }
            }

            end = f + 1;
            if (seq.Stopped() || !seq.AnyTrackActive())
            {
                break;
            }
        }

        std::string loop = "-";
        if (loop_end >= 0)
        {
            loop = TimeString(loop_start * kFrameSeconds) + " - " + TimeString(loop_end * kFrameSeconds);
        }

        std::printf("  %3d  0x%08X  %6zu  %-9s  %s\n", s, h.base, tracks.size(),
                    TimeString(end * kFrameSeconds).c_str(), loop.c_str());
    }
}

// Prints each frame's track output records for --trace, one line per track: the same records the driver builds on its
// stack every frame, which tools/compare_trace.py compares.
int Trace(const Rom& rom, const DriverInfo& drv, const Options& o)
{
    const std::unique_ptr<Sequencer> sequencer = Sequencer::Create(rom, drv, o.trace_song);
    Sequencer& seq = *sequencer;
    if (!seq.Valid())
    {
        std::fprintf(stderr, "konamidi: song %d is invalid\n", o.trace_song);
        return 1;
    }

    // The last two fields are the record's bytes 8 and 9 in the Ultimate Masters revision, and 10 and 11 in the WCT
    // 2004 and Rave Master revisions. The Eternal Duelist and Dungeon Dice Monsters revisions' records have no pan, and
    // 0 there.
    const bool older = drv.revision != Revision::kUltimateMasters;
    const bool no_pan = drv.revision == Revision::kEternalDuelist || drv.revision == Revision::kDungeonDiceMonsters;
    const int tracks = TrackCount(drv.revision);
    for (long long f = 0; f < o.trace_frames && !seq.Stopped(); f++)
    {
        const auto& out = seq.Step();
        for (int t = 0; t < tracks; t++)
        {
            const TrackOutput& r = out[t];
            const int pan1 = no_pan ? 0 : older ? r.pan : r.pan_r;
            const int pan2 = no_pan ? 0 : older ? r.pan_start : r.pan_l;
            std::printf("%lld %d %d %d %d %d %d %d %d %d\n", f, t, r.pitch, r.b2, r.vol, r.trig, r.flags, r.key, pan1,
                        pan2);
        }
    }

    for (const std::string& w : drv.warnings)
    {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    for (const std::string& w : seq.Warnings())
    {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }

    return 0;
}

// Prints what became of a song that was converted, or skipped for playing no notes.
void ReportSong(int song, const SongSummary& r, uint16_t track_mask)
{
    if (r.silent)
    {
        std::printf("song %2d: %s, skipped\n", song,
                    track_mask == 0xFFFF ? "plays no notes" : "no notes on the chosen tracks");
        return;
    }

    std::string loop;
    if (r.loop_end_frame >= 0)
    {
        loop = ", loop " + TimeString(r.loop_start_frame * kFrameSeconds) + "-" +
               TimeString(r.loop_end_frame * kFrameSeconds);
    }

    const std::string midi = Utf8(PathFromUtf8(r.midi_path).filename());
    const std::string sf2 = r.sf2_path.empty() ? "" : ", " + Utf8(PathFromUtf8(r.sf2_path).filename());
    std::printf("song %2d: %s%s, %d tracks, %.1f BPM -> %s%s\n", song, TimeString(r.frames * kFrameSeconds).c_str(),
                loop.c_str(), r.tracks, r.bpm, midi.c_str(), sf2.c_str());
}

// Converts one input file, which messages call `label`. `done` holds the ROM images already converted in this run, so
// dropping several mini-GSFs of one set converts it once.
int ConvertFile(const std::string& input, const std::string& label, const Options& o, std::vector<fs::path>& done)
{
    Rom rom;
    std::string error;
    if (!rom.Load(input, error))
    {
        std::fprintf(stderr, "%s: %s\n", label.c_str(), error.c_str());
        return 1;
    }

    DriverInfo drv;
    if (!DetectDriver(rom, o.overrides, drv, error))
    {
        std::fprintf(stderr, "%s: %s\n", label.c_str(), error.c_str());
        return 1;
    }
    if (o.info)
    {
        PrintInfo(rom, drv);
        return 0;
    }
    if (o.trace_song >= 0)
    {
        return Trace(rom, drv, o);
    }

    // A mini-GSF only selects a song of its library, so the output is the library's music and is named after it.
    const fs::path source = PathFromUtf8(rom.GsfLibrary().empty() ? input : rom.GsfLibrary());
    std::error_code ec;
    // Compare file identities so case differences and links don't cause duplicate conversions.
    for (const fs::path& previous : done)
    {
        if (fs::equivalent(source, previous, ec))
        {
            if (!o.quiet)
            {
                std::printf("%s: same music as %s, already converted\n", label.c_str(),
                            Utf8(source.filename()).c_str());
            }

            return 0;
        }
    }
    done.push_back(source);

    const std::string name = SafeFileName(o.name.empty() ? Utf8(source.stem()) : o.name);
    const fs::path out_dir = o.out_dir.empty() ? source.parent_path() / PathFromUtf8(name) : PathFromUtf8(o.out_dir);
    fs::create_directories(out_dir, ec);
    if (ec)
    {
        std::fprintf(stderr, "%s: can't create %s: %s\n", label.c_str(), Utf8(out_dir).c_str(), ec.message().c_str());
        return 1;
    }

    ConvertOptions opt = o.convert;
    opt.out_dir = Utf8(out_dir);
    opt.base_name = name;

    // Report what detection found. Its warnings are printed even with --quiet, which leaves out the line that names the
    // file, so then the warnings and errors about the file start with its name.
    const std::string prefix = o.quiet ? label + ": " : "  ";
    if (!o.quiet)
    {
        std::printf("%s: %s (%s), %d song%s\n", label.c_str(), rom.Title().c_str(), rom.GameCode().c_str(),
                    drv.song_count, drv.song_count == 1 ? "" : "s");
        for (const std::string& l : drv.log)
        {
            std::printf("  %s\n", l.c_str());
        }
    }
    for (const std::string& w : drv.warnings)
    {
        std::fprintf(stderr, "%swarning: %s\n", prefix.c_str(), w.c_str());
    }

    std::unique_ptr<SoundfontBuilder> shared;
    if (o.single_sf2)
    {
        shared = std::make_unique<SoundfontBuilder>(rom, drv);
    }

    int failures = 0, converted = 0, silent = 0, selected = 0;
    for (int s = 0; s < drv.song_count; s++)
    {
        if (!o.songs.empty() && !o.songs.count(s))
        {
            continue;
        }

        SongHeader h;
        ReadSongHeader(rom, drv.song_table, s, drv.revision, h);
        if (h.base == 0)
        {
            continue; // empty entry
        }

        selected++;
        const SongSummary r = ConvertSong(rom, drv, s, opt, shared.get());
        if (!r.ok)
        {
            failures++;
        }
        else
        {
            (r.silent ? silent : converted)++;
            if (!o.quiet)
            {
                ReportSong(s, r, o.convert.track_mask);
            }
        }

        for (const std::string& w : r.warnings)
        {
            std::fprintf(stderr, "%ssong %d: %s\n", prefix.c_str(), s, w.c_str());
        }

        if (o.dump)
        {
            char file[32];
            std::snprintf(file, sizeof file, "_%02d.txt", s);
            if (!DumpSong(rom, drv, s, Utf8(out_dir / PathFromUtf8(name + file)), error))
            {
                std::fprintf(stderr, "%ssong %d: %s\n", prefix.c_str(), s, error.c_str());
                failures++;
            }
        }
    }

    if (shared && converted > 0)
    {
        Sf2File& f = shared->File();
        f.name = rom.Title();
        f.comment = "Instruments for " + rom.Title() + " (bank = song number), extracted by konamidi";
        const fs::path path = out_dir / PathFromUtf8(name + ".sf2");
        if (!f.Write(Utf8(path), error))
        {
            std::fprintf(stderr, "%s: %s\n", label.c_str(), error.c_str());
            failures++;
        }
        else if (!o.quiet)
        {
            std::printf("soundfont -> %s\n", Utf8(path.filename()).c_str());
        }
    }

    if (selected == 0)
    {
        std::fprintf(stderr, "%s: no songs selected\n", label.c_str());
        return 1;
    }

    if (!o.quiet)
    {
        std::printf("converted %d song%s", converted, converted == 1 ? "" : "s");
        if (silent)
        {
            std::printf(" (%d without notes skipped)", silent);
        }
        if (failures)
        {
            std::printf(", %d failed", failures);
        }
        std::printf("\noutput: %s\n", Utf8(fs::absolute(out_dir, ec).lexically_normal()).c_str());
    }

    return failures ? 1 : 0;
}

// Converts every input the arguments name. Returns the exit code.
int Run(const std::vector<std::string>& args)
{
#ifndef _WIN32
    // Keep progress and errors in order when both go to one pipe (the macOS droplet).
    std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
#endif

    Options o;
    const int parsed = ParseArgs(args, o);
    if (parsed)
    {
        return parsed < 0 ? 0 : parsed;
    }

    std::vector<fs::path> done;
    int result = 0;
    for (size_t i = 0; i < o.inputs.size(); i++)
    {
        if (i > 0 && !o.quiet)
        {
            std::printf("\n");
        }

        // On Windows a path that isn't valid UTF-8, such as a GSF's _lib name in another encoding, throws, and so does
        // running out of memory. That ends this file's conversion, but not the others'. The message starts with the
        // file's name, like the first line about any file, which the macOS droplet relies on.
        std::string label = o.inputs[i];
        try
        {
            label = Utf8(PathFromUtf8(o.inputs[i]).filename());
            if (ConvertFile(o.inputs[i], label, o, done))
            {
                result = 1;
            }
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "%s: %s\n", label.c_str(), e.what());
            result = 1;
        }
    }

    return result;
}

#ifdef _WIN32
// Returns the program's arguments in UTF-8. They're read as wide strings, so that paths outside the ANSI code page
// survive.
std::vector<std::string> CommandLine()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; argv && i < argc; i++)
    {
        const int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
        if (n > 1)
        {
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, &s[0], n, nullptr, nullptr);
        }

        args.push_back(s);
    }

    if (argv)
    {
        LocalFree(argv);
    }

    return args;
}

// Waits for Enter if konamidi has a console window of its own. Dropping files on konamidi.exe (or double-clicking it)
// gives it one, which closes as soon as konamidi exits, so this keeps it open for the results to be read.
void WaitIfOwnConsole()
{
    DWORD processes[2];
    if (GetConsoleProcessList(processes, 2) == 1)
    {
        std::printf("\nPress Enter to close this window.");
        std::fflush(stdout);
        std::getchar();
    }
}
#endif

} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    const int result = Run(CommandLine());
    WaitIfOwnConsole();
    (void)argc;
    (void)argv;

    return result;
#else
    return Run(std::vector<std::string>(argv, argv + argc));
#endif
}
