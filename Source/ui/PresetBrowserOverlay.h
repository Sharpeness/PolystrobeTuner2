#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../dsp/TuningPresets.h"
#include "PresetSelectorComponent.h"

class PresetBrowserOverlay : public juce::Component
{
public:
    std::function<void()>              onClose;
    std::function<void(int)>           onSelectPreset;
    std::function<void(std::uint32_t)> onFavoritesChanged;

    PresetBrowserOverlay()
    {
        backBtn.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (backBtn);

        viewport.setViewedComponent (&listContent, false);
        viewport.setScrollBarsShown (true, false);
        viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, juce::Colour (75, 85, 105));
        addAndMakeVisible (viewport);

        listContent.onRowClick = [this] (int idx)
        {
            selectedPresetIdx = idx;
            if (onSelectPreset) onSelectPreset (idx);
            listContent.repaint();
        };

        listContent.onStarToggle = [this] (int idx)
        {
            const std::uint32_t bit = (1u << static_cast<std::uint32_t> (idx));
            if ((favoritesMask & bit) != 0 && (favoritesMask & ~bit) == 0)
                return;

            favoritesMask ^= bit;
            listContent.favoritesMask = favoritesMask;
            if (onFavoritesChanged) onFavoritesChanged (favoritesMask);
            listContent.repaint();
        };
    }

    void syncState (bool bassMode, int presetIdx, int stringsCount, int capoShift, std::uint32_t favMask)
    {
        const bool modeChanged    = (listContent.isBass != bassMode);
        listContent.isBass        = bassMode;
        selectedPresetIdx         = presetIdx;
        listContent.selectedIdx   = presetIdx;
        listContent.numStrings    = stringsCount;
        listContent.capoSemitones = capoShift;
        favoritesMask             = favMask;
        listContent.favoritesMask = favMask;

        if (modeChanged)
        {
            listContent.rebuildLayout();
            if (viewport.getWidth() > 0)
                listContent.setSize (viewport.getWidth() - 10, listContent.getRequiredHeight());
        }
        listContent.repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawText ("TUNING PRESETS", 62, 4, 140, 22, juce::Justification::centredLeft);

        g.setColour (juce::Colour (50, 55, 68));
        g.drawLine (12.0f, 34.0f, bounds.getWidth() - 12.0f, 34.0f, 1.0f);
    }

    void resized() override
    {
        backBtn.setBounds (8, 4, 45, 22);
        viewport.setBounds (0, 38, getWidth(), getHeight() - 44);
        listContent.setSize (viewport.getWidth() - 10, listContent.getRequiredHeight());
    }

