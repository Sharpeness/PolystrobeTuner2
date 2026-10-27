#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

struct TuningPreset
{
    int id = 0;
    std::string_view name {};
    std::string_view category {};
    // Для гитары используются 8 элементов: [0]=8-я .. [7]=1-я струна
    // Для баса используются первые 6 элементов: [0]=5-я(B0), [1]=4-я(E1), [2]=3-я(A1), [3]=2-я(D2), [4]=1-я(G2), [5]=верхняя(C3)
    std::array<int, 8> midiNotes {};
    std::array<float, 8> centOffsets {};
    bool defaultFavorite = false;
};

struct ActiveStringsView
{
    std::array<int, 8> midiNotes {};
    std::array<float, 8> centOffsets {};
    int count = 0;
};

namespace TuningPresetsData
{
    inline constexpr std::size_t kMaxStrings       = 8;
    inline constexpr std::size_t kNumGuitarPresets = 27;
    inline constexpr std::size_t kNumBassPresets   = 14;

    inline constexpr std::array<std::string_view, 12> kNoteNames = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    [[nodiscard]] constexpr TuningPreset makeShifted (
        int id,
        std::string_view name,
        std::string_view category,
        const std::array<int, 8>& baseNotes,
        int shiftSemitones,
        const std::array<float, 8>& offsets,
        bool fav) noexcept
    {
        TuningPreset tp {};
        tp.id              = id;
        tp.name            = name;
        tp.category        = category;
        tp.centOffsets     = offsets;
        tp.defaultFavorite = fav;
        for (std::size_t i = 0; i < 8; ++i)
            tp.midiNotes[i] = baseNotes[i] + shiftSemitones;
        return tp;
    }

    inline constexpr std::array<int, 8>   kBaseStd       = { 30, 35, 40, 45, 50, 55, 59, 64 };
    inline constexpr std::array<int, 8>   kBaseDropD     = { 28, 33, 38, 45, 50, 55, 59, 64 };
    inline constexpr std::array<float, 8> kZeroOffsets   = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    inline constexpr std::array<float, 8> kGtrSweetener  = { -2.3f, -2.1f, -2.3f, 0.0f, 0.0f, -0.4f, -2.1f, -2.3f };
    inline constexpr std::array<float, 8> kEvhSweetener  = { 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -3.0f, -9.0f, -9.0f };
    inline constexpr std::array<float, 8> kJtSweetener   = { -14.0f, -13.0f, -12.0f, -10.0f, -8.0f, -4.0f, -6.0f, -3.0f };
    inline constexpr std::array<float, 8> kBfeSweetener  = { -2.0f, -2.0f, -2.0f, -2.0f, -2.0f, -2.0f, +1.0f, 0.0f };

