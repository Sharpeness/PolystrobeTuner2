#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

class MpmPitchDetector
{
public:
    static constexpr int kBufferSize        = 1024;
    static constexpr int kHalfBufferSize    = kBufferSize / 2; // 512
    static constexpr int kHopSize           = 256;
    static constexpr int kMaxDecimation     = 8;
    static constexpr int kMaxFullBufferSize = kBufferSize * kMaxDecimation; // 8192
    static constexpr int kMaxPeaks          = 64;

    MpmPitchDetector() noexcept = default;

    void prepare (double sampleRate, int /*maxBlockSize*/) noexcept
    {
        fs = sampleRate;

        if (sampleRate >= 88200.0)      decimationFactor = 8;
        else if (sampleRate >= 44100.0) decimationFactor = 4;
        else                            decimationFactor = 2;

        decimatedFs    = fs / static_cast<double> (decimationFactor);
        fullBufferSize = kBufferSize * decimationFactor; // 2048, 4096 или 8192 (всегда степень двойки)
        fullWritePos   = 0;

        fullBuffer.fill (0.0f);
        inputBuffer.fill (0.0f);
        sqBuffer.fill (0.0f);
        nsdf.fill (0.0f);

        writePos          = 0;
        decimationCounter = 0;

        detectedFrequency  = 0.0f;
        lockedFrequency    = 0.0f;
        candidateFrequency = 0.0f;
        candidateHits      = 0;
        hasSignalFlag      = false;

        smoothRms      = 0.0f;
        silenceSamples = 0;

        lp_x1 = lp_x2 = lp_y1 = lp_y2 = 0.0f;
        hp_x1 = hp_y1 = 0.0f;

        setupFilters (sampleRate);
    }

    // Управление плавностью / точностью (10% .. 100%)
    void setSensitivity (float accuracyPercent) noexcept
    {
        const float newSens = std::clamp (accuracyPercent * 0.01f, 0.10f, 1.00f);
        if (std::abs (sensitivity - newSens) > 0.005f)
        {
            sensitivity = newSens;
            if (fs > 0.0)
                setupFilters (fs);
        }
    }

    // Переключение оптимизации фильтров Guitar / Bass
    void setBassMode (bool bass) noexcept
    {
        if (isBassMode != bass)
        {
            isBassMode = bass;
            if (fs > 0.0)
                setupFilters (fs);
        }
    }

    // Основной метод обработки блока через std::span (C++23, Real-Time Safe, Zero-Alloc)
    void process (std::span<const float> samples) noexcept
    {
        const int numSamples = static_cast<int> (samples.size());
        if (numSamples <= 0) [[unlikely]]
            return;

        const float* __restrict src = samples.data();

        // Векторизуемый расчёт суммы квадратов блока
        float sumSquares = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            sumSquares += src[i] * src[i];

        const float blockRms = std::sqrt (sumSquares / static_cast<float> (numSamples));

        constexpr float alphaRms = 0.85f;
        smoothRms = smoothRms * alphaRms + blockRms * (1.0f - alphaRms);

        // Мягкий порог гейта, не обрезающий сустейн
        const float noiseFloor = 0.00045f + (1.0f - sensitivity) * 0.00015f;
        const bool isAttack    = (blockRms > smoothRms * 1.35f && blockRms > 0.0010f);
        const int fullMask     = fullBufferSize - 1;

        for (int i = 0; i < numSamples; ++i)
        {
            const float filtered = processFilter (src[i]);

            fullBuffer[static_cast<std::size_t> (fullWritePos)] = filtered;
            fullWritePos = (fullWritePos + 1) & fullMask;

            if (++decimationCounter >= decimationFactor)
            {
                decimationCounter = 0;
                inputBuffer[static_cast<std::size_t> (writePos++)] = filtered;

                if (writePos >= kBufferSize)
                {
                    writePos = kBufferSize - kHopSize;

                    if (blockRms >= noiseFloor)
                    {
                        silenceSamples = 0;
                        runDetection (isAttack, blockRms);
                    }
                    else
                    {
                        silenceSamples += kHopSize;
                        if (silenceSamples > static_cast<int> (decimatedFs * 0.22))
                        {
                            hasSignalFlag      = false;
                            lockedFrequency    = 0.0f;
                            candidateFrequency = 0.0f;
                            candidateHits      = 0;
                        }
                    }

                    std::copy (inputBuffer.begin() + kHopSize, inputBuffer.end(), inputBuffer.begin());
                }
            }
        }
    }

