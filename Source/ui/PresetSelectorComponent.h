#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../dsp/TuningPresets.h"

class PresetSelectorComponent : public juce::Component
{
public:
    std::function<void(int)> onPresetSelected;
    std::function<void()>    onStarClicked;

    PresetSelectorComponent() = default;

    void updateState (bool bassMode, int presetIdx, int stringsCount, int capoShift, std::uint32_t favMask)
    {
        if (isBass != bassMode || currentPresetIdx != presetIdx || numStrings != stringsCount ||
            capoSemitones != capoShift || favoritesMask != favMask)
        {
            isBass           = bassMode;
            currentPresetIdx = presetIdx;
            numStrings       = stringsCount;
            capoSemitones    = capoShift;
            favoritesMask    = favMask;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        if (bounds.isEmpty()) return;

        // 1. Светлая скругленная плашка-контейнер пресета
        g.setColour (isBoxHovered ? juce::Colour (248, 250, 253) : juce::Colour (242, 244, 248));
        g.fillRoundedRectangle (bounds.reduced (0.5f), 4.5f);

        g.setColour (juce::Colour (150, 168, 195));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.5f, 1.0f);

        const auto presets = TuningPresets::getPresets (isBass);
        if (currentPresetIdx < 0 || static_cast<std::size_t> (currentPresetIdx) >= presets.size())
            return;

        const auto& preset = presets[static_cast<std::size_t> (currentPresetIdx)];

        // 1-й ряд: Название пресета и золотая звёздочка
        g.setColour (juce::Colour (25, 28, 34));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (TuningPresets::toJuceString (preset.name), 5, 1, getWidth() - 24, 15,
                    juce::Justification::centredLeft, true);

        drawStarIcon (g, bounds.getRight() - 10.5f, 8.5f, 5.2f, true, isStarHovered);

        // 2-й ряд: Плашки с названиями нот (6/7/8 для гитары или 4/5/6 для баса)
        const auto active = TuningPresets::getActiveStrings (preset, isBass, numStrings);

        const float padX   = 4.0f;
        const float gap    = (active.count >= 8) ? 1.5f : 2.2f;
        const float availW = bounds.getWidth() - padX * 2.0f - gap * static_cast<float> (active.count - 1);
        const float badgeW = availW / static_cast<float> (active.count);
        const float badgeH = 14.5f;
        const float badgeY = bounds.getBottom() - badgeH - 3.0f;

        g.setFont (juce::FontOptions (active.count >= 8 ? 10.4f : 10.5f, juce::Font::bold));

        for (int i = 0; i < active.count; ++i)
        {
            const int midi = active.midiNotes[static_cast<std::size_t> (i)] + capoSemitones;
            const juce::String noteStr = TuningPresets::midiToNoteName (midi);

            const float bx = padX + static_cast<float> (i) * (badgeW + gap);
            const juce::Rectangle<float> badgeRect (bx, badgeY, badgeW, badgeH);

            g.setColour (juce::Colour (62, 92, 138));
            g.fillRoundedRectangle (badgeRect, 2.8f);

            g.setColour (juce::Colour (42, 66, 104));
            g.drawRoundedRectangle (badgeRect, 2.8f, 0.7f);

            g.setColour (juce::Colours::white);
            g.drawText (noteStr, badgeRect, juce::Justification::centred, false);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool onStar = getStarBounds().contains (e.getPosition());
        if (onStar != isStarHovered || !isBoxHovered)
        {
            isStarHovered = onStar;
            isBoxHovered  = true;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        isStarHovered = false;
        isBoxHovered  = false;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (!getLocalBounds().contains (e.getPosition()))
            return;

        if (getStarBounds().contains (e.getPosition()))
        {
            if (onStarClicked)
                onStarClicked();
            return;
        }

        showFavoritesPopup();
    }

    static void drawStarIcon (juce::Graphics& g, float cx, float cy, float outerR, bool filled, bool hovered)
    {
        juce::Path star;
        const float innerR = outerR * 0.44f;
        for (int i = 0; i < 10; ++i)
        {
            const float r  = (i % 2 == 0) ? outerR : innerR;
            const float a  = static_cast<float> (i) * juce::MathConstants<float>::pi * 0.2f - juce::MathConstants<float>::halfPi;
            const float px = cx + std::cos (a) * r;
            const float py = cy + std::sin (a) * r;
            if (i == 0) star.startNewSubPath (px, py);
            else        star.lineTo (px, py);
        }
        star.closeSubPath();

        if (filled)
        {
            g.setColour (hovered ? juce::Colour (255, 225, 70) : juce::Colour (245, 195, 35));
            g.fillPath (star);
            g.setColour (juce::Colour (185, 135, 10));
            g.strokePath (star, juce::PathStrokeType (0.8f));
        }
        else
        {
            g.setColour (hovered ? juce::Colours::white : juce::Colour (130, 138, 152));
            g.strokePath (star, juce::PathStrokeType (1.2f));
        }
    }

private:
    bool isBass = false;
    int currentPresetIdx = 0;
    int numStrings = 6;
    int capoSemitones = 0;
    std::uint32_t favoritesMask = 0xFFFFFFFFu;

    bool isBoxHovered = false;
    bool isStarHovered = false;

    [[nodiscard]] juce::Rectangle<int> getStarBounds() const noexcept
    {
        return juce::Rectangle<int> (getWidth() - 21, 0, 21, 18);
    }

    void showFavoritesPopup()
    {
        juce::PopupMenu menu;
        const auto presets = TuningPresets::getPresets (isBass);

        for (std::size_t i = 0; i < presets.size(); ++i)
        {
            if ((favoritesMask & (1u << i)) != 0)
            {
                const auto active = TuningPresets::getActiveStrings (presets[i], isBass, numStrings);

                juce::String notesPreview;
                for (int s = 0; s < active.count; ++s)
                {
                    const int midi = active.midiNotes[static_cast<std::size_t> (s)] + capoSemitones;
                    notesPreview += TuningPresets::midiToNoteName (midi) + (s < active.count - 1 ? "-" : "");
                }

                const juce::String itemText = TuningPresets::toJuceString (presets[i].name) + "   [" + notesPreview + "]";
                menu.addItem (static_cast<int> (i) + 1, itemText, true, static_cast<int> (i) == currentPresetIdx);
            }
        }

        menu.addSeparator();
        menu.addItem (999, "Browse All Presets (★)...");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [this] (int result)
                            {
                                if (result == 999)
                                {
                                    if (onStarClicked) onStarClicked();
                                }
                                else if (result > 0)
                                {
                                    if (onPresetSelected)
                                        onPresetSelected (result - 1);
                                }
                            });
    }
};