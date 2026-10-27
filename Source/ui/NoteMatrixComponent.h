#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <cstdint>

class NoteMatrixComponent : public juce::Component
{
public:
    NoteMatrixComponent() = default;

    void setNote (char noteName, bool isSharp, int octaveNum, bool active,
                  float cents, float inTuneTolerance = 2.0f, bool showOctave = true) noexcept
    {
        targetAlpha = active ? 1.0f : 0.0f;

        const float halfTol  = inTuneTolerance * 0.5f;
        const float absCents = std::abs (cents);

        if (targetInTune)
            targetInTune = (absCents <= halfTol * 1.15f);
        else
            targetInTune = (absCents <= halfTol);

        const char prevNote   = currentNote;
        const bool prevSharp  = sharpActive;
        const int  prevOct    = currentOctave;
        const bool prevOctVis = octaveVisible;

        octaveVisible = showOctave;

        if (active)
        {
            currentNote   = noteName;
            sharpActive   = isSharp;
            currentOctave = octaveNum;
        }
        else if (currentAlpha < 0.02f)
        {
            currentNote = ' ';
            sharpActive = false;
        }

        const float prevAlpha = currentAlpha;
        if (currentAlpha < targetAlpha)
            currentAlpha += (targetAlpha - currentAlpha) * 0.18f;
        else
            currentAlpha += (targetAlpha - currentAlpha) * 0.055f;

        if (currentAlpha < 0.002f && targetAlpha == 0.0f)
            currentAlpha = 0.0f;

        const float prevMorph   = colourMorph;
        const float targetMorph = (targetInTune && active) ? 1.0f : 0.0f;
        colourMorph += (targetMorph - colourMorph) * 0.22f;
        if (std::abs (colourMorph - targetMorph) < 0.002f)
            colourMorph = targetMorph;

        // Перерисовываем ТОЛЬКО если состояние реально изменилось
        const bool stateChanged = (prevNote != currentNote) ||
                                  (prevSharp != sharpActive) ||
                                  (prevOct != currentOctave) ||
                                  (prevOctVis != octaveVisible) ||
                                  (std::abs (prevAlpha - currentAlpha) > 0.002f) ||
                                  (std::abs (prevMorph - colourMorph) > 0.002f);

        if (stateChanged)
            repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        if (bounds.isEmpty()) return;

        constexpr float bloomMargin = 16.0f;
        const auto drawArea = bounds.reduced (bloomMargin, bloomMargin);

        // 1. Исходные параметры геометрии ноты 5x4
        const float stepY     = drawArea.getHeight() / 5.0f;
        const float stepX     = stepY * 0.95f;
        const float ledRadius = stepY * 0.38f;

        // Нота центрируется строго по горизонтали
        const float noteStartX = bounds.getCentreX() - (1.5f * stepX);
        const float noteStartY = drawArea.getY() + stepY * 0.5f;

        // 2. Параметры диеза (5x5) и октавы (5x3) с уменьшенным диаметром
        const float subRadius   = ledRadius * 0.48f;
        const float subStep     = stepY * 0.50f;
        const float slantFactor = 0.10f * stepX; // легкий курсивный наклон строк диеза

        const float rightBlockX = noteStartX + 4.30f * stepX;
        const float sharpStartY = noteStartY - 1.2f * stepY;
        const float octStartY   = noteStartY + 3.2f * stepY;

        const auto* pattern    = getPatternForNote (currentNote);
        const auto* octPattern = getDigitMask (currentOctave);

        // --- ЭТАП 1. ШИРОКИЙ БЛУМ (WIDE BLOOM) ---
        if (currentAlpha > 0.02f)
        {
            for (int row = 0; row < 5; ++row)
            {
                const std::uint8_t rowBits = (pattern != nullptr) ? (*pattern)[static_cast<std::size_t> (row)] : 0;
                for (int col = 0; col < 4; ++col)
                {
                    if ((rowBits & (1u << (3 - col))) != 0)
                    {
                        const float cx = noteStartX + static_cast<float> (col) * stepX;
                        const float cy = noteStartY + static_cast<float> (row) * stepY;
                        drawWideBloom (g, cx, cy, ledRadius, currentAlpha, colourMorph);
                    }
                }
            }

            if (sharpActive)
            {
                for (int row = 0; row < 5; ++row)
                {
                    const float slantOffset = static_cast<float> (4 - row) * slantFactor;
                    for (int col = 0; col < 5; ++col)
                    {
                        if ((kSharpMask[static_cast<std::size_t> (row)] & (1u << (4 - col))) != 0)
                        {
                            const float cx = rightBlockX + static_cast<float> (col) * subStep + slantOffset;
                            const float cy = sharpStartY + static_cast<float> (row) * subStep;
                            drawWideBloom (g, cx, cy, subRadius, currentAlpha, colourMorph);
                        }
                    }
                }
            }

            if (octaveVisible && currentNote != ' ' && octPattern != nullptr)
            {
                for (int row = 0; row < 5; ++row)
                {
                    for (int col = 0; col < 3; ++col)
                    {
                        if (((*octPattern)[static_cast<std::size_t> (row)] & (1u << (2 - col))) != 0)
                        {
                            const float cx = rightBlockX + 0.9f * subStep + static_cast<float> (col) * subStep;
                            const float cy = octStartY + static_cast<float> (row) * subStep;
                            drawWideBloom (g, cx, cy, subRadius, currentAlpha, colourMorph);
                        }
                    }
                }
            }

            // --- ЭТАП 2. ОРЕОЛ СРЕДНЕЙ ДАЛЬНОСТИ (TIGHT HALO) ---
            for (int row = 0; row < 5; ++row)
            {
                const std::uint8_t rowBits = (pattern != nullptr) ? (*pattern)[static_cast<std::size_t> (row)] : 0;
                for (int col = 0; col < 4; ++col)
                {
                    if ((rowBits & (1u << (3 - col))) != 0)
                    {
                        const float cx = noteStartX + static_cast<float> (col) * stepX;
                        const float cy = noteStartY + static_cast<float> (row) * stepY;
                        drawTightHalo (g, cx, cy, ledRadius, currentAlpha, colourMorph);
                    }
                }
            }

            if (sharpActive)
            {
                for (int row = 0; row < 5; ++row)
                {
                    const float slantOffset = static_cast<float> (4 - row) * slantFactor;
                    for (int col = 0; col < 5; ++col)
                    {
                        if ((kSharpMask[static_cast<std::size_t> (row)] & (1u << (4 - col))) != 0)
                        {
                            const float cx = rightBlockX + static_cast<float> (col) * subStep + slantOffset;
                            const float cy = sharpStartY + static_cast<float> (row) * subStep;
                            drawTightHalo (g, cx, cy, subRadius, currentAlpha, colourMorph);
                        }
                    }
                }
            }

            if (octaveVisible && currentNote != ' ' && octPattern != nullptr)
            {
                for (int row = 0; row < 5; ++row)
                {
                    for (int col = 0; col < 3; ++col)
                    {
                        if (((*octPattern)[static_cast<std::size_t> (row)] & (1u << (2 - col))) != 0)
                        {
                            const float cx = rightBlockX + 0.9f * subStep + static_cast<float> (col) * subStep;
                            const float cy = octStartY + static_cast<float> (row) * subStep;
                            drawTightHalo (g, cx, cy, subRadius, currentAlpha, colourMorph);
                        }
                    }
                }
            }
        }

        // --- ЭТАП 3. ТЕЛА ДИОДОВ (LED BODIES) ---
        for (int row = 0; row < 5; ++row)
        {
            const std::uint8_t rowBits = (pattern != nullptr) ? (*pattern)[static_cast<std::size_t> (row)] : 0;
            for (int col = 0; col < 4; ++col)
            {
                const bool active = (rowBits & (1u << (3 - col))) != 0;
                const float cx = noteStartX + static_cast<float> (col) * stepX;
                const float cy = noteStartY + static_cast<float> (row) * stepY;

                drawLedBody (g, cx, cy, ledRadius, active, currentAlpha, colourMorph);
            }
        }

        for (int row = 0; row < 5; ++row)
        {
            const float slantOffset = static_cast<float> (4 - row) * slantFactor;
            for (int col = 0; col < 5; ++col)
            {
                const bool active = sharpActive && ((kSharpMask[static_cast<std::size_t> (row)] & (1u << (4 - col))) != 0);
                const float cx = rightBlockX + static_cast<float> (col) * subStep + slantOffset;
                const float cy = sharpStartY + static_cast<float> (row) * subStep;

                drawLedBody (g, cx, cy, subRadius, active, currentAlpha, colourMorph);
            }
        }

        if (octaveVisible)
        {
            for (int row = 0; row < 5; ++row)
            {
                for (int col = 0; col < 3; ++col)
                {
                    const bool active = (currentNote != ' ') && (octPattern != nullptr) &&
                                        (((*octPattern)[static_cast<std::size_t> (row)] & (1u << (2 - col))) != 0);
                    const float cx = rightBlockX + 0.9f * subStep + static_cast<float> (col) * subStep;
                    const float cy = octStartY + static_cast<float> (row) * subStep;

                    drawLedBody (g, cx, cy, subRadius, active, currentAlpha, colourMorph);
                }
            }
        }
    }

private:
    using Mask5 = std::array<std::uint8_t, 5>;

