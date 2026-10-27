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

        std::array<float, kMaxStrings> baseFreqs {};
        for (int i = 0; i < numConfiguredStrings; ++i)
        {
            const auto idx       = static_cast<std::size_t> (i);
            lastMidiNotes[idx]   = activeStrings.midiNotes[idx];
            lastCentOffsets[idx] = activeStrings.centOffsets[idx];

            // В полифоническом режиме ищутся ноты строго так, как они прописаны в пресете (с учётом Sweetener)
            const float targetMidi = static_cast<float> (activeStrings.midiNotes[idx])
                                   + (activeStrings.centOffsets[idx] * 0.01f);
            baseFreqs[idx]         = clampedA4 * std::pow (2.0f, (targetMidi - 69.0f) / 12.0f);
        }

        for (int i = 0; i < numConfiguredStrings; ++i)
        {
            const auto idx     = static_cast<std::size_t> (i);
            const float baseHz = baseFreqs[idx];

            // Если основной тон струны s (1*f_s) совпадает с мощной 3-й или 4-й гармоникой нижележащей басовой струны
            // (2-я струна совпадает с 3*f6, 1-я струна совпадает с 4*f6 и 3*f5), отслеживаем их 2-ю гармонику (2*f_s).
            int harmOrder      = 1;
            int colliding3xIdx = -1;
            int colliding4xIdx = -1;
            float resonatorQ   = 18.0f;

            for (int low = 0; low < i; ++low)
            {
                const float ratio = baseHz / baseFreqs[static_cast<std::size_t> (low)];
                if (std::abs (ratio - 3.0f) < 0.035f && harmOrder == 1)
                {
                    harmOrder      = 2;
                    colliding3xIdx = low; // 2-я струна: на частоте 2*f2 соседствует только 6*f6
                    resonatorQ     = 14.5f;
                }
                else if (std::abs (ratio - 4.0f) < 0.035f && harmOrder == 1)
                {
                    harmOrder      = 2;
                    colliding4xIdx = low; // 1-я струна: отслеживаем 2*f1 (622..659 Гц)
                    resonatorQ     = 18.5f;
                }
            }

            const float effectiveHz = baseHz * static_cast<float> (harmOrder);
            float notchFreqHz       = effectiveHz * 2.0f;

            if (harmOrder == 1 && i >= 2)
            {
                // Для струн 4 и 3 режем 2-ю гармонику струны s-2 (+200 центов выше основного тона)
                const float low2Harm2 = 2.0f * baseFreqs[static_cast<std::size_t> (i - 2)];
                if (low2Harm2 > effectiveHz * 1.07f && low2Harm2 < effectiveHz * 1.25f)
                    notchFreqHz = low2Harm2;
            }
            else if (harmOrder == 2 && i >= 1)
            {
                // Для 1-й струны (2*f1 = 622..659 Гц) режем 3-ю гармонику 2-й струны (3*f2 = 699..740 Гц, +200 центов выше)
                // Для 2-й струны (2*f2 = 466..494 Гц) по умолчанию режем 3-ю гармонику 3-й струны (3*f3 = 555..588 Гц)
                notchFreqHz = 3.0f * baseFreqs[static_cast<std::size_t> (i - 1)];
            }

            channels[idx].configure (baseHz, notchFreqHz, harmOrder, colliding3xIdx, colliding4xIdx, resonatorQ, decimatedFs);
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

                const bool globalBlank = (attackBlankFrames > 0);
                for (int s = 0; s < numConfiguredStrings; ++s)
                    channels[static_cast<std::size_t> (s)].processSample (filtered, globalBlank);

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
    // 1. У витых струн (harmOrder == 1) основное колебание f_1 лежит на 1.6..3.4 цента ниже гармонического центра тяжести.
    // 2. У тонких необмоточных струн 1 и 2 при замере на 2-й гармонике (harmOrder == 2) частота f_2 физически
    //    растянута жёсткостью сердечника выше 2*f_1 примерно на +1.35 цента, поэтому компенсируем на -1.35 цента,
    //    чтобы установившееся значение после отката точно совпадало с основным тоном струны в Solo-режиме.
    [[nodiscard]] static constexpr float inharmOffsetCents (float baseFreqHz, int harmOrder) noexcept
    {
        if (harmOrder > 1)       return -1.35f; // Компенсация растяжения 2-й гармоники тонких струн 1 и 2
        if (baseFreqHz < 90.0f)  return 3.45f;  // 6-я струна (E2 / D#2 / D2) и басовые струны
        if (baseFreqHz < 120.0f) return 2.80f;  // 5-я струна (A2 / G#2)
        if (baseFreqHz < 160.0f) return 2.95f;  // 4-я струна (D3 / C#3)
        if (baseFreqHz < 210.0f) return 1.65f;  // 3-я струна (G3 / F#3)
        return 1.00f;
    }

    struct ResonatorChannel
    {
        static constexpr std::size_t kRingSize = 32;

        float baseFreqHz     = 100.0f;
        float targetFreqHz   = 100.0f;
        float sampleRateDec  = 24000.0f;
        float defaultNotchHz = 200.0f;
        int   harmOrder      = 1;
        int   colliding3xIdx = -1;
        int   colliding4xIdx = -1;

        float expectedPeriod = 240.0f;
        float minValidPeriod = 230.0f;
        float maxValidPeriod = 250.0f;
        float inharmCents    = 0.0f;

        // 0. Входной ФВЧ 1-го порядка
        float hp_coeff = 0.98f;
        float hp_x1 = 0.0f, hp_y1 = 0.0f;

        // 1. Заграждающий режекторный фильтр (Notch)
        float nt_b0 = 1.0f, nt_b1 = 0.0f, nt_b2 = 1.0f, nt_a1 = 0.0f, nt_a2 = 0.0f;
        float nt_x1 = 0.0f, nt_x2 = 0.0f, nt_y1 = 0.0f, nt_y2 = 0.0f;

        // 2. Канальный ФНЧ Баттерворта
        float lp_b0 = 0.0f, lp_b1 = 0.0f, lp_b2 = 0.0f, lp_a1 = 0.0f, lp_a2 = 0.0f;
        float lp_x1 = 0.0f, lp_x2 = 0.0f, lp_y1 = 0.0f, lp_y2 = 0.0f;

        // 3. Трёхкаскадный узкополосный резонатор
        float bp_b0 = 0.0f, bp_b2 = 0.0f, bp_a1 = 0.0f, bp_a2 = 0.0f;
        float s1_x1 = 0.0f, s1_x2 = 0.0f, s1_y1 = 0.0f, s1_y2 = 0.0f;
        float s2_x1 = 0.0f, s2_x2 = 0.0f, s2_y1 = 0.0f, s2_y2 = 0.0f;
        float s3_x1 = 0.0f, s3_x2 = 0.0f, s3_y1 = 0.0f, s3_y2 = 0.0f;

        std::array<float, kRingSize> periodRing {};
        std::size_t ringWriteIdx = 0;
        std::size_t ringCount    = 0;
        int newPeriodsInWindow   = 0;

        float prevSample         = 0.0f;
        int   samplesSincePos    = 0;
        float prevPosFrac        = 0.0f;
        bool  hasPosCross        = false;

        int   samplesSinceNeg    = 0;
        float prevNegFrac        = 0.0f;
        bool  hasNegCross        = false;

        float energySum          = 0.0f;
        int   energySamples      = 0;

        float rawRmsSmooth       = 0.0f;
        float peakEnergy         = 0.0f;
        int   framesSincePeak    = 100;
        int   channelBlankFrames = 0;

        float smoothedCents      = 0.0f;
        float smoothedEnergy     = 0.0f;
        int   holdFrames         = 0;
        int   lockAgeFrames      = 0;
        bool  isLocked           = false;
        bool  hasCentsInit       = false;

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
            peakEnergy         = 0.0f;
            framesSincePeak    = 100;
            channelBlankFrames = 0;
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

        // Оставляет только последние keepCount периодов при новом ударе по струне, чтобы не терять захват,
        // но быстро сбросить устаревшие периоды от пика атаки
        void trimOldPeriods (std::size_t keepCount) noexcept
        {
            if (ringCount <= keepCount)
                return;
            std::array<float, kRingSize> temp {};
            for (std::size_t i = 0; i < keepCount; ++i)
            {
                const std::size_t srcIdx = (ringWriteIdx + kRingSize - keepCount + i) & (kRingSize - 1);
                temp[i] = periodRing[srcIdx];
            }
            periodRing.fill (0.0f);
            for (std::size_t i = 0; i < keepCount; ++i)
                periodRing[i] = temp[i];
            ringWriteIdx = keepCount & (kRingSize - 1);
            ringCount    = keepCount;
        }

        void setNotchFilter (float notchHz, float qVal) noexcept
        {
            constexpr float twoPi    = std::numbers::pi_v<float> * 2.0f;
            const float clampedNotch = std::clamp (notchHz, 20.0f, sampleRateDec * 0.44f);
            const float ntOmega      = twoPi * clampedNotch / sampleRateDec;
            const float ntSn         = std::sin (ntOmega);
            const float ntCs         = std::cos (ntOmega);
            const float ntAlpha      = ntSn / (2.0f * qVal);
            const float ntInvA0      = 1.0f / (1.0f + ntAlpha);

            nt_b0 = 1.0f * ntInvA0;
            nt_b1 = (-2.0f * ntCs) * ntInvA0;
            nt_b2 = 1.0f * ntInvA0;
            nt_a1 = nt_b1;
            nt_a2 = (1.0f - ntAlpha) * ntInvA0;
        }

        void configure (
            float baseHz,
            float notchHz,
            int   harmonicMultiplier,
            int   colliding3xStringIdx,
            int   colliding4xStringIdx,
            float resonatorQ,
            float fsDec) noexcept
        {
            sampleRateDec  = fsDec;
            baseFreqHz     = std::clamp (baseHz, 20.0f, sampleRateDec * 0.42f);
            harmOrder      = std::max (1, harmonicMultiplier);
            colliding3xIdx = colliding3xStringIdx;
            colliding4xIdx = colliding4xStringIdx;
            targetFreqHz   = std::clamp (baseFreqHz * static_cast<float> (harmOrder), 20.0f, sampleRateDec * 0.42f);
            defaultNotchHz = notchHz;

            expectedPeriod = sampleRateDec / targetFreqHz;
            inharmCents    = inharmOffsetCents (baseFreqHz, harmOrder);

            // Допуск захвата периода струны +-55 центов
            minValidPeriod = expectedPeriod * 0.9685f;
            maxValidPeriod = expectedPeriod * 1.0325f;

            constexpr float twoPi = std::numbers::pi_v<float> * 2.0f;

            // 0. Входной ФВЧ на 0.80 * targetFreqHz
            const float hpCutoff = std::clamp (targetFreqHz * 0.80f, 18.0f, sampleRateDec * 0.38f);
            hp_coeff = std::exp (-twoPi * hpCutoff / sampleRateDec);

            // 1. Режекторный фильтр (Notch) на частоту главной помехи
            setNotchFilter (defaultNotchHz, (harmOrder > 1) ? 6.0f : 8.0f);

            // 2. Канальный ФНЧ Баттерворта на 1.22 * targetFreqHz
            const float lpCutoff = std::clamp (targetFreqHz * 1.22f, 35.0f, sampleRateDec * 0.45f);
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

            // 3. Трёхкаскадный резонатор с полюсами на omega0
            const float omega0 = twoPi * targetFreqHz / sampleRateDec;
            const float poleR  = std::exp (-omega0 / (2.0f * resonatorQ));
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

        void processSample (float x, bool globalBlank) noexcept
        {
            const bool recordPeriods = (!globalBlank) && (channelBlankFrames == 0);

            // Ступень 0: ФВЧ отсечки нижних струн
            const float h0 = x - hp_x1 + hp_coeff * hp_y1;
            hp_x1 = x;
            hp_y1 = h0;

            // Ступень 1: Режекторный фильтр (Notch)
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

            // Ступень 5: Полосовой резонатор 3
            const float y3 = bp_b0 * y2 + bp_b2 * s3_x2 - bp_a1 * s3_y1 - bp_a2 * s3_y2;
            s3_x2 = s3_x1;
            s3_x1 = y2;
            s3_y2 = s3_y1;
            s3_y1 = y3;

            energySum += y3 * y3;
            ++energySamples;
            ++samplesSincePos;
            ++samplesSinceNeg;

            // 1. Пересечение нуля снизу вверх
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

        // Межквартильное усечённое среднее по кольцевому буферу периодов + оценка когерентности (IQR в центах)
        [[nodiscard]] float computeStablePeriod (float& outIqrSpreadCents) const noexcept
        {
            outIqrSpreadCents = 0.0f;
            if (ringCount == 0)
                return expectedPeriod;

            // На тонких струнах (harmOrder > 1) используем окно из последних 16 периодов (~25..35 мс на 466..660 Гц),
            // чтобы анализатор мгновенно отслеживал откат высоты тона после атаки и подстройку колком на лету!
            const std::size_t windowLen = (harmOrder > 1)
                                              ? std::min<std::size_t> (ringCount, 16)
                                              : ringCount;

            std::array<float, kRingSize> sorted {};
            for (std::size_t i = 0; i < windowLen; ++i)
            {
                const std::size_t srcIdx = (ringWriteIdx + kRingSize - windowLen + i) & (kRingSize - 1);
                sorted[i] = periodRing[srcIdx];
            }

            std::sort (sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t> (windowLen));

            if (windowLen >= 4)
            {
                const std::size_t q1  = windowLen / 4;
                const std::size_t q3  = windowLen - 1 - q1;
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
                return (cnt > 0) ? (sum / static_cast<float> (cnt)) : sorted[windowLen / 2];
            }

            return sorted[windowLen / 2];
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

    // Анти-алиасинговый фильтр Баттерворта перед децимацией (срез 1800 Гц для захвата 2-й гармоники 1-й и 2-й струн)
    float aa_b0 = 0.0f, aa_b1 = 0.0f, aa_b2 = 0.0f, aa_a1 = 0.0f, aa_a2 = 0.0f;
    float aa_x1 = 0.0f, aa_x2 = 0.0f, aa_y1 = 0.0f, aa_y2 = 0.0f;

    void setupAntiAliasFilter (double sampleRate) noexcept
    {
        constexpr float cutoff = 1800.0f;
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
        const float frameInputRms = (inputSamplesCount > 0)
                                        ? std::sqrt (inputSqSum / static_cast<float> (inputSamplesCount))
                                        : 0.0f;
        inputSqSum        = 0.0f;
        inputSamplesCount = 0;

        const bool isPickAttack = (frameInputRms > smoothInputRms * 1.55f) && (frameInputRms > 0.0015f);
        smoothInputRms          = smoothInputRms * 0.72f + frameInputRms * 0.28f;

        if (isPickAttack)
        {
            attackBlankFrames = 5; // ~100 мс глобальной блокировки щелчка атаки
            for (int s = 0; s < numConfiguredStrings; ++s)
            {
                auto& ch           = channels[static_cast<std::size_t> (s)];
                ch.framesSincePeak = 0;
                ch.peakEnergy      = 0.0f;
                if (ch.harmOrder > 1)
                    ch.trimOldPeriods (4);
            }
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

            // Поканальная детекция щипка конкретной струны
            const bool chAttack = (r > ch.rawRmsSmooth * 1.50f) && (r > 0.00025f);

            if (r > ch.rawRmsSmooth)
                ch.rawRmsSmooth = ch.rawRmsSmooth * 0.30f + r * 0.70f;
            else
                ch.rawRmsSmooth = ch.rawRmsSmooth * 0.86f + r * 0.14f;
            rawRms[idx] = ch.rawRmsSmooth;

            if (chAttack)
            {
                ch.channelBlankFrames = 4; // ~80 мс пропуска пикового всплеска натяжения сразу после щипка
                ch.peakEnergy         = ch.rawRmsSmooth;
                ch.framesSincePeak    = 0;
                if (ch.harmOrder > 1)
                    ch.trimOldPeriods (4);
            }
            else
            {
                if (ch.channelBlankFrames > 0)
                {
                    --ch.channelBlankFrames;
                }
                else
                {
                    if (ch.rawRmsSmooth > ch.peakEnergy)
                        ch.peakEnergy = ch.rawRmsSmooth;
                    else
                        ch.peakEnergy *= 0.992f; // плавная адаптация огибающей к плато сустейна после спада пика атаки
                    ++ch.framesSincePeak;
                }
            }

            // Выравнивание чувствительности тонких струн для отображения
            const float highStringGain = 1.0f + (static_cast<float> (s) / denomStrings) * 0.90f;
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

        const float absFloor          = 0.000040f + (1.0f - sensitivity) * 0.000030f;
        const float displayGateThresh = std::max (absFloor, maxChannelRms * 0.045f);
        const float curve             = sensitivity * sensitivity;

        // 1. Определяем гармонически связанные струны (по их базовым частотам baseFreqHz) для надёжного разделения Solo / Polytune
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
                if (rawRms[lowIdx] < absFloor * 3.0f || rawRms[lowIdx] < rawRms[idx] * 0.08f)
                    continue;

                const float ratio = ch.baseFreqHz / channels[lowIdx].baseFreqHz;
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

        // 3. Стабильная логика переключения Solo <-> Polytune
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
            if (nonHarmonicActiveCount >= 2 && maxNonHarmonicRms >= absFloor * 2.5f)
            {
                polyHoldFrames     = std::max (polyHoldFrames, 55);
                soloOverrideFrames = 0;
            }
            else if (singleStringDominant && monoHasStrongSingleLock && attackBlankFrames == 0)
            {
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

            // Двусторонний динамический режекторный фильтр (Notch) для 2-й струны (2*f2 = 466..494 Гц):
            // Вырезаем 6-ю гармонику 6-й струны (6*f6) как при расстроенной 6-й струне, так и при расстроенной 2-й струне
            // (когда |cents[2] - cents[6]| > 2.5 цента), чтобы 2-я струна свободно отслеживала откат после атаки и подстройку колком!
            if (ch.harmOrder == 2 && ch.colliding3xIdx >= 0)
            {
                const auto lowIdx = static_cast<std::size_t> (ch.colliding3xIdx);
                const auto& lowCh = channels[lowIdx];
                const bool lowStringRinging = (rawRms[lowIdx] > absFloor * 3.0f);
                const bool pitchSeparated   = lowCh.hasCentsInit &&
                                              (std::abs (lowCh.smoothedCents) > 2.5f ||
                                               (ch.hasCentsInit && std::abs (ch.smoothedCents - lowCh.smoothedCents) > 2.5f));
                if (lowStringRinging && pitchSeparated)
                {
                    const float lowActualHz = lowCh.targetFreqHz * std::pow (2.0f, lowCh.smoothedCents / 1200.0f);
                    ch.setNotchFilter (lowActualHz * 6.0f, 18.0f);
                }
                else
                {
                    ch.setNotchFilter (ch.defaultNotchHz, 6.0f);
                }
            }

            const float activeThresh = polyModeLocked
                                           ? std::max (absFloor, maxChannelRms * 0.025f)
                                           : displayGateThresh;
            const bool hasRecentCycles = (ch.newPeriodsInWindow >= 1) ||
                                         (ch.ringCount >= 4 && chRms[idx] >= activeThresh * 1.2f);
            const bool suppressHarmonicGhost = (!polyModeLocked) && isHarmonicOfLower[idx];

            if (!suppressHarmonicGhost && chRms[idx] >= activeThresh && hasRecentCycles && ch.ringCount >= 4)
            {
                float iqrSpreadCents = 0.0f;
                const float stablePeriod = ch.computeStablePeriod (iqrSpreadCents);

                const bool isBlanked = (attackBlankFrames > 0) || (ch.channelBlankFrames > 0);

                // Проверяем, не затухла ли тонкая струна (1 или 2) полностью на фоне ещё гудящей 6-й струны:
                // пока энергия тонкой струны >= 10% от её плавно адаптирующегося пика ИЛИ пока она соизмерима с басовой струной,
                // разрешаем непрерывное обновление на протяжении всего сустейна (~2.2+ секунды), чтобы видеть откат и крутить колок!
                bool bassSwamping = false;
                const int lowRefIdx = (ch.colliding3xIdx >= 0) ? ch.colliding3xIdx : ch.colliding4xIdx;
                if (ch.harmOrder > 1 && lowRefIdx >= 0)
                {
                    const float lowRms = rawRms[static_cast<std::size_t> (lowRefIdx)];
                    // Блокируем только в самом конце затухания тонкой струны (> 1.6 с после удара), если 6-я струна в 3+ раза громче
                    if (ch.framesSincePeak > 80 && lowRms > ch.rawRmsSmooth * 3.0f && ch.rawRmsSmooth < ch.peakEnergy * 0.16f)
                        bassSwamping = true;
                }

                const bool allowUpdate = (ch.harmOrder > 1)
                                             ? (!isBlanked && !bassSwamping && ch.framesSincePeak <= 115 && ch.rawRmsSmooth >= ch.peakEnergy * 0.10f)
                                             : (!isBlanked && ch.rawRmsSmooth >= ch.peakEnergy * 0.08f);
                const float maxIqr     = (ch.harmOrder > 1) ? 6.5f : 8.0f;

                if (allowUpdate && iqrSpreadCents < maxIqr)
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
                        const float coherenceWeight = (iqrSpreadCents < 3.5f) ? 1.0f : 0.60f;

                        if (ch.harmOrder > 1)
                        {
                            // Двухфазное отслеживание для тонких струн (1 и 2):
                            // 1) В первые ~140 мс после щипка (framesSincePeak <= 7), пока струна завышена из-за пикового натяжения атаки,
                            //    используем умеренный коэффициент (0.16), чтобы не прыгать на пик атаки.
                            // 2) Начиная со ~160 мс (framesSincePeak > 7), когда струна откатывает к истинной высоте тона и музыкант
                            //    подстраивает колок, включаем быстрое отслеживание (0.28..0.34), чтобы плавно и точно ловить откат и вращение колка!
                            const float settleAlpha = (ch.framesSincePeak <= 7)
                                                          ? 0.16f
                                                          : ((ch.framesSincePeak <= 45) ? 0.30f : (0.14f + curve * 0.18f));
                            ch.smoothedCents += (clampedC - ch.smoothedCents) * (settleAlpha * coherenceWeight);
                        }
                        else if (ch.lockAgeFrames <= 6)
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
                    ch.holdFrames = 55; // ~1.1 с плавного удержания зажжённого столбца струны
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
