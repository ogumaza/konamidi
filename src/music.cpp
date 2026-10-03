// SPDX-License-Identifier: MIT

#include "music.h"

#include "program.h"

namespace supergbamidi
{

std::unique_ptr<Music> OpenMusic(const Rom& rom, const Overrides& overrides, std::string& error)
{
    error.clear();

    // Check for Rare's driver first: its detection uses code alone. Konami's detection can fall back on a data scan,
    // which might mistake another driver's data for a song table.
    if (overrides.driver != Driver::kKonami)
    {
        std::unique_ptr<Music> music = rare::OpenMusic(rom, overrides, error);
        if (music || !error.empty())
        {
            return music;
        }
    }

    if (overrides.driver != Driver::kRare)
    {
        std::unique_ptr<Music> music = konami::OpenMusic(rom, overrides, error);
        if (music || !error.empty())
        {
            return music;
        }
    }

    // Neither driver was detected.
    const std::string unknown =
        std::string(": this game's music uses another engine, or a driver version ") + kProgramName + " doesn't know";
    switch (overrides.driver)
    {
    case Driver::kKonami:
        error = "no Konami sound driver found" + unknown;
        break;
    case Driver::kRare:
        error = "no Rare sound driver found (try --song-table)";
        break;
    default:
        error = "no Konami or Rare sound driver found" + unknown;
        break;
    }

    return nullptr;
}

} // namespace supergbamidi
