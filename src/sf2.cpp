// SPDX-License-Identifier: MIT

#include "sf2.h"

#include <algorithm>
#include <cstring>

#include "files.h"

namespace konamidi
{
namespace
{

// Builds a RIFF file in memory: little-endian values, and chunks whose sizes are filled in when they end.
struct Writer
{
    void U8(uint8_t v)
    {
        buf.push_back(v);
    }

    void U16(uint16_t v)
    {
        U8(uint8_t(v));
        U8(uint8_t(v >> 8));
    }

    void U32(uint32_t v)
    {
        U16(uint16_t(v));
        U16(uint16_t(v >> 16));
    }

    void Id(const char* s)
    {
        buf.insert(buf.end(), s, s + 4);
    }

    // Writes a name in a 20-byte field, cut to 19 characters so it's zero-terminated.
    void Name20(const std::string& s)
    {
        char n[20] = {};
        std::memcpy(n, s.data(), std::min<size_t>(s.size(), 19));
        buf.insert(buf.end(), n, n + 20);
    }

    // Starts a chunk; returns the offset of its size field.
    size_t Begin(const char* chunk_id)
    {
        Id(chunk_id);
        const size_t at = buf.size();
        U32(0);

        return at;
    }

    size_t BeginList(const char* type)
    {
        const size_t at = Begin("LIST");
        Id(type);

        return at;
    }

    void Patch32(size_t at, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
        {
            buf[at + i] = uint8_t(v >> (8 * i));
        }
    }

    // Ends the chunk whose size field is at `at`. Pads odd-length chunks without including the padding in their size.
    void End(size_t at)
    {
        const uint32_t size = uint32_t(buf.size() - at - 4);
        Patch32(at, size);
        if (size & 1)
        {
            U8(0);
        }
    }

    // Writes a chunk holding a zero-terminated string. SoundFont strings are padded to an even length inside the chunk.
    void Zstr(const char* chunk_id, const std::string& s)
    {
        const size_t at = Begin(chunk_id);
        buf.insert(buf.end(), s.begin(), s.end());
        U8(0);
        if ((s.size() + 1) & 1)
        {
            U8(0);
        }
        Patch32(at, uint32_t(buf.size() - at - 4));
    }

