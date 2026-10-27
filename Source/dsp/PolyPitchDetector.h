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
        // Децимация до ~22..24 кГц для высокой суб-сэмпловой точности
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
            // (например, 2-я гармоника 6-й струны E2 = 164.8 Гц рядом с 4-й струной D3 = 146.8 Гц,
            //  и 2-я гармоника 5-й струны A2 = 220.0 Гц рядом с 3-й струной G3 = 196.0 Гц).
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

                for (int s = 0; s < numConfiguredStrings; ++s)
                    channels[static_cast<std::size_t> (s)].processSample (filtered);

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
    struct ResonatorChannel
    {
        // Компактный буфер на 8 последних периодов (быстро забывает стартовый всплеск атаки медиатора)
        static constexpr std::size_t kRingSize = 8;

        float targetFreqHz   = 100.0f;
        float expectedPeriod = 240.0f;
        float minValidPeriod = 230.0f;
        float maxValidPeriod = 250.0f;

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
        // Даёт > 52 дБ подавления соседних струн (полностью устраняя увод частоты по теореме Райса)
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

        void processSample (float x) noexcept
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
                if (hasPosCross)
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
                if (hasNegCross)
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

        // Межквартильное усечённое среднее по кольцевому буферу из 8 периодов
        [[nodiscard]] float computeStablePeriod() const noexcept
        {
            if (ringCount == 0)
                return expectedPeriod;

            std::array<float, kRingSize> sorted {};
            for (std::size_t i = 0; i < ringCount; ++i)
                sorted[i] = periodRing[i];

            std::sort (sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t> (ringCount));

            if (ringCount >= 6)
            {
                // Отбрасываем крайние выбросы (по 2 с каждой стороны при 8 элементах, или по 1 при 6..7)
                const std::size_t trim  = (ringCount >= 8) ? 2 : 1;
                const std::size_t start = trim;
                const std::size_t end   = ringCount - trim;
                float sum = 0.0f;
                for (std::size_t i = start; i < end; ++i)
                    sum += sorted[i];
                return sum / static_cast<float> (end - start);
            }
            if (ringCount >= 3)
            {
                const std::size_t mid = ringCount / 2;
                return (ringCount % 2 == 1)
                           ? sorted[mid]
                           : 0.5f * (sorted[mid - 1] + sorted[mid]);
            }
            return sorted[ringCount - 1];
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
        // 0. Детекция удара медиатором (при атаке сбрасываем переходный звон фильтров, чтобы не запоминать завышенную атаку)
        const float frameInputRms = (inputSamplesCount > 0)
                                        ? std::sqrt (inputSqSum / static_cast<float> (inputSamplesCount))
                                        : 0.0f;
        inputSqSum        = 0.0f;
        inputSamplesCount = 0;

        const bool isPickAttack = (frameInputRms > smoothInputRms * 1.55f) && (frameInputRms > 0.0010f);
        smoothInputRms          = smoothInputRms * 0.72f + frameInputRms * 0.28f;

        if (isPickAttack)
        {
            attackBlankFrames = 3; // ~60 мс на прохождение стартового щелчка медиатора
        }
        else if (attackBlankFrames > 0)
        {
            --attackBlankFrames;
            if (attackBlankFrames == 0)
            {
                // Очищаем буферы периодов от нестабильных первых миллисекунд щипка,
                // чтобы замер строился строго по чистому установившемуся сустейну струны
                for (int s = 0; s < numConfiguredStrings; ++s)
                {
                    auto& ch = channels[static_cast<std::size_t> (s)];
                    ch.flushPeriods();
                    ch.lockAgeFrames = 0;
                }
            }
        }

        float maxChannelRms = 0.0f;
        std::array<float, kMaxStrings> chRms {};

        const float denomStrings = static_cast<float> (std::max (1, numConfiguredStrings - 1));
        for (int s = 0; s < numConfiguredStrings; ++s)
        {
            const auto idx = static_cast<std::size_t> (s);
            auto& ch       = channels[idx];
            float rms      = (ch.energySamples > 0)
                                 ? std::sqrt (ch.energySum / static_cast<float> (ch.energySamples))
                                 : 0.0f;

            // Выравнивание чувствительности тонких струн относительно толстых обмоточных
            const float highStringGain = 1.0f + (static_cast<float> (s) / denomStrings) * 1.15f;
            rms *= highStringGain;

            if (rms > ch.smoothedEnergy)
                ch.smoothedEnergy = ch.smoothedEnergy * 0.30f + rms * 0.70f;
            else
                ch.smoothedEnergy = ch.smoothedEnergy * 0.86f + rms * 0.14f;

            chRms[idx]    = ch.smoothedEnergy;
            maxChannelRms = std::max (maxChannelRms, ch.smoothedEnergy);

            ch.energySum     = 0.0f;
            ch.energySamples = 0;
        }

        const float absFloor          = 0.000040f + (1.0f - sensitivity) * 0.000030f;
        const float triggerGateThresh = std::max (absFloor * 2.5f, maxChannelRms * 0.24f);
        const float displayGateThresh = std::max (absFloor,        maxChannelRms * 0.045f);

        const float curve = sensitivity * sensitivity;

        // 1. Подсчитываем число независимых струн, преодолевших порог триггера полифонии
        int independentTriggerCount = 0;
        float secondNonHarmonicRms  = 0.0f;
        std::array<bool, kMaxStrings> isHarmonicOfLower {};

        for (int s = 0; s < numConfiguredStrings; ++s)
        {
            const auto idx = static_cast<std::size_t> (s);
            const auto& ch = channels[idx];

            bool harmonicFlag = false;
            for (int low = 0; low < s; ++low)
            {
                const auto lowIdx = static_cast<std::size_t> (low);
                if (chRms[lowIdx] < maxChannelRms * 0.25f)
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

            if (!harmonicFlag && chRms[idx] < maxChannelRms * 0.999f)
                secondNonHarmonicRms = std::max (secondNonHarmonicRms, chRms[idx]);

            const bool hasCycles = (ch.newPeriodsInWindow >= 1) || (ch.ringCount >= 2);
            if (!harmonicFlag && chRms[idx] >= triggerGateThresh && hasCycles)
                ++independentTriggerCount;
        }

        // 2. Переключение Solo <-> Polytune:
        const bool singleStringDominant = (maxChannelRms > absFloor * 3.0f) &&
                                          (secondNonHarmonicRms < maxChannelRms * 0.30f);

        if (!singleStringDominant && independentTriggerCount >= 2 && attackBlankFrames == 0)
        {
            ++polyConfirmFrames;
            if (polyConfirmFrames >= 2 || polyModeLocked)
            {
                polyModeLocked = true;
                polyHoldFrames = 32; // ~640 мс плавного удержания экрана Polytune на сустейне
            }
        }
        else
        {
            polyConfirmFrames = 0;
            if (singleStringDominant && monoHasStrongSingleLock)
            {
                polyHoldFrames = 0;
                polyModeLocked = false;
            }
            else if (maxChannelRms < absFloor)
            {
                if (polyHoldFrames > 0)
                    --polyHoldFrames;
                else
                    polyModeLocked = false;
            }
            else if (polyHoldFrames > 0)
            {
                --polyHoldFrames;
                if (polyHoldFrames == 0)
                    polyModeLocked = false;
            }
        }

        // 3. Обновляем состояние и сглаженные центы для каждой струны
        int currentlyLocked = 0;
        for (int s = 0; s < numConfiguredStrings; ++s)
        {
            const auto idx = static_cast<std::size_t> (s);
            auto& ch       = channels[idx];

            // Пока струна даёт пересечения нуля выше абсолютного порога тишины, продолжаем уточнять её частоту,
            // чтобы она гарантированно доходила до точного значения на сустейне и не «замерзала» на старом замере атаки!
            const float activeThresh = polyModeLocked ? std::max (absFloor, maxChannelRms * 0.025f) : displayGateThresh;
            const bool hasRecentCycles = (ch.newPeriodsInWindow >= 1) ||
                                         (ch.ringCount >= 2 && chRms[idx] >= activeThresh * 1.2f);
            const bool suppressHarmonicGhost = (!polyModeLocked) && isHarmonicOfLower[idx];

            if (!suppressHarmonicGhost && chRms[idx] >= activeThresh && hasRecentCycles && ch.ringCount >= 2)
            {
                const float stablePeriod = ch.computeStablePeriod();
                const float measuredHz   = decimatedFs / stablePeriod;
                const float rawCents     = 1200.0f * std::log2 (measuredHz / ch.targetFreqHz);
                const float clampedC     = std::clamp (rawCents, -50.0f, 50.0f);

                if (!ch.hasCentsInit)
                {
                    ch.smoothedCents = clampedC;
                    ch.hasCentsInit  = true;
                    ch.lockAgeFrames = 1;
                }
                else
                {
                    ++ch.lockAgeFrames;
                    // В первые ~160 мс после атаки быстро сходимся к реальному строю струны,
                    // а затем переходим к плавному бездрожащему удержанию
                    if (ch.lockAgeFrames <= 8)
                    {
                        ch.smoothedCents += (clampedC - ch.smoothedCents) * 0.50f;
                    }
                    else
                    {
                        const float centsDiff = std::abs (clampedC - ch.smoothedCents);
                        const float minW  = (centsDiff < 1.5f) ? 0.03f : ((centsDiff < 4.0f) ? 0.06f : 0.12f);
                        const float maxW  = (centsDiff < 1.5f) ? 0.16f : ((centsDiff < 4.0f) ? 0.28f : 0.45f);
                        const float alpha = minW + curve * (maxW - minW);

                        ch.smoothedCents += (clampedC - ch.smoothedCents) * alpha;
                    }
                }

                ch.isLocked   = true;
                ch.holdFrames = 32; // ~640 мс плавного удержания зажжённого столбца струны
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
                if (chRms[idx] < absFloor * 0.5f)
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
