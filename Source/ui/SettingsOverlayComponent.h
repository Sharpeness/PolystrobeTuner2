#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "ValueSpinBox.h"
#include "PedalToggle.h"

class SettingsOverlayComponent : public juce::Component
{
public:
    std::function<void()> onClose;

    SettingsOverlayComponent()
        : fastStrobeBox    ("Fast strobe, cent", 0.10f, 0.05f, 5.00f, 0.01f, 2),
          slowStrobeBox    ("Slow strobe, cent", 1.0f,  1.0f,  10.0f, 0.1f,  1),
          inTuneBox        ("In tune, cent",     3.0f,  1.0f,  10.0f, 0.1f,  1),
          polytuneBox      ("Polytune, cent",    2.0f,  1.0f,  10.0f, 0.1f,  1),
          slowAboveToggle  ("Slow strobe above", true),
          showOctaveToggle ("Show octave number", true)
    {
        backBtn.setButtonText ("< Back");
        backBtn.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (backBtn);

        addAndMakeVisible (fastStrobeBox);
        addAndMakeVisible (slowStrobeBox);
        addAndMakeVisible (inTuneBox);
        addAndMakeVisible (polytuneBox);
        addAndMakeVisible (slowAboveToggle);
        addAndMakeVisible (showOctaveToggle);
    }

    ValueSpinBox fastStrobeBox;
    ValueSpinBox slowStrobeBox;
    ValueSpinBox inTuneBox;
    ValueSpinBox polytuneBox;
    PedalToggle  slowAboveToggle;
    PedalToggle  showOctaveToggle;

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("SETTINGS", 65, 10, 120, 24, juce::Justification::centredLeft);

        g.setColour (juce::Colour (50, 55, 68));
        g.drawLine (16.0f, 38.0f, bounds.getWidth() - 16.0f, 38.0f, 1.0f);

        g.setColour (juce::Colour (100, 215, 255));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("TUNING ACCURACY", 18, 44, 160, 18, juce::Justification::centredLeft);
    }

    void resized() override
    {
        backBtn.setBounds (16, 10, 45, 24);

        int y               = 66;
        constexpr int itemH = 24;
        constexpr int gap   = 3;
        const int w         = getWidth() - 20;

        fastStrobeBox.setBounds (18, y, w, itemH);
        y += itemH + gap;

        slowStrobeBox.setBounds (18, y, w, itemH);
        y += itemH + gap;

        inTuneBox.setBounds (18, y, w, itemH);
        y += itemH + gap;

        polytuneBox.setBounds (18, y, w, itemH);
        y += itemH + gap + 4;

        slowAboveToggle.setBounds (18, y, w - 14, itemH);
        y += itemH + gap;

        showOctaveToggle.setBounds (18, y, w - 14, itemH);
    }

private:
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
};