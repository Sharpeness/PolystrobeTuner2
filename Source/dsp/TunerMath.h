#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <numbers>
#include <string_view>

enum class TunerError : std::uint8_t
{
    FrequencyOutOfRange,
    InvalidReferencePitch
};

struct TunerResult
{
    char note = ' ';
    bool isSharp = false;
    int midiNote = 69;
    int octave = 4;
    float cents = 0.0f;
    bool isValid = false;
};

class TunerMath
{
public:
    static constexpr float kMinFrequencyHz = 20.0f;
    static constexpr float kMaxFrequencyHz = 4500.0f;
    static constexpr float kMinRefPitchHz  = 400.0f;
    static constexpr float kMaxRefPitchHz  = 480.0f;

    static constexpr std::array<std::string_view, 12> kNoteNames = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    [[nodiscard]] static std::expected<TunerResult, TunerError> frequencyToNote (
        float freq,
        float a4Ref = 440.0f,
        float sweetenerCentsOffset = 0.0f,
        int capoSemitones = 0) noexcept
    {
        if (a4Ref < kMinRefPitchHz || a4Ref > kMaxRefPitchHz) [[unlikely]]
            return std::unexpected (TunerError::InvalidReferencePitch);

        if (freq < kMinFrequencyHz || freq > kMaxFrequencyHz) [[unlikely]]
            return std::unexpected (TunerError::FrequencyOutOfRange);

        // Реальный акустический MIDI номер (A4 = 440 Hz = 69)
        const float semitone = 12.0f * std::log2 (freq / a4Ref) + 69.0f;
        const int nearestMidi = static_cast<int> (std::lround (semitone));

        TunerResult res;
        // Отклонение считается строго от реальной частоты ноты с учётом поправки Sweetener
        res.cents = (semitone - static_cast<float> (nearestMidi)) * 100.0f - sweetenerCentsOffset;

        // Визуальный сдвиг ноты на экране: при Capo = -2 нота D (50) отображается как E (50 - (-2) = 52)
        res.midiNote = nearestMidi - capoSemitones;
        res.octave   = std::clamp ((res.midiNote / 12) - 1, 0, 8);
        res.isValid  = true;

        const int noteIndex = ((res.midiNote % 12) + 12) % 12;
        const std::string_view name = kNoteNames[static_cast<std::size_t> (noteIndex)];

        res.note    = name.front();
        res.isSharp = (name.size() > 1 && name[1] == '#');

        return res;
    }
};