    static constexpr Mask5 kSharpMask = {
        0b01010,
        0b11111,
        0b01010,
        0b11111,
        0b01010
    };

    static constexpr std::array<Mask5, 10> kDigitMasks = {{
        { 0b111, 0b101, 0b101, 0b101, 0b111 }, // 0
        { 0b010, 0b110, 0b010, 0b010, 0b111 }, // 1
        { 0b111, 0b001, 0b111, 0b100, 0b111 }, // 2
        { 0b111, 0b001, 0b111, 0b001, 0b111 }, // 3
        { 0b101, 0b101, 0b111, 0b001, 0b001 }, // 4
        { 0b111, 0b100, 0b111, 0b001, 0b111 }, // 5
        { 0b111, 0b100, 0b111, 0b101, 0b111 }, // 6
        { 0b111, 0b001, 0b010, 0b010, 0b010 }, // 7
        { 0b111, 0b101, 0b111, 0b101, 0b111 }, // 8
        { 0b111, 0b101, 0b111, 0b001, 0b111 }  // 9
    }};

    static constexpr std::array<Mask5, 7> kNotePatterns = {{
        { 0b0110, 0b1001, 0b1111, 0b1001, 0b1001 }, // A
        { 0b1110, 0b1001, 0b1110, 0b1001, 0b1110 }, // B
        { 0b0111, 0b1000, 0b1000, 0b1000, 0b0111 }, // C
        { 0b1110, 0b1001, 0b1001, 0b1001, 0b1110 }, // D
        { 0b1111, 0b1000, 0b1110, 0b1000, 0b1111 }, // E
        { 0b1111, 0b1000, 0b1110, 0b1000, 0b1000 }, // F
        { 0b0111, 0b1000, 0b1011, 0b1001, 0b0111 }  // G
    }};

