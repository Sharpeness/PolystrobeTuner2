#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>

class StrobeArcComponent : public juce::Component
{
public:
    static constexpr int kNumRows    = 5;
    static constexpr int kNumCols    = 23;
    static constexpr int kNumPoints  = kNumRows * kNumCols;
    static constexpr int kMaxStrings = 8;

    struct PolyColumnData
    {
        float cents = 0.0f;
        bool active = false;
    };

    StrobeArcComponent() = default;

    void updateStrobe (float cents, bool active,
                       float fastThresh = 0.1f, float slowThresh = 1.0f, bool slowAbove = true) noexcept
    {
        const bool modeChanged = isPolyMode;
        isPolyMode = false;

        // Плавно гасим столбики и накал полос Polytune при переходе в моно-стробоскоп
        bool polyAlphaChanged = false;
        for (std::size_t s = 0; s < static_cast<std::size_t> (kMaxStrings); ++s)
        {
            if (polyColAlpha[s] > 0.001f)
            {
                polyColAlpha[s] *= 0.82f;
                if (polyColAlpha[s] < 0.003f)
                    polyColAlpha[s] = 0.0f;
                polyAlphaChanged = true;
            }
            for (std::size_t r = 0; r < static_cast<std::size_t> (kNumRows); ++r)
            {
                if (polyRowBrightness[s][r] > 0.001f)
                {
                    polyRowBrightness[s][r] *= 0.82f;
                    if (polyRowBrightness[s][r] < 0.003f)
                        polyRowBrightness[s][r] = 0.0f;
                    polyAlphaChanged = true;
                }
            }
        }

        targetAlpha = active ? 1.0f : 0.0f;

        const float prevAlpha = currentAlpha;
        if (currentAlpha < targetAlpha)
            currentAlpha += (targetAlpha - currentAlpha) * 0.18f;
        else
            currentAlpha += (targetAlpha - currentAlpha) * 0.055f;

        if (currentAlpha < 0.003f && targetAlpha == 0.0f)
            currentAlpha = 0.0f;

        const float prevTop = topAngle;
        const float prevBtm = bottomAngle;
        const bool prevMode = isSlowAboveCurrent;

        if (currentAlpha > 0.005f)
        {
            constexpr float dt   = 1.0f / 60.0f;
            const float absCents = std::abs (cents);
            const float sign     = (cents > 0.0f) ? 1.0f : -1.0f;

            // 1. Расчёт скорости медленного стробоскопа
            const float slowSpeed = (absCents >= slowThresh) ? (absCents * 0.2f) : 0.0f;

            // 2. Расчёт скорости быстрого стробоскопа
            float fastSpeed = 0.0f;
            if (absCents >= fastThresh)
            {
                if (absCents <= 15.0f)
                    fastSpeed = absCents * 1.2f;
                else
                    fastSpeed = 15.0f * 1.2f + (absCents - 15.0f) * 0.35f;
            }

            isSlowAboveCurrent = slowAbove;
            if (slowAbove)
            {
                topAngle    -= sign * slowSpeed * dt;
                bottomAngle -= sign * fastSpeed * dt;
            }
            else
            {
                topAngle    -= sign * fastSpeed * dt;
                bottomAngle -= sign * slowSpeed * dt;
            }

            currentCents = cents;
        }

        if (modeChanged || polyAlphaChanged ||
            std::abs (prevAlpha - currentAlpha) > 0.002f ||
            std::abs (prevTop - topAngle) > 0.0005f ||
            std::abs (prevBtm - bottomAngle) > 0.0005f ||
            prevMode != isSlowAboveCurrent)
        {
            repaint();
        }
    }

