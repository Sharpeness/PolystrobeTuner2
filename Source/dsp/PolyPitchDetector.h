#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include "TuningPresets.h"

class PolyPitchDetector
{
public:
    static constexpr std::size_t kMaxStrings = TuningPresets::kMaxStrings; // 8

    struct StringTrackResult
    {
        float cents  = 0.0f;
        float energy = 0.0f;
        bool  active = false;
    };

    PolyPitchDetector() noexcept = default;

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        // Децимация до ~22..24 кГц для высокой суб-сэмпловой точности пересечений нуля
        if (sampleRate >= 88200.0)      decimationFactor = 4;
        else if (sampleRate >= 44100.0) decimationFactor = 2;
        else                            decimationFactor = 1;

        decimatedFs           = static_cast<float> (fs / static_cast<double> (decimationFactor));
        decimationCounter     = 0;
        samplesSinceUpdate    = 0;
        updateIntervalSamples = static_cast<int> (decimatedFs * 0.020f); // шаг оценки ~20 мс (50 Гц)

        resetState();
        setupAntiAliasFilter (sampleRate);

        numConfiguredStrings = 0;
    }

    void resetState() noexcept
    {
        for (auto& ch : channels)
            ch.reset();
        for (auto& res : results)
            res = {};
        activeStringCount  = 0;
        polyModeLocked     = false;
        polyConfirmFrames  = 0;
        polyHoldFrames     = 0;
        soloOverrideFrames = 0;
        attackBlankFrames  = 0;
        inputSqSum         = 0.0f;
        inputSamplesCount  = 0;
        smoothInputRms     = 0.0f;
        aa_x1 = aa_x2 = aa_y1 = aa_y2 = 0.0f;
    }

    // Конфигурация целевых частот активных струн строго по пресету (Capo НЕ влияет на Polytune)
    void setTargetStrings (
        const ActiveStringsView& activeStrings,
        float a4Ref,
        float accuracyPercent) noexcept
    {
        const float clampedA4 = std::clamp (a4Ref, 400.0f, 480.0f);
        sensitivity           = std::clamp (accuracyPercent * 0.01f, 0.10f, 1.00f);

        bool needsFilterUpdate = (activeStrings.count != numConfiguredStrings) ||
                                 (std::abs (clampedA4 - lastA4Ref) > 0.02f);

        if (!needsFilterUpdate)
        {
            for (int i = 0; i < activeStrings.count; ++i)
            {
                const auto idx = static_cast<std::size_t> (i);
                if (activeStrings.midiNotes[idx] != lastMidiNotes[idx] ||
                    std::abs (activeStrings.centOffsets[idx] - lastCentOffsets[idx]) > 0.01f)
                {
                    needsFilterUpdate = true;
                    break;
                }
            }
        }

        if (!needsFilterUpdate)
            return;

        numConfiguredStrings = activeStrings.count;
        lastA4Ref            = clampedA4;

        for (int i = 0; i < numConfiguredStrings; ++i)
        {
            const auto idx       = static_cast<std::size_t> (i);
            lastMidiNotes[idx]   = activeStrings.midiNotes[idx];
            lastCentOffsets[idx] = activeStrings.centOffsets[idx];

            // В полифоническом режиме ищутся ноты строго так, как они прописаны в пресете (с учётом Sweetener)
            const float targetMidi = static_cast<float> (activeStrings.midiNotes[idx])
                                   + (activeStrings.centOffsets[idx] * 0.01f);
            const float targetHz   = clampedA4 * std::pow (2.0f, (targetMidi - 69.0f) / 12.0f);

            // Частота 2-й гармоники струны s-2 (если есть), которая попадает на +200 центов выше струны s
            // (например, 2-я гармоника 6-й струны E2/D#2 рядом с 4-й струной D3/C#3,
            //  и 2-я гармоника 5-й струны A2/G#2 рядом с 3-й струной G3/F#3).
            float notchFreqHz = targetHz * 2.0f;
            if (i >= 2)
            {
                const float low2Midi = static_cast<float> (activeStrings.midiNotes[static_cast<std::size_t> (i - 2)])
                                     + (activeStrings.centOffsets[static_cast<std::size_t> (i - 2)] * 0.01f);
                const float low2Harm2 = 2.0f * clampedA4 * std::pow (2.0f, (low2Midi - 69.0f) / 12.0f);
                if (low2Harm2 > targetHz * 1.07f && low2Harm2 < targetHz * 1.25f)
                    notchFreqHz = low2Harm2;
            }

            channels[idx].configure (targetHz, notchFreqHz, decimatedFs);
        }
    }

    void process (std::span<const float> samples, bool monoHasStrongSingleLock) noexcept
    {
        if (samples.empty() || numConfiguredStrings <= 0) [[unlikely]]
            return;

        const float* __restrict src = samples.data();
        const int numSamples        = static_cast<int> (samples.size());

        for (int i = 0; i < numSamples; ++i)
        {
            const float filtered = processAntiAlias (src[i]);

            if (++decimationCounter >= decimationFactor)
            {
                decimationCounter = 0;
                inputSqSum += filtered * filtered;
                ++inputSamplesCount;

                // Во время первых ~100 мс атаки медиатора фильтры продолжают обновлять состояния и энергию,
                // но нестабильные стартовые периоды не записываются в кольцевой буфер периодов
                const bool recordPeriods = (attackBlankFrames == 0);
                for (int s = 0; s < numConfiguredStrings; ++s)
                    channels[static_cast<std::size_t> (s)].processSample (filtered, recordPeriods);

                if (++samplesSinceUpdate >= updateIntervalSamples)
                {
                    samplesSinceUpdate = 0;
                    evaluateChannels (monoHasStrongSingleLock);
                }
            }
        }
    }

    [[nodiscard]] bool isPolyMode() const noexcept { return polyModeLocked; }
    [[nodiscard]] int getNumActiveStrings() const noexcept { return activeStringCount; }

    [[nodiscard]] const std::array<StringTrackResult, kMaxStrings>& getResults() const noexcept
    {
        return results;
    }