    inline constexpr std::array<int, 8>   kBaseBass      = { 23, 28, 33, 38, 43, 48, 0, 0 };
    inline constexpr std::array<int, 8>   kBaseBassDrop  = { 21, 26, 33, 38, 43, 48, 0, 0 };
    inline constexpr std::array<float, 8> kBassSweetener = { -2.0f, -1.5f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    inline constexpr std::array<float, 8> kBfeBass       = { -2.0f, -2.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    inline constexpr std::array<TuningPreset, kNumGuitarPresets> kGuitarPresets = {
        makeShifted (0,  "Standard",                "STANDARD & LOWERED",     kBaseStd,    0, kZeroOffsets,  true),
        makeShifted (1,  "- 1/2 step down",         "STANDARD & LOWERED",     kBaseStd,   -1, kZeroOffsets,  true),
        makeShifted (2,  "- 1 step down",           "STANDARD & LOWERED",     kBaseStd,   -2, kZeroOffsets,  true),
        makeShifted (3,  "- 1 1/2 steps down",      "STANDARD & LOWERED",     kBaseStd,   -3, kZeroOffsets,  false),
        makeShifted (4,  "- 2 steps down",          "STANDARD & LOWERED",     kBaseStd,   -4, kZeroOffsets,  false),
        makeShifted (5,  "- 2 1/2 steps down",      "STANDARD & LOWERED",     kBaseStd,   -5, kZeroOffsets,  false),

        makeShifted (6,  "Drop D",                  "DROP TUNINGS",           kBaseDropD,  0, kZeroOffsets,  true),
        makeShifted (7,  "Drop C#",                 "DROP TUNINGS",           kBaseDropD, -1, kZeroOffsets,  false),
        makeShifted (8,  "Drop C",                  "DROP TUNINGS",           kBaseDropD, -2, kZeroOffsets,  true),

        makeShifted (9,  "GTR Sweetener (E)",       "PETERSON GTR SWEETENER", kBaseStd,    0, kGtrSweetener, true),
        makeShifted (10, "GTR Sweetener (D#)",      "PETERSON GTR SWEETENER", kBaseStd,   -1, kGtrSweetener, false),
        makeShifted (11, "GTR Sweetener (D)",       "PETERSON GTR SWEETENER", kBaseStd,   -2, kGtrSweetener, false),
        makeShifted (12, "GTR Sweetener (C#)",      "PETERSON GTR SWEETENER", kBaseStd,   -3, kGtrSweetener, false),
        makeShifted (13, "GTR Sweetener (C)",       "PETERSON GTR SWEETENER", kBaseStd,   -4, kGtrSweetener, false),
        makeShifted (14, "GTR Sweetener (B)",       "PETERSON GTR SWEETENER", kBaseStd,   -5, kGtrSweetener, false),

        makeShifted (15, "EVH Tuning (E)",          "EDDIE VAN HALEN (EVH)",  kBaseStd,    0, kEvhSweetener, true),
        makeShifted (16, "EVH Tuning (D#)",         "EDDIE VAN HALEN (EVH)",  kBaseStd,   -1, kEvhSweetener, false),
        makeShifted (17, "EVH Tuning (D)",          "EDDIE VAN HALEN (EVH)",  kBaseStd,   -2, kEvhSweetener, false),
        makeShifted (18, "EVH Tuning (C#)",         "EDDIE VAN HALEN (EVH)",  kBaseStd,   -3, kEvhSweetener, false),
        makeShifted (19, "EVH Tuning (C)",          "EDDIE VAN HALEN (EVH)",  kBaseStd,   -4, kEvhSweetener, false),
        makeShifted (20, "EVH Tuning (B)",          "EDDIE VAN HALEN (EVH)",  kBaseStd,   -5, kEvhSweetener, false),

        makeShifted (21, "James Taylor / ACU (E)",  "JAMES TAYLOR / ACU",     kBaseStd,    0, kJtSweetener,  true),
        makeShifted (22, "James Taylor / ACU (D#)", "JAMES TAYLOR / ACU",     kBaseStd,   -1, kJtSweetener,  false),
        makeShifted (23, "James Taylor / ACU (D)",  "JAMES TAYLOR / ACU",     kBaseStd,   -2, kJtSweetener,  false),

        makeShifted (24, "Buzz Feiten (E)",         "BUZZ FEITEN (BFE)",      kBaseStd,    0, kBfeSweetener, false),
        makeShifted (25, "Buzz Feiten (D#)",        "BUZZ FEITEN (BFE)",      kBaseStd,   -1, kBfeSweetener, false),
        makeShifted (26, "Buzz Feiten (D)",         "BUZZ FEITEN (BFE)",      kBaseStd,   -2, kBfeSweetener, false)
    };

    inline constexpr std::array<TuningPreset, kNumBassPresets> kBassPresets = {
        makeShifted (0,  "Bass Standard",           "BASS STANDARD & LOWERED", kBaseBass,     0, kZeroOffsets,   true),
        makeShifted (1,  "Bass - 1/2 step down",    "BASS STANDARD & LOWERED", kBaseBass,    -1, kZeroOffsets,   true),
        makeShifted (2,  "Bass - 1 step down",      "BASS STANDARD & LOWERED", kBaseBass,    -2, kZeroOffsets,   true),
        makeShifted (3,  "Bass - 1 1/2 steps",      "BASS STANDARD & LOWERED", kBaseBass,    -3, kZeroOffsets,   false),
        makeShifted (4,  "Bass - 2 steps down",     "BASS STANDARD & LOWERED", kBaseBass,    -4, kZeroOffsets,   false),

        makeShifted (5,  "Bass Drop D / A",         "BASS DROP TUNINGS",       kBaseBassDrop, 0, kZeroOffsets,   true),
        makeShifted (6,  "Bass Drop C# / G#",       "BASS DROP TUNINGS",       kBaseBassDrop,-1, kZeroOffsets,   false),
        makeShifted (7,  "Bass Drop C / G",         "BASS DROP TUNINGS",       kBaseBassDrop,-2, kZeroOffsets,   true),

        makeShifted (8,  "Bass Sweetener (E)",      "PETERSON BASS SWEETENER", kBaseBass,     0, kBassSweetener, true),
        makeShifted (9,  "Bass Sweetener (D#)",     "PETERSON BASS SWEETENER", kBaseBass,    -1, kBassSweetener, false),
        makeShifted (10, "Bass Sweetener (D)",      "PETERSON BASS SWEETENER", kBaseBass,    -2, kBassSweetener, false),

        makeShifted (11, "Bass Buzz Feiten (E)",    "BASS BUZZ FEITEN",        kBaseBass,     0, kBfeBass,       false),
        makeShifted (12, "Bass Buzz Feiten (D#)",   "BASS BUZZ FEITEN",        kBaseBass,    -1, kBfeBass,       false),
        makeShifted (13, "Bass Buzz Feiten (D)",    "BASS BUZZ FEITEN",        kBaseBass,    -2, kBfeBass,       false)
    };
} // namespace TuningPresetsData

class TuningPresets
{
public:
    static constexpr std::size_t kMaxStrings       = TuningPresetsData::kMaxStrings;
    static constexpr std::size_t kNumGuitarPresets = TuningPresetsData::kNumGuitarPresets;
    static constexpr std::size_t kNumBassPresets   = TuningPresetsData::kNumBassPresets;

    [[nodiscard]] static constexpr std::span<const TuningPreset> getPresets (bool isBass) noexcept
    {
        if (isBass)
            return std::span<const TuningPreset> (TuningPresetsData::kBassPresets);
        return std::span<const TuningPreset> (TuningPresetsData::kGuitarPresets);
    }

    [[nodiscard]] static constexpr std::span<const TuningPreset> getAllPresets() noexcept
    {
        return getPresets (false);
    }

    [[nodiscard]] static constexpr std::string_view midiToNoteNameView (int midiNote) noexcept
    {
        const int idx = ((midiNote % 12) + 12) % 12;
        return TuningPresetsData::kNoteNames[static_cast<std::size_t> (idx)];
    }

    [[nodiscard]] static juce::String midiToNoteName (int midiNote)
    {
        const auto sv = midiToNoteNameView (midiNote);
        return juce::String::fromUTF8 (sv.data(), static_cast<int> (sv.size()));
    }

    [[nodiscard]] static juce::String toJuceString (std::string_view sv)
    {
        return juce::String::fromUTF8 (sv.data(), static_cast<int> (sv.size()));
    }

    // Возвращает структуру активных струн (слева направо: от самой толстой к самой тонкой) — без аллокаций, constexpr
    [[nodiscard]] static constexpr ActiveStringsView getActiveStrings (
        const TuningPreset& p,
        bool isBass,
        int numStrings) noexcept
    {
        ActiveStringsView view {};
        if (!isBass)
        {
            view.count = std::clamp (numStrings, 6, 8);
            const int startIdx = 8 - view.count;
            for (int i = 0; i < view.count; ++i)
            {
                const auto srcIdx = static_cast<std::size_t> (startIdx + i);
                view.midiNotes[static_cast<std::size_t> (i)]   = p.midiNotes[srcIdx];
                view.centOffsets[static_cast<std::size_t> (i)] = p.centOffsets[srcIdx];
            }
        }
        else
        {
            view.count = std::clamp (numStrings, 4, 6);
            // Для 4 струн берем индексы [1..4] (E-A-D-G), для 5 струн [0..4] (B-E-A-D-G), для 6 струн [0..5] (B-E-A-D-G-C)
            const int startIdx = (view.count == 4) ? 1 : 0;
            for (int i = 0; i < view.count; ++i)
            {
                const auto srcIdx = static_cast<std::size_t> (startIdx + i);
                view.midiNotes[static_cast<std::size_t> (i)]   = p.midiNotes[srcIdx];
                view.centOffsets[static_cast<std::size_t> (i)] = p.centOffsets[srcIdx];
            }
        }
        return view;
    }

    // Совместимая перегрузка для существующих вызовов UI
    static constexpr void getActiveStringNotes (
        const TuningPreset& p,
        bool isBass,
        int numStrings,
        int outMidi[8],
        float outOffsets[8],
        int& outCount) noexcept
    {
        const auto view = getActiveStrings (p, isBass, numStrings);
        outCount = view.count;
        for (int i = 0; i < view.count; ++i)
        {
            outMidi[i]    = view.midiNotes[static_cast<std::size_t> (i)];
            outOffsets[i] = view.centOffsets[static_cast<std::size_t> (i)];
        }
    }

    [[nodiscard]] static constexpr float getSweetenerOffsetForMidi (
        bool isBass,
        int presetIdx,
        int numStrings,
        int detectedMidi) noexcept
    {
        const auto all = getPresets (isBass);
        if (presetIdx < 0 || static_cast<std::size_t> (presetIdx) >= all.size()) [[unlikely]]
            return 0.0f;

        const auto active = getActiveStrings (all[static_cast<std::size_t> (presetIdx)], isBass, numStrings);

        int bestString  = -1;
        int minMidiDist = 999;
        const int detectedPitchClass = ((detectedMidi % 12) + 12) % 12;

        for (int i = 0; i < active.count; ++i)
        {
            const int targetMidi = active.midiNotes[static_cast<std::size_t> (i)];
            const int targetPitchClass = ((targetMidi % 12) + 12) % 12;
            if (targetPitchClass == detectedPitchClass)
            {
                const int dist = (detectedMidi >= targetMidi) ? (detectedMidi - targetMidi) : (targetMidi - detectedMidi);
                if (dist < minMidiDist)
                {
                    minMidiDist = dist;
                    bestString  = i;
                }
            }
        }

        return (bestString >= 0) ? active.centOffsets[static_cast<std::size_t> (bestString)] : 0.0f;
    }

    [[nodiscard]] static constexpr std::uint32_t getDefaultFavoritesMask (bool isBass) noexcept
    {
        const auto presets = getPresets (isBass);
        std::uint32_t mask = 0;
        for (std::size_t i = 0; i < presets.size(); ++i)
        {
            if (presets[i].defaultFavorite)
                mask |= (1u << static_cast<std::uint32_t> (i));
        }
        return mask;
    }
};