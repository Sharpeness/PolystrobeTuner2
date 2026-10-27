#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class PedalToggle : public juce::Component
{
public:
    std::function<void(bool)> onToggle;

    explicit PedalToggle (const juce::String& text, bool initialState = true)
        : isChecked (initialState)
    {
        titleLabel.setText (text, juce::dontSendNotification);
        titleLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, juce::Colour (210, 215, 225));
        titleLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (titleLabel);
    }

    void setChecked (bool checked, bool notify = true)
    {
        if (isChecked != checked)
        {
            isChecked = checked;
            if (notify && onToggle)
                onToggle (isChecked);
            repaint();
        }
    }

    [[nodiscard]] bool getChecked() const noexcept { return isChecked; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().removeFromRight (24).reduced (3).toFloat();

        g.setColour (juce::Colour (18, 20, 26));
        g.fillRoundedRectangle (r, 3.5f);

        g.setColour (isHovered ? juce::Colour (160, 165, 175) : juce::Colour (60, 65, 78));
        g.drawRoundedRectangle (r, 3.5f, 1.2f);

        if (isChecked)
        {
            juce::Path tick;
            tick.startNewSubPath (r.getX() + 4.0f, r.getCentreY());
            tick.lineTo (r.getX() + 7.0f, r.getBottom() - 5.0f);
            tick.lineTo (r.getRight() - 4.5f, r.getY() + 5.0f);

            g.setColour (juce::Colour (100, 255, 65));
            g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
    }

    void resized() override
    {
        titleLabel.setBounds (getLocalBounds().removeFromLeft (getWidth() - 30));
    }

    void mouseEnter (const juce::MouseEvent&) override { isHovered = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { isHovered = false; repaint(); }
    void mouseUp    (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()))
            setChecked (!isChecked);
    }

private:
    bool isChecked = true;
    bool isHovered = false;
    juce::Label titleLabel;
};