    // Перегрузка для совместимости с сырым указателем JUCE AudioBuffer
    void process (const float* samples, int numSamples) noexcept
    {
        if (samples == nullptr || numSamples <= 0) [[unlikely]]
            return;
        process (std::span<const float> (samples, static_cast<std::size_t> (numSamples)));
    }

    [[nodiscard]] float getFrequency() const noexcept { return detectedFrequency; }
    [[nodiscard]] bool hasSignal() const noexcept     { return hasSignalFlag; }

private:
    // Выровненные по 32 байта буферы под AVX2 — 0 динамических аллокаций в куче
    alignas (32) std::array<float, kMaxFullBufferSize> fullBuffer {};
    alignas (32) std::array<float, kBufferSize>        inputBuffer {};
    alignas (32) std::array<float, kBufferSize>        sqBuffer {};
    alignas (32) std::array<float, kHalfBufferSize>    nsdf {};

    double fs          = 48000.0;
    double decimatedFs = 12000.0;
    int decimationFactor  = 4;
    int decimationCounter = 0;

    float sensitivity = 1.0f;
    bool isBassMode   = false;

    int writePos       = 0;
    int fullBufferSize = 4096;
    int fullWritePos   = 0;

    float detectedFrequency  = 0.0f;
    float lockedFrequency    = 0.0f;
    float candidateFrequency = 0.0f;
    int candidateHits        = 0;
    bool hasSignalFlag       = false;

    float smoothRms    = 0.0f;
    int silenceSamples = 0;

    float lp_b0 = 0.0f, lp_b1 = 0.0f, lp_b2 = 0.0f, lp_a1 = 0.0f, lp_a2 = 0.0f;
    float lp_x1 = 0.0f, lp_x2 = 0.0f, lp_y1 = 0.0f, lp_y2 = 0.0f;
    float hp_x1 = 0.0f, hp_y1 = 0.0f;
    float hp_coeff = 0.998f;

    void setupFilters (double sampleRate) noexcept
    {
        constexpr float pi = std::numbers::pi_v<float>;
        const float hpFreq = isBassMode ? 14.0f : 35.0f;
        hp_coeff = std::exp (-2.0f * pi * hpFreq / static_cast<float> (sampleRate));

        const float slowDown     = (1.0f - sensitivity) / 0.90f;
        const float guitarCutoff = 1150.0f - slowDown * 630.0f;
        const float bassCutoff   = 480.0f  - slowDown * 130.0f;
        const float cutoff       = isBassMode ? bassCutoff : guitarCutoff;

        const float omega = 2.0f * pi * cutoff / static_cast<float> (sampleRate);
        const float sn    = std::sin (omega);
        const float cs    = std::cos (omega);
        const float alpha = sn / (2.0f * std::numbers::sqrt2_v<float> * 0.5f);

        const float invA0 = 1.0f / (1.0f + alpha);
        lp_b0 = ((1.0f - cs) * 0.5f) * invA0;
        lp_b1 = (1.0f - cs) * invA0;
        lp_b2 = lp_b0;
        lp_a1 = (-2.0f * cs) * invA0;
        lp_a2 = (1.0f - alpha) * invA0;
    }

    [[nodiscard]] float processFilter (float input) noexcept
    {
        const float hpOut = input - hp_x1 + hp_coeff * hp_y1;
        hp_x1 = input;
        hp_y1 = hpOut;

        const float lpOut = lp_b0 * hpOut + lp_b1 * lp_x1 + lp_b2 * lp_x2 - lp_a1 * lp_y1 - lp_a2 * lp_y2;
        lp_x2 = lp_x1;
        lp_x1 = hpOut;
        lp_y2 = lp_y1;
        lp_y1 = lpOut;

        return lpOut;
    }

    [[nodiscard]] float computeFineNsdfAt (int tauFull, int fineWindow) const noexcept
    {
        const int mask     = fullBufferSize - 1;
        const int startPos = (fullWritePos - fineWindow * 2 + fullBufferSize) & mask;
        const float* __restrict buf = fullBuffer.data();

        float acf = 0.0f;
        float m0  = 0.0f;

        for (int i = 0; i < fineWindow; ++i)
        {
            const float x1 = buf[(startPos + i) & mask];
            const float x2 = buf[(startPos + i + tauFull) & mask];
            acf += x1 * x2;
            m0  += x1 * x1 + x2 * x2;
        }

        return (m0 > 1e-9f) ? (2.0f * acf / m0) : 0.0f;
    }