    std::vector<uint8_t> buf;
};

// Ranks a generator by its place in its zone: keyRange first, and the sample or instrument last, as the format
// requires.
int GenRank(uint16_t op)
{
    if (op == sf2gen::kKeyRange)
    {
        return 0;
    }
    if (op == sf2gen::kSampleId || op == sf2gen::kInstrument)
    {
        return 2;
    }

    return 1;
}

// Returns `gens` sorted by GenRank(), preserving the order of generators with equal rank.
std::vector<Sf2Gen> Ordered(std::vector<Sf2Gen> gens)
{
    std::stable_sort(gens.begin(), gens.end(),
                     [](const Sf2Gen& a, const Sf2Gen& b) { return GenRank(a.op) < GenRank(b.op); });

    return gens;
}

// Writes the INFO list: format version, sound engine, name, software and comment.
void WriteInfo(Writer& w, const std::string& name, const std::string& comment)
{
    const size_t info = w.BeginList("INFO");
    const size_t ifil = w.Begin("ifil");
    w.U16(2);
    w.U16(1);
    w.End(ifil);

    w.Zstr("isng", "EMU8000");
    w.Zstr("INAM", name);
    w.Zstr("ISFT", "konamidi");
    if (!comment.empty())
    {
        w.Zstr("ICMT", comment);
    }
    w.End(info);
}

// Writes the sdta list: the sample data, each sample followed by 46 zero points. Returns where each sample starts, in
// points.
std::vector<uint32_t> WriteSampleData(Writer& w, const std::vector<Sf2Sample>& samples)
{
    std::vector<uint32_t> starts;
    const size_t sdta = w.BeginList("sdta");
    const size_t smpl = w.Begin("smpl");
    uint32_t cursor = 0;
    for (const Sf2Sample& s : samples)
    {
        starts.push_back(cursor);
        for (int16_t v : s.pcm)
        {
            w.U16(uint16_t(v));
        }
        for (int i = 0; i < 46; i++)
        {
            w.U16(0);
        }
        cursor += uint32_t(s.pcm.size()) + 46;
    }
    w.End(smpl);
    w.End(sdta);

    return starts;
}

// Writes the presets, listed by bank and program, each with one zone naming its instrument.
void WritePresets(Writer& w, const std::vector<Sf2Preset>& presets)
{
    std::vector<const Sf2Preset*> order;
    for (const Sf2Preset& p : presets)
    {
        order.push_back(&p);
    }

    auto by_bank_then_program = [](const Sf2Preset* a, const Sf2Preset* b)
    {
        return a->bank != b->bank ? a->bank < b->bank : a->program < b->program;
    };
    std::stable_sort(order.begin(), order.end(), by_bank_then_program);

    size_t c = w.Begin("phdr");
    for (size_t i = 0; i < order.size(); i++)
    {
        w.Name20(order[i]->name);
        w.U16(order[i]->program);
        w.U16(order[i]->bank);
        w.U16(uint16_t(i));
        w.U32(0);
        w.U32(0);
        w.U32(0);
    }
    w.Name20("EOP");
    w.U16(0);
    w.U16(0);
    w.U16(uint16_t(order.size()));
    w.U32(0);
    w.U32(0);
    w.U32(0);
    w.End(c);

    c = w.Begin("pbag");
    for (size_t i = 0; i <= order.size(); i++)
    {
        w.U16(uint16_t(i)); // one generator per preset zone
        w.U16(0);
    }
    w.End(c);

    c = w.Begin("pmod");
    for (int i = 0; i < 10; i++)
    {
        w.U8(0);
    }
    w.End(c);

    c = w.Begin("pgen");
    for (const Sf2Preset* p : order)
    {
        w.U16(sf2gen::kInstrument);
        w.U16(uint16_t(p->instrument));
    }
    w.U32(0);
    w.End(c);
}

// Writes the instruments, with their zones and generators.
void WriteInstruments(Writer& w, const std::vector<Sf2Instrument>& instruments)
{
    size_t c = w.Begin("inst");
    uint16_t bag = 0;
    for (const Sf2Instrument& inst : instruments)
    {
        w.Name20(inst.name);
        w.U16(bag);
        bag = uint16_t(bag + inst.zones.size());
    }
    w.Name20("EOI");
    w.U16(bag);
    w.End(c);

    c = w.Begin("ibag");
    uint16_t gen = 0;
    for (const Sf2Instrument& inst : instruments)
    {
        for (const Sf2Zone& z : inst.zones)
        {
            w.U16(gen);
            w.U16(0);
            gen = uint16_t(gen + z.gens.size());
        }
    }
    w.U16(gen);
    w.U16(0);
    w.End(c);

    c = w.Begin("imod");
    for (int i = 0; i < 10; i++)
    {
        w.U8(0);
    }
    w.End(c);

    c = w.Begin("igen");
    for (const Sf2Instrument& inst : instruments)
    {
        for (const Sf2Zone& z : inst.zones)
        {
            for (const Sf2Gen& g : Ordered(z.gens))
            {
                w.U16(g.op);
                w.U16(g.amount);
            }
        }
    }
    w.U32(0);
    w.End(c);
}

// Writes the sample headers. `starts` gives where each sample's data starts, in points.
void WriteSampleHeaders(Writer& w, const std::vector<Sf2Sample>& samples, const std::vector<uint32_t>& starts)
{
    const size_t c = w.Begin("shdr");
    for (size_t i = 0; i < samples.size(); i++)
    {
        const Sf2Sample& s = samples[i];
        const uint32_t start = starts[i];
        const uint32_t end = start + uint32_t(s.pcm.size());
        w.Name20(s.name);
        w.U32(start);
        w.U32(end);
        if (s.loop)
        {
            w.U32(start + s.loop_start);
            w.U32(start + s.loop_end);
        }
        else
        {
            w.U32(start);
            w.U32(end);
        }
        w.U32(s.rate);
        w.U8(s.root_key);
        w.U8(0);  // pitch correction
        w.U16(0); // sample link
        w.U16(1); // mono
    }
    w.Name20("EOS");
    for (int i = 0; i < 26; i++)
    {
        w.U8(0);
    }
    w.End(c);
}

} // namespace

bool Sf2File::Write(const std::string& path, std::string& error) const
{
    // Zones and generators are numbered with 16 bits.
    size_t zones = 0, gens = 0;
    for (const Sf2Instrument& inst : instruments)
    {
        zones += inst.zones.size();
        for (const Sf2Zone& z : inst.zones)
        {
            gens += z.gens.size();
        }
    }
    if (zones > 0xFFFF || gens > 0xFFFF)
    {
        error = "soundfont too large (more than 65535 zones or generators)";
        return false;
    }

    Writer w;
    const size_t riff = w.Begin("RIFF");
    w.Id("sfbk");
    WriteInfo(w, name, comment);
    const std::vector<uint32_t> starts = WriteSampleData(w, samples);

    // The pdta list: presets, instruments and sample headers.
    const size_t pdta = w.BeginList("pdta");
    WritePresets(w, presets);
    WriteInstruments(w, instruments);
    WriteSampleHeaders(w, samples, starts);
    w.End(pdta);
    w.End(riff);

    return WriteFile(path, w.buf, error);
}

} // namespace konamidi