    char currentNote = ' ';
    bool sharpActive = false;
    int currentOctave = 4;
    bool octaveVisible = true;

    float currentAlpha = 0.0f;
    float targetAlpha = 0.0f;

    bool targetInTune = false;
    float colourMorph = 0.0f; // 0.0 = Красный, 1.0 = Зеленый

    void drawWideBloom (juce::Graphics& g, float cx, float cy, float r, float a, float morph)
    {
        const juce::Colour redBloom (255, 30, 25);
        const juce::Colour greenBloom (60, 220, 45);
        const juce::Colour baseBloom = redBloom.interpolatedWith (greenBloom, morph);

        const float bloomR = r * 3.2f;
        const float bloomAlpha = juce::jmap (morph, 0.40f, 0.34f) * a;

        juce::ColourGradient grad (baseBloom.withAlpha (bloomAlpha), cx, cy,
                                   baseBloom.withAlpha (0.0f), cx, cy + bloomR, true);
        g.setGradientFill (grad);
        g.fillEllipse (cx - bloomR, cy - bloomR, bloomR * 2.0f, bloomR * 2.0f);
    }

    void drawTightHalo (juce::Graphics& g, float cx, float cy, float r, float a, float morph)
    {
        const juce::Colour redNeon (255, 45, 35);
        const juce::Colour greenNeon (85, 230, 55);
        const juce::Colour baseNeon = redNeon.interpolatedWith (greenNeon, morph);

        const float haloR = r * 1.85f;
        const float haloAlpha = juce::jmap (morph, 0.55f, 0.46f) * a;

        juce::ColourGradient grad (baseNeon.withAlpha (haloAlpha), cx, cy,
                                   baseNeon.withAlpha (0.0f), cx, cy + haloR, true);
        g.setGradientFill (grad);
        g.fillEllipse (cx - haloR, cy - haloR, haloR * 2.0f, haloR * 2.0f);
    }