    void runDetection (bool isAttack, float currentRms) noexcept
    {
        constexpr int W = kHalfBufferSize; // 512
        const float* __restrict in = inputBuffer.data();
        float* __restrict sq       = sqBuffer.data();
        float* __restrict nsdfPtr  = nsdf.data();

        // 1. Векторизуемый расчёт квадратов входного окна
        for (int i = 0; i < kBufferSize; ++i)
            sq[i] = in[i] * in[i];

        float sumX1 = 0.0f;
        for (int i = 0; i < W; ++i)
            sumX1 += sq[i];

        float sumX2 = sumX1;
        nsdfPtr[0]  = 1.0f;

        const int minTauSearch = isBassMode ? 12 : 5;

        // Обнуляем начальный участок до minTauSearch, поддерживая бегущую сумму sumX2 без ветвлений внутри основного цикла
        for (int tau = 1; tau < minTauSearch; ++tau)
        {
            sumX2 += sq[W + tau - 1] - sq[tau - 1];
            nsdfPtr[tau] = 0.0f;
        }

        // Основной цикл NSDF без внутренних ветвлений (максимальная авто-векторизация AVX2/FMA)
        for (int tau = minTauSearch; tau < W; ++tau)
        {
            sumX2 += sq[W + tau - 1] - sq[tau - 1];

            const float* __restrict p1 = in;
            const float* __restrict p2 = in + tau;

            float acc0 = 0.0f, acc1 = 0.0f, acc2 = 0.0f, acc3 = 0.0f;
            for (int i = 0; i < W; i += 4)
            {
                acc0 += p1[i]     * p2[i];
                acc1 += p1[i + 1] * p2[i + 1];
                acc2 += p1[i + 2] * p2[i + 2];
                acc3 += p1[i + 3] * p2[i + 3];
            }

            const float acf = (acc0 + acc1) + (acc2 + acc3);
            const float m0  = sumX1 + sumX2;
            nsdfPtr[tau]    = (m0 > 1e-9f) ? (2.0f * acf / m0) : 0.0f;
        }

        struct Peak
        {
            int tau = 0;
            float value = 0.0f;
        };
        std::array<Peak, kMaxPeaks> peaks {};
        int numPeaks = 0;

        for (int tau = minTauSearch + 1; tau < W - 1; ++tau)
        {
            const float val = nsdfPtr[tau];
            if (val > 0.0f && val > nsdfPtr[tau - 1] && val >= nsdfPtr[tau + 1])
            {
                if (numPeaks < kMaxPeaks)
                    peaks[static_cast<std::size_t> (numPeaks++)] = { tau, val };
            }
        }

        if (numPeaks == 0)
            return;

        float highestVal = 0.0f;
        for (int i = 0; i < numPeaks; ++i)
            highestVal = std::max (highestVal, peaks[static_cast<std::size_t> (i)].value);

        const float minClarity = (currentRms < 0.002f) ? 0.60f : 0.45f;
        if (highestVal < minClarity)
            return;

        const float cutoff = std::max (minClarity, highestVal * 0.85f);

        int selectedTau = -1;
        for (int i = 0; i < numPeaks; ++i)
        {
            if (peaks[static_cast<std::size_t> (i)].value >= cutoff)
            {
                selectedTau = peaks[static_cast<std::size_t> (i)].tau;
                break;
            }
        }

        if (selectedTau <= 0 || selectedTau >= W - 1)
            return;

        // 2. Уточнение периода на полной частоте дискретизации (Dual-Rate Fine Peak Refinement)
        const float slowDown    = (1.0f - sensitivity) / 0.90f;
        const int centerFullTau = selectedTau * decimationFactor;
        const int minFineW      = 256  + static_cast<int> (slowDown * 512.0f);
        const int maxFineW      = 1024 + static_cast<int> (slowDown * 512.0f);
        const int fineW         = std::clamp (centerFullTau * (2 + static_cast<int> (slowDown * 2.0f)), minFineW, maxFineW);

        int bestFullTau   = centerFullTau;
        float bestFullVal = -1.0f;
        const int searchRadius = decimationFactor;
        std::array<float, 17> localNsdf {};

        for (int offset = -searchRadius; offset <= searchRadius; ++offset)
        {
            const int t   = centerFullTau + offset;
            const float v = (t > 2) ? computeFineNsdfAt (t, fineW) : 0.0f;
            localNsdf[static_cast<std::size_t> (offset + searchRadius)] = v;
            if (v > bestFullVal)
            {
                bestFullVal = v;
                bestFullTau = t;
            }
        }

        const int bestIdx = (bestFullTau - centerFullTau) + searchRadius;
        const float alpha = (bestIdx > 0)
                                ? localNsdf[static_cast<std::size_t> (bestIdx - 1)]
                                : computeFineNsdfAt (bestFullTau - 1, fineW);
        const float beta  = bestFullVal;
        const float gamma = (bestIdx < 2 * searchRadius)
                                ? localNsdf[static_cast<std::size_t> (bestIdx + 1)]
                                : computeFineNsdfAt (bestFullTau + 1, fineW);

        const float denom = 2.0f * (alpha - 2.0f * beta + gamma);
        float delta = (std::abs (denom) > 1e-7f) ? (alpha - gamma) / denom : 0.0f;
        delta = std::clamp (delta, -1.0f, 1.0f);

        const float exactFullPeriod = static_cast<float> (bestFullTau) + delta;
        if (exactFullPeriod <= 1.0f)
            return;

        const float rawFreq = static_cast<float> (fs) / exactFullPeriod;

        const float minAllowedFreq = isBassMode ? 20.0f : 42.0f;
        const float maxAllowedFreq = isBassMode ? 800.0f : 2000.0f;
        if (rawFreq < minAllowedFreq || rawFreq > maxAllowedFreq)
            return;

        // Защита от ложного захвата субгармоники аккорда (~46.3 Гц G1/F#1 от биений струн 4, 3, 2 в отношении 3:4:5):
        // В гитарном режиме частоты ниже 65 Гц принимаются только при высокой чистоте периодичности одиночной струны
        if (!isBassMode && rawFreq < 65.0f && highestVal < 0.78f)
            return;

        // 3. Нелинейное сглаживание от ручки Accuracy и защита от скачков (Note Lock)
        // При атаке медиатором мгновенный перезахват разрешён только при высокой чистоте тона (highestVal >= 0.82),
        // чтобы стартовый скрежет медиатора не вызывал кратковременный выброс (+40..+50 центов)
        if (lockedFrequency <= 0.0f || (isAttack && sensitivity > 0.70f && highestVal >= 0.82f))
        {
            lockedFrequency    = rawFreq;
            detectedFrequency  = rawFreq;
            candidateFrequency = 0.0f;
            candidateHits      = 0;
            hasSignalFlag      = true;
            return;
        }

        const float ratio = rawFreq / lockedFrequency;

        if (ratio >= 0.94f && ratio <= 1.06f)
        {
            const float centsDiff  = std::abs (1200.0f * std::log2 (ratio));
            const float baseWeight = (centsDiff < 0.25f) ? 0.10f  : ((centsDiff < 1.5f) ? 0.25f  : 0.45f);
            const float minWeight  = (centsDiff < 0.25f) ? 0.008f : ((centsDiff < 1.5f) ? 0.018f : 0.035f);

            const float curve     = sensitivity * sensitivity;
            const float newWeight = minWeight + curve * (baseWeight - minWeight);

            lockedFrequency    = lockedFrequency * (1.0f - newWeight) + rawFreq * newWeight;
            detectedFrequency  = lockedFrequency;
            candidateFrequency = 0.0f;
            candidateHits      = 0;
            hasSignalFlag      = true;
            return;
        }

        const int requiredFastHits = 2 + static_cast<int> (std::lround (slowDown * 4.0f));
        const int requiredSlowHits = 3 + static_cast<int> (std::lround (slowDown * 5.0f));

        if (highestVal >= 0.75f || currentRms > 0.004f)
        {
            if (++candidateHits >= requiredFastHits)
            {
                lockedFrequency    = rawFreq;
                detectedFrequency  = rawFreq;
                candidateFrequency = 0.0f;
                candidateHits      = 0;
                hasSignalFlag      = true;
                return;
            }
        }
        else
        {
            const float candRatio = (candidateFrequency > 0.0f) ? (rawFreq / candidateFrequency) : 0.0f;
            if (candRatio >= 0.96f && candRatio <= 1.04f)
            {
                if (++candidateHits >= requiredSlowHits)
                {
                    lockedFrequency    = rawFreq;
                    detectedFrequency  = rawFreq;
                    candidateFrequency = 0.0f;
                    candidateHits      = 0;
                    hasSignalFlag      = true;
                    return;
                }
            }
            else
            {
                candidateFrequency = rawFreq;
                candidateHits      = 1;
            }
        }

        detectedFrequency = lockedFrequency;
        hasSignalFlag     = true;
    }
};