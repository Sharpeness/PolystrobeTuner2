#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>

class GuitarBassSwitchComponent : public juce::Component
{
public:
    std::function<void(bool)> onModeChange; // false = Guitar, true = Bass

    GuitarBassSwitchComponent() noexcept = default;

    void setBassMode (bool bass, bool notify = true)
    {
        if (isBass != bass)
        {
            isBass = bass;
            if (notify && onModeChange)
                onModeChange (isBass);
            repaint();
        }
    }

    [[nodiscard]] bool isBassMode() const noexcept { return isBass; }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        if (bounds.isEmpty()) return;

        const float midY       = bounds.getCentreY();
        constexpr float trackW = 36.0f;
        constexpr float trackH = 14.0f;
        const float trackX     = bounds.getCentreX() - trackW * 0.5f + 2.0f;
        const float trackY     = midY - trackH * 0.5f;
        const juce::Rectangle<float> trackRect (trackX, trackY, trackW, trackH);

        // 1. Надписи "Guitar" (слева) и "Bass" (справа)
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));

        g.setColour (!isBass ? juce::Colour (50, 55, 65) : juce::Colour (125, 130, 140));
        g.drawText ("Guitar", 0.0f, 0.0f, trackX - 5.0f, bounds.getHeight(),
                    juce::Justification::centredRight, false);

        g.setColour (isBass ? juce::Colour (50, 55, 65) : juce::Colour (125, 130, 140));
        g.drawText ("Bass", trackRect.getRight() + 6.0f, 0.0f, bounds.getWidth() - trackRect.getRight() - 6.0f, bounds.getHeight(),
                    juce::Justification::centredLeft, false);

        // 2. Внешний 3D металлический обод гнезда переключателя
        const juce::Rectangle<float> outerBezel = trackRect.expanded (1.8f);
        juce::ColourGradient bezelGrad (juce::Colour (130, 135, 145), outerBezel.getX(), outerBezel.getY(),
                                        juce::Colour (248, 250, 252), outerBezel.getX(), outerBezel.getBottom(), false);
        g.setGradientFill (bezelGrad);
        g.fillRoundedRectangle (outerBezel, (trackH + 3.6f) * 0.5f);

        g.setColour (juce::Colour (105, 110, 120));
        g.drawRoundedRectangle (outerBezel, (trackH + 3.6f) * 0.5f, 0.8f);

        // 3. Внутренний углубленный паз (с верхней внутренней тенью)
        juce::ColourGradient slotGrad (juce::Colour (145, 150, 160), trackX, trackY,
                                       juce::Colour (215, 220, 228), trackX, trackY + trackH, false);
        g.setGradientFill (slotGrad);
        g.fillRoundedRectangle (trackRect, trackH * 0.5f);

        g.setColour (juce::Colour (75, 80, 90).withAlpha (0.6f));
        g.drawRoundedRectangle (trackRect, trackH * 0.5f, 1.0f);

        // 4. Металлический 3D-ползунок (штифт тумблера)
        constexpr float thumbR = (trackH - 2.6f) * 0.5f;
        const float leftX      = trackX + thumbR + 1.6f;
        const float rightX     = trackRect.getRight() - thumbR - 1.6f;
        const float thumbX     = isBass ? rightX : leftX;
        const float thumbY     = midY;

        // Тень от ползунка внутри паза
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillEllipse (thumbX - thumbR + (isBass ? -1.2f : 1.2f), thumbY - thumbR + 0.8f, thumbR * 2.0f, thumbR * 2.0f);

        // Точеное тело ползунка
        juce::ColourGradient thumbGrad (juce::Colour (245, 248, 252), thumbX - thumbR * 0.6f, thumbY - thumbR * 0.6f,
                                        juce::Colour (95, 102, 115),  thumbX + thumbR * 0.7f, thumbY + thumbR * 0.7f, false);
        thumbGrad.addColour (0.5, juce::Colour (175, 182, 192));
        g.setGradientFill (thumbGrad);
        g.fillEllipse (thumbX - thumbR, thumbY - thumbR, thumbR * 2.0f, thumbR * 2.0f);

        // Центральная вогнутая/выпуклая риска-шапочка на ползунке
        constexpr float capR = thumbR * 0.62f;
        juce::ColourGradient capGrad (juce::Colour (140, 146, 158), thumbX - capR, thumbY - capR,
                                      juce::Colour (235, 238, 244), thumbX + capR, thumbY + capR, false);
        g.setGradientFill (capGrad);
        g.fillEllipse (thumbX - capR, thumbY - capR, capR * 2.0f, capR * 2.0f);

        g.setColour (juce::Colour (70, 75, 85));
        g.drawEllipse (thumbX - thumbR, thumbY - thumbR, thumbR * 2.0f, thumbR * 2.0f, 0.7f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (!getLocalBounds().contains (e.getPosition()))
            return;

        const float midX = static_cast<float> (getWidth()) * 0.5f;
        if (e.position.x < midX - 18.0f)
            setBassMode (false);
        else if (e.position.x > midX + 22.0f)
            setBassMode (true);
        else
            setBassMode (!isBass);
    }

private:
    bool isBass = false;
};