private:
    int selectedPresetIdx = 0;
    std::uint32_t favoritesMask = 0xFFFFFFFFu;

    class FlatBackButton : public juce::Button
    {
    public:
        FlatBackButton() : juce::Button ("back") {}
        void paintButton (juce::Graphics& g, bool isMouseOver, bool) override
        {
            g.setColour (isMouseOver ? juce::Colours::white : juce::Colour (170, 175, 185));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText ("< Back", getLocalBounds(), juce::Justification::centredLeft, false);
        }
    } backBtn;

    class PresetListContent : public juce::Component
    {
    public:
        std::function<void(int)> onRowClick;
        std::function<void(int)> onStarToggle;

        bool isBass = false;
        int selectedIdx = 0;
        int numStrings = 6;
        int capoSemitones = 0;
        std::uint32_t favoritesMask = 0xFFFFFFFFu;

        PresetListContent()
        {
            rebuildLayout();
        }

        [[nodiscard]] int getRequiredHeight() const noexcept { return totalHeight; }

        void rebuildLayout()
        {
            rows.clear();
            const auto presets = TuningPresets::getPresets (isBass);
            std::string_view lastCat {};
            int y = 2;

            for (std::size_t i = 0; i < presets.size(); ++i)
            {
                if (presets[i].category != lastCat)
                {
                    lastCat = presets[i].category;
                    rows.push_back ({ true, TuningPresets::toJuceString (lastCat), -1, y });
                    y += 16;
                }
                rows.push_back ({ false, {}, static_cast<int> (i), y });
                y += 17;
            }
            totalHeight = y + 6;
        }

        void paint (juce::Graphics& g) override
        {
            const auto presets = TuningPresets::getPresets (isBass);

            for (const auto& item : rows)
            {
                if (item.isHeader)
                {
                    g.setColour (juce::Colour (100, 215, 255));
                    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                    g.drawText (item.categoryTitle, 6, item.y + 4, getWidth() - 12, 16,
                                juce::Justification::centredLeft, false);
                }
                else if (item.presetIdx >= 0 && static_cast<std::size_t> (item.presetIdx) < presets.size())
                {
                    const auto& p = presets[static_cast<std::size_t> (item.presetIdx)];
                    const juce::Rectangle<float> rowRect (2.0f, static_cast<float> (item.y + 4),
                                                          static_cast<float> (getWidth()) - 4.0f, 18.0f);

                    const bool isSelected = (item.presetIdx == selectedIdx);
                    const bool isHovered  = (item.presetIdx == hoveredPresetIdx);

                    if (isSelected)
                    {
                        g.setColour (juce::Colour (42, 62, 92));
                        g.fillRoundedRectangle (rowRect, 4.0f);
                    }
                    else if (isHovered)
                    {
                        g.setColour (juce::Colour (32, 38, 50));
                        g.fillRoundedRectangle (rowRect, 4.0f);
                    }

                    // 1. Звёздочка избранного слева
                    const bool isFav = (favoritesMask & (1u << item.presetIdx)) != 0;
                    PresetSelectorComponent::drawStarIcon (g, 14.0f, static_cast<float> (item.y) + 13.0f, 5.2f,
                                                           isFav, isHovered && hoveredOnStar);

                    // 2. Имя пресета
                    g.setColour (isSelected ? juce::Colours::white : juce::Colour (210, 215, 225));
                    g.setFont (juce::FontOptions (11.0f, isSelected ? juce::Font::bold : juce::Font::plain));
                    g.drawText (TuningPresets::toJuceString (p.name), 26, item.y, 118, 24,
                                juce::Justification::centredLeft, true);

                    const auto active = TuningPresets::getActiveStrings (p, isBass, numStrings);

                    // 3. Мини-плашки нот справа
                    const float badgeW     = 15.5f;
                    const float gap        = 1.6f;
                    const float rightStart = static_cast<float> (getWidth()) - static_cast<float> (active.count) * (badgeW + gap) - 4.0f;

                    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                    for (int s = 0; s < active.count; ++s)
                    {
                        const int midi = active.midiNotes[static_cast<std::size_t> (s)] + capoSemitones;
                        const juce::String nStr = TuningPresets::midiToNoteName (midi);
                        const juce::Rectangle<float> br (rightStart + static_cast<float> (s) * (badgeW + gap),
                                                         static_cast<float> (item.y) + 5.5f, badgeW, 14.5f);

                        g.setColour (juce::Colour (55, 82, 125));
                        g.fillRoundedRectangle (br, 2.2f);
                        g.setColour (juce::Colours::white);
                        g.drawText (nStr, br, juce::Justification::centred, false);
                    }
                }
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            int newHover = -1;
            const bool onStar = (e.x < 25);
            for (const auto& item : rows)
            {
                if (!item.isHeader && e.y >= item.y && e.y < item.y + 24)
                {
                    newHover = item.presetIdx;
                    break;
                }
            }
            if (newHover != hoveredPresetIdx || onStar != hoveredOnStar)
            {
                hoveredPresetIdx = newHover;
                hoveredOnStar    = onStar;
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            hoveredPresetIdx = -1;
            repaint();
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            for (const auto& item : rows)
            {
                if (!item.isHeader && e.y >= item.y && e.y < item.y + 24)
                {
                    if (e.x < 25)
                    {
                        if (onStarToggle) onStarToggle (item.presetIdx);
                    }
                    else
                    {
                        if (onRowClick) onRowClick (item.presetIdx);
                    }
                    break;
                }
            }
        }

    private:
        struct RowItem
        {
            bool isHeader;
            juce::String categoryTitle;
            int presetIdx;
            int y;
        };
        std::vector<RowItem> rows;
        int totalHeight = 0;
        int hoveredPresetIdx = -1;
        bool hoveredOnStar = false;
    } listContent;

    juce::Viewport viewport;
};