private:
    // Физическая компенсация негармоничности жёсткости гитарных струн (f_n = n * f_1 * sqrt(1 + B * n^2)):
    // Широкополосный соло-тюнер (MPM / PolyTune) оценивает высоту тона с весом обертонов n = 2..5,
    // тогда как узкополосный резонатор выделяет чистое основное колебание f_1, которое у витых струн
    // физически лежит на 1.5..3.5 цента ниже гармонического центра тяжести.
    [[nodiscard]] static constexpr float inharmOffsetCents (float freqHz) noexcept
    {
        if (freqHz < 90.0f)  return 3.45f; // 6-я струна (E2 / D#2 / D2) и басовые струны
        if (freqHz < 120.0f) return 2.80f; // 5-я струна (A2 / G#2)
        if (freqHz < 160.0f) return 2.95f; // 4-я струна (D3 / C#3)
        if (freqHz < 210.0f) return 1.65f; // 3-я струна (G3 / F#3)
        return 1.00f;                      // 2-я и 1-я струны без обмотки (B3 / E4)
    }

    struct ResonatorChannel
    {
        // Кольцевой буфер на 32 полупериода (~16 полных периодов) усредняет фазовые биения аккорда
        static constexpr std::size_t kRingSize = 32;

        float targetFreqHz   = 100.0f;
        float expectedPeriod = 240.0f;
        float minValidPeriod = 230.0f;
        float maxValidPeriod = 250.0f;
        float inharmCents    = 0.0f;

        // 0. Входной ФВЧ 1-го порядка (срезает нижние соседние струны и низкочастотный удар медиатора)
        float hp_coeff = 0.98f;
        float hp_x1 = 0.0f, hp_y1 = 0.0f;

        // 1. Заграждающий режекторный фильтр (Notch) на частоту главной помехи (+200 центов от 2-й гармоники струны s-2 или 2*f0)
        float nt_b0 = 1.0f, nt_b1 = 0.0f, nt_b2 = 1.0f, nt_a1 = 0.0f, nt_a2 = 0.0f;
        float nt_x1 = 0.0f, nt_x2 = 0.0f, nt_y1 = 0.0f, nt_y2 = 0.0f;

        // 2. Канальный ФНЧ Баттерворта (срез 1.26 * targetFreqHz) для подавления верхних струн и обертонов
        float lp_b0 = 0.0f, lp_b1 = 0.0f, lp_b2 = 0.0f, lp_a1 = 0.0f, lp_a2 = 0.0f;
        float lp_x1 = 0.0f, lp_x2 = 0.0f, lp_y1 = 0.0f, lp_y2 = 0.0f;

        // 3. Трёхкаскадный узкополосный резонатор (Q = 18.0) с точным размещением полюсов
        float bp_b0 = 0.0f, bp_b2 = 0.0f, bp_a1 = 0.0f, bp_a2 = 0.0f;
        float s1_x1 = 0.0f, s1_x2 = 0.0f, s1_y1 = 0.0f, s1_y2 = 0.0f;
        float s2_x1 = 0.0f, s2_x2 = 0.0f, s2_y1 = 0.0f, s2_y2 = 0.0f;
        float s3_x1 = 0.0f, s3_x2 = 0.0f, s3_y1 = 0.0f, s3_y2 = 0.0f;

        std::array<float, kRingSize> periodRing {};
        std::size_t ringWriteIdx = 0;
        std::size_t ringCount    = 0;
        int newPeriodsInWindow   = 0;

        float prevSample       = 0.0f;
        int   samplesSincePos  = 0;
        float prevPosFrac      = 0.0f;
        bool  hasPosCross      = false;

        int   samplesSinceNeg  = 0;
        float prevNegFrac      = 0.0f;
        bool  hasNegCross      = false;

        float energySum        = 0.0f;
        int   energySamples    = 0;

        float rawRmsSmooth     = 0.0f;
        float smoothedCents    = 0.0f;
        float smoothedEnergy   = 0.0f;
        int   holdFrames       = 0;
        int   lockAgeFrames    = 0;
        bool  isLocked         = false;
        bool  hasCentsInit     = false;

        void reset() noexcept
        {
            hp_x1 = hp_y1 = 0.0f;
            nt_x1 = nt_x2 = nt_y1 = nt_y2 = 0.0f;
            lp_x1 = lp_x2 = lp_y1 = lp_y2 = 0.0f;
            s1_x1 = s1_x2 = s1_y1 = s1_y2 = 0.0f;
            s2_x1 = s2_x2 = s2_y1 = s2_y2 = 0.0f;
            s3_x1 = s3_x2 = s3_y1 = s3_y2 = 0.0f;
            flushPeriods();
            prevSample         = 0.0f;
            samplesSincePos    = 0;
            prevPosFrac        = 0.0f;
            hasPosCross        = false;
            samplesSinceNeg    = 0;
            prevNegFrac        = 0.0f;
            hasNegCross        = false;
            energySum          = 0.0f;
            energySamples      = 0;
            rawRmsSmooth       = 0.0f;
            smoothedCents      = 0.0f;
            smoothedEnergy     = 0.0f;
            holdFrames         = 0;
            lockAgeFrames      = 0;
            isLocked           = false;
            hasCentsInit       = false;
        }

        void flushPeriods() noexcept
        {
            periodRing.fill (0.0f);
            ringWriteIdx       = 0;
            ringCount          = 0;
            newPeriodsInWindow = 0;
        }

        void configure (float freqHz, float notchHz, float sampleRateDec) noexcept
        {
            targetFreqHz   = std::clamp (freqHz, 20.0f, sampleRateDec * 0.42f);
            expectedPeriod = sampleRateDec / targetFreqHz;
            inharmCents    = inharmOffsetCents (targetFreqHz);

            // Допуск захвата периода струны +-55 центов
            minValidPeriod = expectedPeriod * 0.9685f;
            maxValidPeriod = expectedPeriod * 1.0325f;

            constexpr float twoPi = std::numbers::pi_v<float> * 2.0f;

            // 0. Входной ФВЧ на 0.78 * targetFreqHz (ослабляет соседнюю нижнюю струну на 0.749 * f0)
            const float hpCutoff = std::clamp (targetFreqHz * 0.78f, 18.0f, sampleRateDec * 0.35f);
            hp_coeff = std::exp (-twoPi * hpCutoff / sampleRateDec);

            // 1. Узкий режекторный фильтр (Notch, Q = 8.0) точно на частоту помехи (2-я гармоника струны s-2 или 2*f0)
            const float clampedNotch = std::clamp (notchHz, targetFreqHz * 1.08f, sampleRateDec * 0.44f);
            const float ntOmega      = twoPi * clampedNotch / sampleRateDec;
            const float ntSn         = std::sin (ntOmega);
            const float ntCs         = std::cos (ntOmega);
            const float ntAlpha      = ntSn / (2.0f * 8.0f);
            const float ntInvA0      = 1.0f / (1.0f + ntAlpha);

            nt_b0 = 1.0f * ntInvA0;
            nt_b1 = (-2.0f * ntCs) * ntInvA0;
            nt_b2 = 1.0f * ntInvA0;
            nt_a1 = nt_b1;
            nt_a2 = (1.0f - ntAlpha) * ntInvA0;

            // 2. Канальный ФНЧ Баттерворта на 1.26 * targetFreqHz (режет соседнюю верхнюю струну 1.335*f0 и гармоники)
            const float lpCutoff = std::clamp (targetFreqHz * 1.26f, 35.0f, sampleRateDec * 0.45f);
            const float lpOmega  = twoPi * lpCutoff / sampleRateDec;
            const float lpSn     = std::sin (lpOmega);
            const float lpCs     = std::cos (lpOmega);
            const float lpAlpha  = lpSn / std::numbers::sqrt2_v<float>;
            const float lpInvA0  = 1.0f / (1.0f + lpAlpha);

            lp_b0 = ((1.0f - lpCs) * 0.5f) * lpInvA0;
            lp_b1 = (1.0f - lpCs) * lpInvA0;
            lp_b2 = lp_b0;
            lp_a1 = (-2.0f * lpCs) * lpInvA0;
            lp_a2 = (1.0f - lpAlpha) * lpInvA0;

            // 3. Трёхкаскадный резонатор с полюсами на omega0 (Q = 18.0)
            constexpr float Q  = 18.0f;
            const float omega0 = twoPi * targetFreqHz / sampleRateDec;
            const float poleR  = std::exp (-omega0 / (2.0f * Q));
            const float poleR2 = poleR * poleR;

            bp_a1 = -2.0f * poleR * std::cos (omega0);
            bp_a2 = poleR2;
            bp_b0 = 0.5f * (1.0f - poleR2);
            bp_b2 = -bp_b0;

            reset();
        }

        void pushPeriod (float period) noexcept
        {
            if (period >= minValidPeriod && period <= maxValidPeriod)
            {
                periodRing[ringWriteIdx] = period;
                ringWriteIdx = (ringWriteIdx + 1) & (kRingSize - 1);
                if (ringCount < kRingSize)
                    ++ringCount;
                ++newPeriodsInWindow;
            }
        }

        void processSample (float x, bool recordPeriods) noexcept
        {
            // Ступень 0: ФВЧ отсечки нижних струн
            const float h0 = x - hp_x1 + hp_coeff * hp_y1;
            hp_x1 = x;
            hp_y1 = h0;

            // Ступень 1: Режекторный фильтр (Notch) подавления 2-й гармоники нижележащей струны (+200 центов)
            const float n0 = nt_b0 * h0 + nt_b1 * nt_x1 + nt_b2 * nt_x2 - nt_a1 * nt_y1 - nt_a2 * nt_y2;
            nt_x2 = nt_x1;
            nt_x1 = h0;
            nt_y2 = nt_y1;
            nt_y1 = n0;

            // Ступень 2: ФНЧ Баттерворта
            const float y0 = lp_b0 * n0 + lp_b1 * lp_x1 + lp_b2 * lp_x2 - lp_a1 * lp_y1 - lp_a2 * lp_y2;
            lp_x2 = lp_x1;
            lp_x1 = n0;
            lp_y2 = lp_y1;
            lp_y1 = y0;

            // Ступень 3: Полосовой резонатор 1
            const float y1 = bp_b0 * y0 + bp_b2 * s1_x2 - bp_a1 * s1_y1 - bp_a2 * s1_y2;
            s1_x2 = s1_x1;
            s1_x1 = y0;
            s1_y2 = s1_y1;
            s1_y1 = y1;

            // Ступень 4: Полосовой резонатор 2
            const float y2 = bp_b0 * y1 + bp_b2 * s2_x2 - bp_a1 * s2_y1 - bp_a2 * s2_y2;
            s2_x2 = s2_x1;
            s2_x1 = y1;
            s2_y2 = s2_y1;
            s2_y1 = y2;

            // Ступень 5: Полосовой резонатор 3 (высокая избирательность против межструнных биений)
            const float y3 = bp_b0 * y2 + bp_b2 * s3_x2 - bp_a1 * s3_y1 - bp_a2 * s3_y2;
            s3_x2 = s3_x1;
            s3_x1 = y2;
            s3_y2 = s3_y1;
            s3_y1 = y3;

            energySum += y3 * y3;
            ++energySamples;
            ++samplesSincePos;
            ++samplesSinceNeg;

            // 1. Пересечение нуля снизу вверх (целочисленный счётчик исключает потерю точности float)
            if (prevSample < 0.0f && y3 >= 0.0f)
            {
                const float denom = y3 - prevSample;
                const float frac  = (denom > 1e-12f) ? (-prevSample / denom) : 0.0f;
                if (hasPosCross && recordPeriods)
                {
                    const float period = static_cast<float> (samplesSincePos - 1) + frac + (1.0f - prevPosFrac);
                    pushPeriod (period);
                }
                samplesSincePos = 0;
                prevPosFrac     = frac;
                hasPosCross     = true;
            }
            // 2. Пересечение нуля сверху вниз
            else if (prevSample > 0.0f && y3 <= 0.0f)
            {
                const float denom = prevSample - y3;
                const float frac  = (denom > 1e-12f) ? (prevSample / denom) : 0.0f;
                if (hasNegCross && recordPeriods)
                {
                    const float period = static_cast<float> (samplesSinceNeg - 1) + frac + (1.0f - prevNegFrac);
                    pushPeriod (period);
                }
                samplesSinceNeg = 0;
                prevNegFrac     = frac;
                hasNegCross     = true;
            }

            prevSample = y3;
        }

        // Межквартильное усечённое среднее по кольцевому буферу из 32 периодов + оценка когерентности (IQR в центах)
        [[nodiscard]] float computeStablePeriod (float& outIqrSpreadCents) const noexcept
        {
            outIqrSpreadCents = 0.0f;
            if (ringCount == 0)
                return expectedPeriod;

            std::array<float, kRingSize> sorted {};
            for (std::size_t i = 0; i < ringCount; ++i)
                sorted[i] = periodRing[i];

            std::sort (sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t> (ringCount));

            if (ringCount >= 4)
            {
                const std::size_t q1  = ringCount / 4;
                const std::size_t q3  = ringCount - 1 - q1;
                const float pLow      = sorted[q1];
                const float pHigh     = sorted[q3];
                outIqrSpreadCents     = (pLow > 1.0f) ? (1200.0f * std::log2 (pHigh / pLow)) : 99.0f;

                float sum = 0.0f;
                std::size_t cnt = 0;
                for (std::size_t i = q1; i <= q3; ++i)
                {
                    sum += sorted[i];
                    ++cnt;
                }
                return (cnt > 0) ? (sum / static_cast<float> (cnt)) : sorted[ringCount / 2];
            }

            return sorted[ringCount / 2];
        }
    };

    std::array<ResonatorChannel, kMaxStrings>  channels {};
    std::array<StringTrackResult, kMaxStrings> results {};
    std::array<int, kMaxStrings>               lastMidiNotes {};
    std::array<float, kMaxStrings>             lastCentOffsets {};

    double fs                 = 48000.0;
    float decimatedFs         = 24000.0f;
    int decimationFactor      = 2;
    int decimationCounter     = 0;
    int samplesSinceUpdate    = 0;
    int updateIntervalSamples = 480;

    int numConfiguredStrings  = 0;
    float lastA4Ref           = 440.0f;
    float sensitivity         = 1.0f;

    int activeStringCount     = 0;
    bool polyModeLocked       = false;
    int polyConfirmFrames     = 0;
    int polyHoldFrames        = 0;
    int soloOverrideFrames    = 0;
    int attackBlankFrames     = 0;

    float inputSqSum          = 0.0f;
    int   inputSamplesCount   = 0;
    float smoothInputRms      = 0.0f;

    // Анти-алиасинговый фильтр Баттерворта перед децимацией
    float aa_b0 = 0.0f, aa_b1 = 0.0f, aa_b2 = 0.0f, aa_a1 = 0.0f, aa_a2 = 0.0f;
    float aa_x1 = 0.0f, aa_x2 = 0.0f, aa_y1 = 0.0f, aa_y2 = 0.0f;

    void setupAntiAliasFilter (double sampleRate) noexcept
    {
        constexpr float cutoff = 1600.0f;
        constexpr float twoPi  = std::numbers::pi_v<float> * 2.0f;
        const float omega      = twoPi * cutoff / static_cast<float> (sampleRate);
        const float sn         = std::sin (omega);
        const float cs         = std::cos (omega);
        const float alpha      = sn / std::numbers::sqrt2_v<float>;

        const float invA0 = 1.0f / (1.0f + alpha);
        aa_b0 = ((1.0f - cs) * 0.5f) * invA0;
        aa_b1 = (1.0f - cs) * invA0;
        aa_b2 = aa_b0;
        aa_a1 = (-2.0f * cs) * invA0;
        aa_a2 = (1.0f - alpha) * invA0;
    }

    [[nodiscard]] float processAntiAlias (float x) noexcept
    {
        const float y = aa_b0 * x + aa_b1 * aa_x1 + aa_b2 * aa_x2 - aa_a1 * aa_y1 - aa_a2 * aa_y2;
        aa_x2 = aa_x1;
        aa_x1 = x;
        aa_y2 = aa_y1;
        aa_y1 = y;
        return y;
    }

    void evaluateChannels (bool monoHasStrongSingleLock) noexcept
    {
        // 0. Детекция удара медиатором (при атаке приостанавливаем запись периодов на ~100 мс,
        //    НО НЕ стираем накопленный буфер периодов, чтобы строй не прыгал и не сбрасывался!)
        const float frameInputRms = (inputSamplesCount > 0)
                                        ? std::sqrt (inputSqSum / static_cast<float> (inputSamplesCount))
                                        : 0.0f;
        inputSqSum        = 0.0f;
        inputSamplesCount = 0;

        const bool isPickAttack = (frameInputRms > smoothInputRms * 1.55f) && (frameInputRms > 0.0012f);
        smoothInputRms          = smoothInputRms * 0.72f + frameInputRms * 0.28f;

        if (isPickAttack)
        {
            attackBlankFrames = 5; // ~100 мс блокировки записи переходного процесса щипка
        }
        else if (attackBlankFrames > 0)
        {
            --attackBlankFrames;
        }

        float maxChannelRms = 0.0f;
        std::array<float, kMaxStrings> rawRms {};
        std::array<float, kMaxStrings> chRms {};

        const float denomStrings = static_cast<float> (std::max (1, numConfiguredStrings - 1));
        for (int s = 0; s < numConfiguredStrings; ++s)
        {
            const auto idx = static_cast<std::size_t> (s);
            auto& ch       = channels[idx];
            const float r  = (ch.energySamples > 0)
                                 ? std::sqrt (ch.energySum / static_cast<float> (ch.energySamples))
                                 : 0.0f;

            // Сохраняем неискажённую (без подъёма ВЧ) энергию для точного распознавания обертонов 6-й и 5-й струн
            if (r > ch.rawRmsSmooth)
                ch.rawRmsSmooth = ch.rawRmsSmooth * 0.30f + r * 0.70f;
            else
                ch.rawRmsSmooth = ch.rawRmsSmooth * 0.86f + r * 0.14f;
            rawRms[idx] = ch.rawRmsSmooth;

            // Выравнивание чувствительности тонких струн для отображения
            const float highStringGain = 1.0f + (static_cast<float> (s) / denomStrings) * 0.85f;
            const float boosted        = r * highStringGain;

            if (boosted > ch.smoothedEnergy)
                ch.smoothedEnergy = ch.smoothedEnergy * 0.30f + boosted * 0.70f;
            else
                ch.smoothedEnergy = ch.smoothedEnergy * 0.86f + boosted * 0.14f;

            chRms[idx]    = ch.smoothedEnergy;
            maxChannelRms = std::max (maxChannelRms, ch.smoothedEnergy);

            ch.energySum     = 0.0f;
            ch.energySamples = 0;
        }

        const float absFloor          = 0.000045f + (1.0f - sensitivity) * 0.000030f;
        const float displayGateThresh = std::max (absFloor, maxChannelRms * 0.045f);
        const float curve             = sensitivity * sensitivity;

        // 1. Определяем, какие верхние каналы возбуждены обертонами (2*f0, 3*f0, 4*f0) звучащей нижней струны.
        //    КРИТИЧНО: сравниваем неискажённую энергию rawRms[low] с rawRms[s]*0.08f, а НЕ с maxChannelRms!
        //    У электрогитары при щипке 6-й струны (D#2 = 77.8 Гц) её 3-я гармоника (233.3 Гц = 2-я струна A#3)
        //    и 4-я гармоника (311.1 Гц = 1-я струна D#4) имеют амплитуду в 2..2.5 раза ВЫШЕ основного тона 6-й струны!
        std::array<bool, kMaxStrings> isHarmonicOfLower {};
        float maxNonHarmonicRms    = 0.0f;
        float secondNonHarmonicRms = 0.0f;

        for (int s = 0; s < numConfiguredStrings; ++s)
        {
            const auto idx = static_cast<std::size_t> (s);
            const auto& ch = channels[idx];

            bool harmonicFlag = false;
            for (int low = 0; low < s; ++low)
            {
                const auto lowIdx = static_cast<std::size_t> (low);
                // Если нижняя струна реально звучит (выше порога тишины и >= 8% от энергии верхней гармоники)
                if (rawRms[lowIdx] < absFloor * 3.0f || rawRms[lowIdx] < rawRms[idx] * 0.08f)
                    continue;

                const float ratio = ch.targetFreqHz / channels[lowIdx].targetFreqHz;
                const float near2 = std::abs (ratio - 2.0f);
                const float near3 = std::abs (ratio - 3.0f);
                const float near4 = std::abs (ratio - 4.0f);

                if (near2 < 0.035f || near3 < 0.035f || near4 < 0.035f)
                {
                    harmonicFlag = true;
                    break;
                }
            }
            isHarmonicOfLower[idx] = harmonicFlag;

            if (!harmonicFlag)
            {
                if (chRms[idx] > maxNonHarmonicRms)
                {
                    secondNonHarmonicRms = maxNonHarmonicRms;
                    maxNonHarmonicRms    = chRms[idx];
                }
                else if (chRms[idx] > secondNonHarmonicRms)
                {
                    secondNonHarmonicRms = chRms[idx];
                }
            }
        }

        // 2. Считаем число независимых (негармонических) струн с устойчивыми периодами
        const float nonHarmTriggerThresh = std::max (absFloor * 3.5f, maxNonHarmonicRms * 0.24f);
        int nonHarmonicActiveCount = 0;

        for (int s = 0; s < numConfiguredStrings; ++s)
        {
            const auto idx = static_cast<std::size_t> (s);
            const auto& ch = channels[idx];
            const bool hasCycles = (ch.newPeriodsInWindow >= 1) || (ch.ringCount >= 4);
            if (!isHarmonicOfLower[idx] && chRms[idx] >= nonHarmTriggerThresh && hasCycles)
                ++nonHarmonicActiveCount;
        }

        // 3. Стабильная логика переключения Solo <-> Polytune (как в TC Electronic PolyTune):
        //    - Для входа в Polytune при чесе по струнам требуется минимум 3 независимых негармонических струны
        //      (или 2 для 4-струнного баса), преодоление порога уровня (0.0012) и 5 кадров (~100 мс) подтверждения.
        //    - Внутри Polytune режим надёжно удерживается (polyHoldFrames = 65, ~1.3 с) и продлевается,
        //      пока звучат хотя бы 2 струны, не перескакивая туда-обратно.
        const int minChordStrings = (numConfiguredStrings <= 4) ? 2 : 3;
        const bool singleStringDominant = (maxNonHarmonicRms > absFloor * 3.0f) &&
                                          (secondNonHarmonicRms < maxNonHarmonicRms * 0.18f) &&
                                          (nonHarmonicActiveCount <= 1);

        if (!polyModeLocked)
        {
            soloOverrideFrames = 0;
            if (!singleStringDominant &&
                nonHarmonicActiveCount >= minChordStrings &&
                maxNonHarmonicRms >= 0.0012f &&
                attackBlankFrames == 0)
            {
                ++polyConfirmFrames;
                if (polyConfirmFrames >= 5)
                {
                    polyModeLocked = true;
                    polyHoldFrames = 65; // ~1.3 с стабильного удержания экрана Polytune
                }
            }
            else
            {
                polyConfirmFrames = 0;
            }
        }
        else
        {
            // Уже в режиме Polytune: поддерживаем удержание, пока звучат >= 2 независимые струны
            if (nonHarmonicActiveCount >= 2 && maxNonHarmonicRms >= absFloor * 2.5f)
            {
                polyHoldFrames     = std::max (polyHoldFrames, 55); // продлеваем ещё на ~1.1 с
                soloOverrideFrames = 0;
            }
            else if (singleStringDominant && monoHasStrongSingleLock && attackBlankFrames == 0)
            {
                // Если музыкант приглушил аккорд и отчётливо дёрнул одну струну, выходим в Solo через ~120 мс
                if (++soloOverrideFrames >= 6)
                {
                    polyModeLocked     = false;
                    polyHoldFrames     = 0;
                    polyConfirmFrames  = 0;
                    soloOverrideFrames = 0;
                }
            }
            else
            {
                soloOverrideFrames = 0;
                if (polyHoldFrames > 0)
                    --polyHoldFrames;
                else
                    polyModeLocked = false;
            }
        }

        // 4. Обновляем состояние и сглаженные центы для каждой струны
        int currentlyLocked = 0;
        for (int s = 0; s < numConfiguredStrings; ++s)
        {
            const auto idx = static_cast<std::size_t> (s);
            auto& ch       = channels[idx];

            const float activeThresh = polyModeLocked
                                           ? std::max (absFloor, maxChannelRms * 0.030f)
                                           : displayGateThresh;
            const bool hasRecentCycles = (ch.newPeriodsInWindow >= 1) ||
                                         (ch.ringCount >= 4 && chRms[idx] >= activeThresh * 1.2f);
            const bool suppressHarmonicGhost = (!polyModeLocked) && isHarmonicOfLower[idx];

            if (!suppressHarmonicGhost && chRms[idx] >= activeThresh && hasRecentCycles && ch.ringCount >= 4)
            {
                float iqrSpreadCents = 0.0f;
                const float stablePeriod = ch.computeStablePeriod (iqrSpreadCents);

                // Обновляем оценку высоты тона только когда периоды когерентны (IQR < 8 центов)
                // и не идёт стартовый щелчок медиатора
                if (attackBlankFrames == 0 && iqrSpreadCents < 8.0f)
                {
                    const float measuredHz = decimatedFs / stablePeriod;
                    const float rawCents   = 1200.0f * std::log2 (measuredHz / ch.targetFreqHz) + ch.inharmCents;
                    const float clampedC   = std::clamp (rawCents, -50.0f, 50.0f);

                    if (!ch.hasCentsInit)
                    {
                        ch.smoothedCents = clampedC;
                        ch.hasCentsInit  = true;
                        ch.lockAgeFrames = 1;
                    }
                    else
                    {
                        ++ch.lockAgeFrames;
                        // Плавная фильтрация: чем чище когерентность периодов, тем выше доверие
                        const float coherenceWeight = (iqrSpreadCents < 3.5f) ? 1.0f : 0.45f;
                        if (ch.lockAgeFrames <= 6)
                        {
                            ch.smoothedCents += (clampedC - ch.smoothedCents) * (0.35f * coherenceWeight);
                        }
                        else
                        {
                            const float centsDiff = std::abs (clampedC - ch.smoothedCents);
                            const float minW  = (centsDiff < 1.5f) ? 0.025f : ((centsDiff < 4.0f) ? 0.05f : 0.10f);
                            const float maxW  = (centsDiff < 1.5f) ? 0.12f  : ((centsDiff < 4.0f) ? 0.22f : 0.35f);
                            const float alpha = (minW + curve * (maxW - minW)) * coherenceWeight;

                            ch.smoothedCents += (clampedC - ch.smoothedCents) * alpha;
                        }
                    }
                }

                if (ch.hasCentsInit)
                {
                    ch.isLocked   = true;
                    ch.holdFrames = 45; // ~900 мс плавного удержания зажжённого столбца струны
                }
            }
            else if (ch.holdFrames > 0 && polyModeLocked)
            {
                --ch.holdFrames;
                if (ch.holdFrames == 0)
                    ch.isLocked = false;
            }
            else
            {
                ch.isLocked      = false;
                ch.holdFrames    = 0;
                ch.lockAgeFrames = 0;
                if (chRms[idx] < absFloor * 0.4f)
                {
                    ch.ringCount    = 0;
                    ch.hasCentsInit = false;
                }
            }

            ch.newPeriodsInWindow = 0;

            results[idx].cents  = ch.smoothedCents;
            results[idx].energy = ch.smoothedEnergy;
            results[idx].active = ch.isLocked;

            if (ch.isLocked)
                ++currentlyLocked;
        }

        for (int s = numConfiguredStrings; s < static_cast<int> (kMaxStrings); ++s)
            results[static_cast<std::size_t> (s)] = {};

        activeStringCount = currentlyLocked;
    }
};
