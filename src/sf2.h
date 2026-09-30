// SPDX-License-Identifier: MIT

// SoundFont 2.01 writer.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace konamidi
{

// The SF2 generator operators konamidi uses.
namespace sf2gen
{

enum : uint16_t
{
    kInstrument = 41,
    kKeyRange = 43,
    kSampleId = 53,
    kSampleModes = 54,
    kOverridingRootKey = 58,
};

} // namespace sf2gen

struct Sf2Sample
{
    std::string name;
    std::vector<int16_t> pcm;
    uint32_t rate = 22050;
    uint8_t root_key = 60;
    bool loop = false;
    uint32_t loop_start = 0; // relative to the sample start
    uint32_t loop_end = 0;   // exclusive
};

struct Sf2Gen
{
    // Returns a generator that sets a range, such as a key range.
    static Sf2Gen Range(uint16_t op, int lo, int hi)
    {
        return {op, uint16_t((lo & 0xFF) | ((hi & 0xFF) << 8))};
    }

    // Returns a generator that sets a signed amount.
    static Sf2Gen Value(uint16_t op, int v)
    {
        return {op, uint16_t(int16_t(v))};
    }

    uint16_t op;
    uint16_t amount;
};

// A zone: a list of generators. keyRange must come first and the sample or instrument reference last, which
// Sf2File::Write() enforces.
struct Sf2Zone
{
    std::vector<Sf2Gen> gens;
};

struct Sf2Instrument
{
    std::string name;
    std::vector<Sf2Zone> zones;
};

struct Sf2Preset
{
    std::string name;
    uint16_t bank = 0;
    uint16_t program = 0;
    int instrument = 0; // index into Sf2File::instruments
};

struct Sf2File
{
    // Writes the file. Returns false and sets `error` if it's too big for the format or can't be written.
    bool Write(const std::string& path, std::string& error) const;

    std::string name = "Untitled";
    std::string comment;
    std::vector<Sf2Sample> samples;
    std::vector<Sf2Instrument> instruments;
    std::vector<Sf2Preset> presets;
};

} // namespace konamidi
