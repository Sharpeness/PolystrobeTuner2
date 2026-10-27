#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include <numbers>

class SettingsButton : public juce::Component
{
public:
    std::function<void()> onClick;

    SettingsButton() noexcept = default;

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        if (bounds.isEmpty()) return;

        // Язычок с закругленными правыми углами
        juce::Path tabPath;
        constexpr float r      = 11.0f;
        constexpr float halfPi = std::numbers::pi_v<float> * 0.5f;
        constexpr float pi     = std::numbers::pi_v<float>;
        constexpr float twoPi  = std::numbers::pi_v<float> * 2.0f;

        tabPath.startNewSubPath (0.0f, 0.0f);
        tabPath.lineTo (bounds.getWidth() - r, 0.0f);
        tabPath.addArc (bounds.getWidth() - r * 2.0f, 0.0f, r * 2.0f, r * 2.0f, 0.0f, halfPi);
        tabPath.lineTo (bounds.getWidth(), bounds.getHeight() - r);
        tabPath.addArc (bounds.getWidth() - r * 2.0f, bounds.getHeight() - r * 2.0f, r * 2.0f, r * 2.0f, halfPi, pi);
        tabPath.lineTo (0.0f, bounds.getHeight());
        tabPath.closeSubPath();

        // Тело язычка
        g.setColour (isHovered ? juce::Colour (248, 249, 251) : juce::Colour (236, 238, 242));
        g.fillPath (tabPath);

        // Фаска контура
        g.setColour (juce::Colour (172, 177, 185));
        g.strokePath (tabPath, juce::PathStrokeType (1.2f));

        // Векторная шестерёнка
        const float cx         = bounds.getWidth() * 0.50f;
        const float cy         = bounds.getHeight() * 0.50f;
        constexpr float rOuter = 6.8f;
        constexpr float rInner = 4.8f;
        constexpr float holeR  = 2.4f;
        constexpr int numTeeth = 8;

        juce::Path gear;
        constexpr float angleStep = twoPi / static_cast<float> (numTeeth);

        for (int i = 0; i < numTeeth; ++i)
        {
            const float a0 = static_cast<float> (i) * angleStep;
            const float a1 = a0 + angleStep * 0.35f;
            const float a2 = a0 + angleStep * 0.55f;
            const float a3 = a0 + angleStep * 0.90f;

            if (i == 0)
                gear.startNewSubPath (cx + std::cos (a0) * rInner, cy + std::sin (a0) * rInner);
            else
                gear.lineTo (cx + std::cos (a0) * rInner, cy + std::sin (a0) * rInner);

            gear.lineTo (cx + std::cos (a1) * rOuter, cy + std::sin (a1) * rOuter);
            gear.lineTo (cx + std::cos (a2) * rOuter, cy + std::sin (a2) * rOuter);
            gear.lineTo (cx + std::cos (a3) * rInner, cy + std::sin (a3) * rInner);
        }
        gear.closeSubPath();

        gear.addEllipse (cx - holeR, cy - holeR, holeR * 2.0f, holeR * 2.0f);
        gear.setUsingNonZeroWinding (false);

        const juce::Colour gearCol = isHovered ? juce::Colour (90, 95, 105) : juce::Colour (140, 145, 155);
        g.setColour (gearCol);
        g.fillPath (gear);
    }

    void mouseEnter (const juce::MouseEvent&) override { isHovered = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { isHovered = false; repaint(); }
    void mouseUp    (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }

private:
    bool isHovered = false;
};