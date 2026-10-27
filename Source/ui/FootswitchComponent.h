#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include <numbers>

class FootswitchComponent : public juce::Component
{
public:
    std::function<void(bool)> onToggle;

    FootswitchComponent() noexcept = default;

    void setMuted (bool muted, bool animate = true) noexcept
    {
        isMutedState = muted;
        if (!animate)
            glowAlpha = muted ? 1.0f : 0.0f;
        repaint();
    }

    [[nodiscard]] bool isMuted() const noexcept { return isMutedState; }

    void updateAnimation() noexcept
    {
        const float target = isMutedState ? 1.0f : 0.0f;
        if (std::abs (glowAlpha - target) > 0.005f)
        {
            glowAlpha += (target - glowAlpha) * 0.22f;
            repaint();
        }
        else if (glowAlpha != target)
        {
            glowAlpha = target;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        if (bounds.isEmpty()) return;

        const float midX = bounds.getCentreX();
        const float midY = bounds.getY() + 42.0f;
        constexpr float outerRadius = 19.5f;

        // 1. Надпись "mute"
        g.setColour (juce::Colour (35, 38, 45));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("mute", bounds.getX(), bounds.getY() + 1.0f, bounds.getWidth(), 15.0f,
                    juce::Justification::centred, false);

        // 2. РУБИНОВЫЙ ОРЕОЛ (ФИНАЛЬНАЯ КАЛИБРОВКА)
        if (glowAlpha > 0.01f)
        {
            constexpr int passes        = 11;
            constexpr float maxSpread   = 5.0f;
            constexpr float pi          = std::numbers::pi_v<float>;

            for (int i = passes; i >= 1; --i)
            {
                const float t       = static_cast<float> (i) / static_cast<float> (passes);
                const float r       = outerRadius + t * maxSpread;
                const float profile = 0.5f * (1.0f + std::cos (t * pi));

                const juce::Colour ringCol = juce::Colour (245, 10, 20).interpolatedWith (juce::Colour (175, 0, 10), t);
                const float alpha          = profile * 0.12f * glowAlpha;

                g.setColour (ringCol.withAlpha (alpha));
                g.drawEllipse (midX - r, midY - r, r * 2.0f, r * 2.0f, 1.1f);
            }

            // Плотная основа световода у металла
            for (int i = 0; i < 3; ++i)
            {
                const float r         = outerRadius + 0.4f + static_cast<float> (i) * 0.45f;
                const float coreAlpha = (0.80f - static_cast<float> (i) * 0.20f) * glowAlpha;
                g.setColour (juce::Colour (255, 30, 25).withAlpha (coreAlpha));
                g.drawEllipse (midX - r, midY - r, r * 2.0f, r * 2.0f, 1.0f);
            }
        }

        // 3. Тень от металлической шайбы
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (midX - outerRadius, midY - outerRadius + 1.8f, outerRadius * 2.0f, outerRadius * 2.0f);

        // 4. МАССИВНЫЙ ХРОМИРОВАННЫЙ ОБОД
        juce::ColourGradient rimGrad (juce::Colour (245, 247, 250), midX - outerRadius * 0.7f, midY - outerRadius * 0.7f,
                                      juce::Colour (55, 60, 70),    midX + outerRadius * 0.7f, midY + outerRadius * 0.7f, false);
        rimGrad.addColour (0.25, juce::Colour (185, 190, 200));
        rimGrad.addColour (0.65, juce::Colour (95, 100, 110));
        rimGrad.addColour (0.85, juce::Colour (210, 215, 225));
        g.setGradientFill (rimGrad);
        g.fillEllipse (midX - outerRadius, midY - outerRadius, outerRadius * 2.0f, outerRadius * 2.0f);

        // Внутренний темный паз фаски (разделяет обод и поршень)
        constexpr float grooveR = outerRadius - 4.4f;
        g.setColour (juce::Colour (14, 16, 20));
        g.fillEllipse (midX - grooveR, midY - grooveR, grooveR * 2.0f, grooveR * 2.0f);

        // 5. ВНУТРЕННИЙ ПОРШЕНЬ
        constexpr float plungerR = grooveR - 1.2f;
        const float pushOffset   = isMouseDown ? 0.8f : 0.0f;

        juce::ColourGradient plungerGrad (juce::Colour (235, 238, 243), midX - plungerR * 0.35f, midY - plungerR * 0.45f + pushOffset,
                                          juce::Colour (155, 160, 170), midX + plungerR * 0.5f,  midY + plungerR * 0.6f  + pushOffset, false);
        plungerGrad.addColour (0.7, juce::Colour (175, 180, 190));
        g.setGradientFill (plungerGrad);
        g.fillEllipse (midX - plungerR, midY - plungerR + pushOffset, plungerR * 2.0f, plungerR * 2.0f);

        // Металлический блик на шапочке кнопки
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.drawEllipse (midX - plungerR + 1.0f, midY - plungerR + pushOffset + 0.6f,
                       (plungerR - 1.0f) * 2.0f, (plungerR - 1.4f) * 2.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        isMouseDown = true;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        isMouseDown = false;
        if (getLocalBounds().contains (e.getPosition()))
        {
            isMutedState = !isMutedState;
            if (onToggle)
                onToggle (isMutedState);
        }
        repaint();
    }

private:
    bool isMutedState = false;
    bool isMouseDown  = false;
    float glowAlpha   = 0.0f;
};