    void updatePolytune (std::span<const PolyColumnData> strings, int numStrings, float polyTolerance = 2.0f) noexcept
    {
        bool changed = !isPolyMode;
        isPolyMode = true;

        if (currentAlpha > 0.001f)
        {
            currentAlpha *= 0.80f;
            if (currentAlpha < 0.003f)
                currentAlpha = 0.0f;
            changed = true;
        }
        targetAlpha = 0.0f;

        const int clampedStrings = std::clamp (numStrings, 4, kMaxStrings);
        if (currentPolyStrings != clampedStrings)
        {
            currentPolyStrings = clampedStrings;
            changed = true;
        }

        if (std::abs (currentPolyTolerance - polyTolerance) > 0.01f)
        {
            currentPolyTolerance = polyTolerance;
            changed = true;
        }

        for (int s = 0; s < kMaxStrings; ++s)
        {
            const auto idx = static_cast<std::size_t> (s);
            const bool strActive = (s < clampedStrings && idx < strings.size()) ? strings[idx].active : false;
            const float targetA  = strActive ? 1.0f : 0.0f;
            const float prevA    = polyColAlpha[idx];

            if (polyColAlpha[idx] < targetA)
                polyColAlpha[idx] += (targetA - polyColAlpha[idx]) * 0.24f;
            else
                polyColAlpha[idx] += (targetA - polyColAlpha[idx]) * 0.065f;

            if (polyColAlpha[idx] < 0.003f && targetA == 0.0f)
                polyColAlpha[idx] = 0.0f;

            if (std::abs (prevA - polyColAlpha[idx]) > 0.002f)
                changed = true;

            std::array<float, kNumRows> targetRows { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

            if (strActive || polyColAlpha[idx] > 0.003f)
            {
                if (strActive)
                {
                    const float targetCents = strings[idx].cents;
                    if (prevA < 0.02f)
                        polyColCents[idx] = targetCents;
                    else
                        polyColCents[idx] += (targetCents - polyColCents[idx]) * 0.30f;
                }

                computeRowTargetBrightness (
                    polyColCents[idx],
                    currentPolyTolerance,
                    polyColInTune[idx],
                    targetRows);
            }
            else
            {
                polyColInTune[idx] = false;
            }

            // Плавная тепловая инерция накала каждой отдельной полосы (0..4) внутри столбца струны
            for (std::size_t r = 0; r < static_cast<std::size_t> (kNumRows); ++r)
            {
                const float desired = targetRows[r] * polyColAlpha[idx];
                const float prevRow = polyRowBrightness[idx][r];

                if (polyRowBrightness[idx][r] < desired)
                    polyRowBrightness[idx][r] += (desired - polyRowBrightness[idx][r]) * 0.28f;
                else
                    polyRowBrightness[idx][r] += (desired - polyRowBrightness[idx][r]) * 0.15f;

                if (polyRowBrightness[idx][r] < 0.003f && desired < 0.003f)
                    polyRowBrightness[idx][r] = 0.0f;

                if (std::abs (prevRow - polyRowBrightness[idx][r]) > 0.002f)
                    changed = true;
            }
        }

        if (changed)
            repaint();
    }

    void resized() override
    {
        rebuildBackgroundCache();
    }

    void paint (juce::Graphics& g) override
    {
        if (backgroundCache.isValid())
            g.drawImageAt (backgroundCache, 0, 0);

        if (!pointsValid)
            return;

        if (isPolyMode)
        {
            drawPolytuneColumns (g);
            return;
        }

        if (currentAlpha < 0.005f)
            return;

        constexpr float dotRadius = 3.9f;
        const juce::Colour outerGreen (100, 255, 65);
        const juce::Colour innerCyan  (70, 215, 255);

        const float absCents  = std::abs (currentCents);
        const int fastGapLen  = (absCents >= 16.0f) ? ((absCents >= 35.0f) ? 3 : 2) : 1;
        const int fastSegLen  = (absCents >= 16.0f) ? ((absCents >= 35.0f) ? 4 : 5) : 6;

        // Верхние 3 ряда
        const int topSeg = isSlowAboveCurrent ? 6 : fastSegLen;
        const int topGap = isSlowAboveCurrent ? 1 : fastGapLen;
        for (int row = 0; row < 3; ++row)
            drawActiveRow (g, row, dotRadius, topAngle, outerGreen, topSeg, topGap);

        // Нижние 2 ряда
        const int btmSeg = isSlowAboveCurrent ? fastSegLen : 6;
        const int btmGap = isSlowAboveCurrent ? fastGapLen : 1;
        for (int row = 3; row < 5; ++row)
            drawActiveRow (g, row, dotRadius, bottomAngle, innerCyan, btmSeg, btmGap);
    }

private:
    struct PointXY
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct PolyGridLayout
    {
        int startCol = 0;
        int colWidth = 3;
    };

    [[nodiscard]] static constexpr PolyGridLayout getPolyLayout (int numStrings) noexcept
    {
        switch (numStrings)
        {
            case 4:  return { 0, 5 }; // 4 столбца 5x5 через 1 пропуск = 23 (0..4, 6..10, 12..16, 18..22)
            case 5:  return { 2, 3 }; // 5 столбцов 3x5 через 1 пропуск, отступ 2 слева и 2 справа (2..20)
            case 6:  return { 0, 3 }; // 6 столбцов 3x5 через 1 пропуск = 23 (0..2, 4..6, 8..10, 12..14, 16..18, 20..22)
            case 7:  return { 1, 2 }; // 7 столбцов 2x5 через 1 пропуск, отступ 1 слева и 2 справа (1..20)
            case 8:  return { 0, 2 }; // 8 столбцов 2x5 через 1 пропуск = 23 (0..1, 3..4, ..., 21..22)
            default: return { 0, 3 };
        }
    }

    // Непрерывный расчёт яркости (накала) 5 полос столбца с учётом порога Polytune, cent:
    // - Внутри допуска Polytune, cent (с мягким гистерезисом): горит ТОЛЬКО центральная зелёная полоса (row 2 = 1.0), красные = 0.0.
    // - За пределами допуска Polytune, cent: плавный перелив накала (crossfade) от зелёной (row 2) к средней красной (row 1 / row 3),
    //   и далее при сильном отклонении — плавный перелив от средней красной к крайней красной (row 0 / row 4).
    static void computeRowTargetBrightness (
        float cents,
        float polyTolerance,
        bool& inTuneState,
        std::array<float, kNumRows>& outRows) noexcept
    {
        outRows = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        const float absC = std::abs (cents);
        // Полуширина чисто зелёной зоны соответствует настройке Polytune, cent (как In tune в соло-режиме),
        // с небольшим гистерезисом на выход, чтобы на границе не моргала красная полоса
        const float halfTol   = std::max (0.5f, polyTolerance * 0.55f);
        const float exitLimit = halfTol + std::max (0.35f, halfTol * 0.25f);

        if (inTuneState)
            inTuneState = (absC <= exitLimit);
        else
            inTuneState = (absC <= halfTol);

        if (inTuneState)
        {
            outRows[2] = 1.0f; // Чисто зелёная центральная полоса
            return;
        }

        // Начало плавного зажигания соседней красной полосы сразу за границей чисто зелёной зоны
        const float over = std::max (0.0f, absC - halfTol);
        const std::size_t midRow   = (cents >= 0.0f) ? 1u : 3u; // row 1 = высит, row 3 = низит
        const std::size_t outerRow = (cents >= 0.0f) ? 0u : 4u; // row 0 = сильно высит, row 4 = сильно низит

        constexpr float halfPi = std::numbers::pi_v<float> * 0.5f;

        // Диапазон 1: плавный перелив накала Зелёная (row 2) <-> Средняя Красная (row 1 / row 3)
        const float spanGreenToMid = std::max (4.2f, polyTolerance * 1.65f);
        // Участок чистого горения средней красной полосы
        const float spanMidHold    = std::max (1.5f, polyTolerance * 0.50f);
        // Диапазон 2: плавный перелив накала Средняя Красная (row 1 / row 3) <-> Крайняя Красная (row 0 / row 4)
        const float spanMidToOuter = std::max (9.0f, polyTolerance * 2.80f);

        if (over <= spanGreenToMid)
        {
            const float t = std::clamp (over / spanGreenToMid, 0.0f, 1.0f);
            // При малом выходе за допуск зелёный диод остаётся ярким, а красный плавно разгорается от тусклого к яркому
            outRows[2]      = std::cos (t * halfPi);
            outRows[midRow] = std::sin (t * halfPi);
        }
        else if (over <= spanGreenToMid + spanMidHold)
        {
            outRows[midRow] = 1.0f;
        }
        else if (over <= spanGreenToMid + spanMidHold + spanMidToOuter)
        {
            const float u = std::clamp ((over - spanGreenToMid - spanMidHold) / spanMidToOuter, 0.0f, 1.0f);
            outRows[midRow]   = std::cos (u * halfPi);
            outRows[outerRow] = std::sin (u * halfPi);
        }
        else
        {
            outRows[outerRow] = 1.0f;
        }
    }

    std::array<PointXY, kNumPoints> cachedPoints {};
    bool pointsValid = false;
    juce::Image backgroundCache;

    float topAngle    = 0.0f;
    float bottomAngle = 0.0f;
    float currentCents = 0.0f;
    float currentAlpha = 0.0f;
    float targetAlpha  = 0.0f;
    bool isSlowAboveCurrent = true;

    // Состояние полифонического отображения (Polytune) с индивидуальным накалом каждой полосы
    bool isPolyMode            = false;
    int currentPolyStrings     = 6;
    float currentPolyTolerance = 2.0f;
    std::array<float, kMaxStrings> polyColCents {};
    std::array<float, kMaxStrings> polyColAlpha {};
    std::array<bool, kMaxStrings>  polyColInTune {};
    std::array<std::array<float, kNumRows>, kMaxStrings> polyRowBrightness {};

    void rebuildBackgroundCache()
    {
        const auto bounds = getLocalBounds();
        if (bounds.isEmpty()) return;

        backgroundCache = juce::Image (juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
        juce::Graphics g (backgroundCache);

        const float fW = static_cast<float> (bounds.getWidth());
        const float fH = static_cast<float> (bounds.getHeight());
        const float centerX = fW * 0.5f;
        const float centerY = fH * 2.8f + 5.0f;

        constexpr float startAngle = juce::MathConstants<float>::pi * 1.368f;
        constexpr float endAngle   = juce::MathConstants<float>::pi * 1.632f;
        constexpr float angleStep  = (endAngle - startAngle) / static_cast<float> (kNumCols - 1);

        constexpr float dotR = 3.9f;
        const float baseR    = (fH * 2.8f) - 10.0f;
        const std::array<float, kNumRows> radii = {
            baseR, baseR - 10.0f, baseR - 20.0f, baseR - 30.0f, baseR - 40.0f
        };

        const juce::Colour offCenter (28, 30, 36);
        const juce::Colour offEdge   (14, 15, 18);

        for (int row = 0; row < kNumRows; ++row)
        {
            const float r = radii[static_cast<std::size_t> (row)];
            for (int i = 0; i < kNumCols; ++i)
            {
                const float a  = startAngle + static_cast<float> (i) * angleStep;
                const float px = centerX + std::cos (a) * r;
                const float py = centerY + std::sin (a) * r;

                cachedPoints[static_cast<std::size_t> (row * kNumCols + i)] = { px, py };

                juce::ColourGradient offGrad (offCenter, px, py - dotR * 0.3f,
                                              offEdge, px, py + dotR, true);
                g.setGradientFill (offGrad);
                g.fillEllipse (px - dotR, py - dotR, dotR * 2.0f, dotR * 2.0f);
            }
        }
        pointsValid = true;
    }

    void drawLitLed (juce::Graphics& g, float px, float py, float dotR, juce::Colour baseColour, float brightness) const
    {
        juce::ColourGradient glow (baseColour.withAlpha (brightness * 0.38f), px, py,
                                   baseColour.withAlpha (0.0f), px, py + dotR * 1.6f, true);
        g.setGradientFill (glow);
        g.fillEllipse (px - dotR * 1.6f, py - dotR * 1.6f, dotR * 3.2f, dotR * 3.2f);

        g.setColour (baseColour.withAlpha (brightness));
        g.fillEllipse (px - dotR, py - dotR, dotR * 2.0f, dotR * 2.0f);

        g.setColour (juce::Colours::white.withAlpha (brightness * 0.7f));
        g.fillEllipse (px - dotR * 0.45f, py - dotR * 0.45f, dotR * 0.9f, dotR * 0.9f);
    }

    void drawPolytuneColumns (juce::Graphics& g)
    {
        constexpr float dotR = 3.9f;
        const juce::Colour polyGreen (65, 255, 30);
        const juce::Colour polyRed   (255, 25, 15);

        const auto [startCol, colWidth] = getPolyLayout (currentPolyStrings);
        const int stride = colWidth + 1;

        for (int s = 0; s < currentPolyStrings; ++s)
        {
            const auto idx = static_cast<std::size_t> (s);
            if (polyColAlpha[idx] <= 0.005f)
                continue;

            const int col0 = startCol + s * stride;
            for (int r = 0; r < kNumRows; ++r)
            {
                const float rowBright = polyRowBrightness[idx][static_cast<std::size_t> (r)];
                if (rowBright <= 0.015f)
                    continue;

                const juce::Colour col = (r == 2) ? polyGreen : polyRed;
                const PointXY* rowPts  = &cachedPoints[static_cast<std::size_t> (r * kNumCols)];

                for (int c = 0; c < colWidth; ++c)
                {
                    const int gridCol = col0 + c;
                    if (gridCol >= 0 && gridCol < kNumCols)
                    {
                        const auto& pt = rowPts[gridCol];
                        drawLitLed (g, pt.x, pt.y, dotR, col, rowBright);
                    }
                }
            }
        }
    }

    void drawActiveRow (juce::Graphics& g, int rowIdx, float dotR,
                        float offsetA, juce::Colour baseColour, int segmentLen, int gapLen)
    {
        const int period = segmentLen + gapLen;
        const PointXY* rowPts = &cachedPoints[static_cast<std::size_t> (rowIdx * kNumCols)];

        for (int i = 0; i < kNumCols; ++i)
        {
            const float shiftedIdx = static_cast<float> (i) + (offsetA * 3.5f);
            float modIdx = std::fmod (shiftedIdx, static_cast<float> (period));
            if (modIdx < 0.0f)
                modIdx += static_cast<float> (period);

            if (modIdx < static_cast<float> (segmentLen))
            {
                const float normPos = modIdx / (static_cast<float> (segmentLen) - 1.0f);
                float brightness = std::sin (normPos * juce::MathConstants<float>::pi);
                brightness = juce::jlimit (0.35f, 1.0f, std::pow (brightness, 0.7f)) * currentAlpha;

                if (brightness > 0.03f)
                {
                    const float px = rowPts[i].x;
                    const float py = rowPts[i].y;
                    drawLitLed (g, px, py, dotR, baseColour, brightness);
                }
            }
        }
    }
};