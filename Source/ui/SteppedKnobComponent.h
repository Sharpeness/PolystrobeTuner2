#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

class SteppedKnobComponent : public juce::Component
{
public:
    std::function<void(int)> onValueChange;

    SteppedKnobComponent (const juce::String& labelTitle, int minValue, int maxValue, int defaultValue, int stepSize = 1)
        : title (labelTitle), minVal (minValue), maxVal (maxValue),
          defaultVal (defaultValue), currentVal (defaultValue), stepVal (stepSize)
    {
    }

    void setRange (int newMin, int newMax, int newDefault) noexcept
    {
        minVal     = newMin;
        maxVal     = newMax;
        defaultVal = newDefault;
        currentVal = std::clamp (currentVal, minVal, maxVal);
        repaint();
    }

    void setValue (int newVal, bool notify = true)
    {
        const int snapped = minVal + static_cast<int> (std::lround (static_cast<float> (newVal - minVal) / static_cast<float> (stepVal))) * stepVal;
        const int clamped = std::clamp (snapped, minVal, maxVal);
        if (currentVal != clamped)
        {
            currentVal = clamped;
            if (notify && onValueChange)
                onValueChange (currentVal);
            repaint();
        }
    }

    [[nodiscard]] int getValue() const noexcept { return currentVal; }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        if (bounds.isEmpty()) return;

        // 1. Заголовок над ручкой ("Strings" / "Capo" / "Accuracy")
        g.setColour (juce::Colour (90, 95, 105));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, 0.0f, 0.0f, bounds.getWidth(), 14.0f, juce::Justification::centred, false);

        const float cx         = bounds.getCentreX();
        constexpr float cy     = 34.0f;
        constexpr float knobR  = 14.0f;
        constexpr float minAng = -std::numbers::pi_v<float> * 0.75f;
        constexpr float maxAng =  std::numbers::pi_v<float> * 0.75f;

        // 2. Боковые риски крайних положений (-135° и +135°)
        g.setColour (juce::Colour (80, 85, 95));
        for (const float tickA : { minAng, maxAng })
        {
            constexpr float r1 = knobR + 2.0f;
            constexpr float r2 = knobR + 5.2f;
            const float x1 = cx + std::sin (tickA) * r1;
            const float y1 = cy - std::cos (tickA) * r1;
            const float x2 = cx + std::sin (tickA) * r2;
            const float y2 = cy - std::cos (tickA) * r2;
            g.drawLine (x1, y1, x2, y2, 1.3f);
        }

        // 3. Падающая 3D-тень от ручки
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillEllipse (cx - knobR, cy - knobR + 2.0f, knobR * 2.0f, knobR * 2.0f);

        // 4. Внешняя точеная металлическая фаска
        juce::ColourGradient bevelGrad (juce::Colour (242, 245, 248), cx - knobR * 0.7f, cy - knobR * 0.7f,
                                        juce::Colour (95, 100, 110),  cx + knobR * 0.7f, cy + knobR * 0.7f, false);
        bevelGrad.addColour (0.45, juce::Colour (175, 180, 188));
        g.setGradientFill (bevelGrad);
        g.fillEllipse (cx - knobR, cy - knobR, knobR * 2.0f, knobR * 2.0f);

        g.setColour (juce::Colour (110, 115, 125));
        g.drawEllipse (cx - knobR, cy - knobR, knobR * 2.0f, knobR * 2.0f, 0.8f);

        // 5. Верхняя шлифованная грань ручки
        constexpr float capR = knobR - 2.0f;
        juce::ColourGradient capGrad (juce::Colour (228, 231, 236), cx - capR * 0.5f, cy - capR * 0.6f,
                                      juce::Colour (162, 167, 175), cx + capR * 0.5f, cy + capR * 0.6f, false);
        capGrad.addColour (0.5, juce::Colour (196, 200, 207));
        g.setGradientFill (capGrad);
        g.fillEllipse (cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.drawEllipse (cx - capR + 0.5f, cy - capR + 0.5f, (capR - 0.5f) * 2.0f, (capR - 0.5f) * 2.0f, 0.8f);

        // 6. Ступенчатый индикатор (точка-углубление)
        const float norm  = (maxVal > minVal)
                                ? static_cast<float> (currentVal - minVal) / static_cast<float> (maxVal - minVal)
                                : 0.0f;
        const float angle = minAng + norm * (maxAng - minAng);

        constexpr float dotDist = capR - 3.1f;
        const float dx          = cx + std::sin (angle) * dotDist;
        const float dy          = cy - std::cos (angle) * dotDist;
        constexpr float dotR    = 1.5f;

        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.fillEllipse (dx - dotR, dy - dotR + 0.6f, dotR * 2.0f, dotR * 2.0f);
        g.setColour (juce::Colour (40, 44, 52));
        g.fillEllipse (dx - dotR, dy - dotR, dotR * 2.0f, dotR * 2.0f);

        // 7. Текстовое значение под ручкой
        g.setColour (juce::Colour (45, 50, 58));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (juce::String (currentVal), 0.0f, 52.0f, bounds.getWidth(), 14.0f, juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartX     = e.position.x;
        dragStartY     = e.position.y;
        valAtDragStart = currentVal;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float delta = (e.position.x - dragStartX) - (e.position.y - dragStartY);
        const int steps   = static_cast<int> (std::lround (delta / 9.0f));
        setValue (valAtDragStart + steps * stepVal);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        setValue (defaultVal);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY > 0.0f)
            setValue (currentVal + stepVal);
        else if (wheel.deltaY < 0.0f)
            setValue (currentVal - stepVal);
    }

private:
    juce::String title;
    int minVal, maxVal, defaultVal, currentVal, stepVal;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
    int valAtDragStart = 0;
};