    void drawLedBody (juce::Graphics& g, float cx, float cy, float r, bool active, float a, float morph)
    {
        juce::Colour offCenter (28, 30, 36);
        juce::Colour offEdge   (14, 15, 18);
        juce::ColourGradient offGrad (offCenter, cx, cy - r * 0.3f,
                                      offEdge, cx, cy + r, true);
        g.setGradientFill (offGrad);
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);

        g.setColour (juce::Colour (40, 44, 52).withAlpha (0.45f));
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 0.6f);

        if (active && a > 0.01f)
        {
            const juce::Colour redCenter (255, 110, 95);
            const juce::Colour redEdge   (225, 20, 20);

            const juce::Colour greenCenter (71, 216, 6);
            const juce::Colour greenEdge   (69, 154, 32);

            const juce::Colour curCenter = redCenter.interpolatedWith (greenCenter, morph).withAlpha (a);
            const juce::Colour curEdge   = redEdge.interpolatedWith (greenEdge, morph).withAlpha (0.90f * a);

            juce::ColourGradient coreGrad (curCenter, cx, cy - r * 0.2f,
                                           curEdge, cx, cy + r * 1.15f, true);
            g.setGradientFill (coreGrad);
            g.fillEllipse (cx - r * 1.12f, cy - r * 1.12f, r * 2.24f, r * 2.24f);

            const float hotAlpha = juce::jmap (morph, 0.95f, 0.75f) * a;
            const juce::Colour hotCol = juce::Colour (255, 255, 240).withAlpha (hotAlpha);

            juce::ColourGradient hotGrad (hotCol, cx, cy,
                                          hotCol.withAlpha (0.0f), cx, cy + r * (0.9f + 0.1f * morph), true);
            g.setGradientFill (hotGrad);
            g.fillEllipse (cx - r * 0.7f, cy - r * 0.7f, r * 1.4f, r * 1.4f);
        }
    }

    [[nodiscard]] static constexpr const Mask5* getDigitMask (int digit) noexcept
    {
        if (digit >= 0 && digit <= 9)
            return &kDigitMasks[static_cast<std::size_t> (digit)];
        return nullptr;
    }

    [[nodiscard]] static constexpr const Mask5* getPatternForNote (char note) noexcept
    {
        if (note >= 'A' && note <= 'G')
            return &kNotePatterns[static_cast<std::size_t> (note - 'A')];
        return nullptr;
    }
};