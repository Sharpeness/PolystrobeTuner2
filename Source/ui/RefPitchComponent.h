#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <functional>

class RefPitchComponent : public juce::Component,
                          public juce::Label::Listener
{
public:
    static constexpr float kMinPitch     = 400.0f;
    static constexpr float kMaxPitch     = 480.0f;
    static constexpr float kDefaultPitch = 440.0f;

    std::function<void(float)> onValueChange;

    RefPitchComponent()
    {
        valueLabel.setFont (juce::FontOptions (12.0f, juce::Font::plain));
        valueLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        valueLabel.setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
        valueLabel.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::transparentBlack);
        valueLabel.setColour (juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
        valueLabel.setJustificationType (juce::Justification::centredLeft);
        valueLabel.setEditable (true, false, false);
        valueLabel.addListener (this);

        valueLabel.onEditorShow = [this]()
        {
            if (auto* ed = valueLabel.getCurrentTextEditor())
            {
                ed->setColour (juce::CaretComponent::caretColourId, juce::Colours::white);
                ed->setColour (juce::TextEditor::highlightColourId, juce::Colour (60, 65, 78));
                ed->setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
                ed->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
            }
        };

        addAndMakeVisible (valueLabel);
        updateText();
    }

    void setValue (float newVal, bool sendNotification = true)
    {
        newVal = std::clamp (std::round (newVal * 10.0f) / 10.0f, kMinPitch, kMaxPitch);
        if (std::abs (currentPitch - newVal) > 0.01f)
        {
            currentPitch = newVal;
            updateText();
            if (sendNotification && onValueChange)
                onValueChange (currentPitch);
            repaint();
        }
    }

    [[nodiscard]] float getValue() const noexcept { return currentPitch; }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        // 1. Тёмная капсула-подложка
        g.setColour (juce::Colour (23, 26, 37));
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (juce::Colour (50, 54, 64));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        // 2. Текст "REF. Hz"
        g.setColour (juce::Colour (170, 170, 170).withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("REF. Hz", 7, 0, 50, getHeight(), juce::Justification::centredLeft, false);

        // 3. Вертикальные стрелки справа
        const auto arrowCol = getLocalBounds().removeFromRight (16);
        const float midY    = static_cast<float> (getHeight()) * 0.5f;
        const float ax      = static_cast<float> (arrowCol.getCentreX());

        juce::Path upArrow;
        upArrow.startNewSubPath (ax - 3.5f, midY - 2.0f);
        upArrow.lineTo (ax, midY - 6.0f);
        upArrow.lineTo (ax + 3.5f, midY - 2.0f);
        g.setColour (upHovered ? juce::Colours::white : juce::Colour (160, 165, 175));
        g.strokePath (upArrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

        juce::Path downArrow;
        downArrow.startNewSubPath (ax - 3.5f, midY + 2.0f);
        downArrow.lineTo (ax, midY + 6.0f);
        downArrow.lineTo (ax + 3.5f, midY + 2.0f);
        g.setColour (downHovered ? juce::Colours::white : juce::Colour (160, 165, 175));
        g.strokePath (downArrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }

    void resized() override
    {
        valueLabel.setBounds (52, 1, getWidth() - 58 - 16 + 5, getHeight() - 2);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        updateHover (e);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        upHovered   = false;
        downHovered = false;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartY       = e.position.y;
        pitchAtDragStart = currentPitch;

        const auto arrowCol = getLocalBounds().removeFromRight (18);
        if (arrowCol.contains (e.getPosition()))
        {
            if (e.y < getHeight() / 2)
                setValue (currentPitch + 0.1f);
            else
                setValue (currentPitch - 0.1f);
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.x < 52)
            setValue (kDefaultPitch);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float diffY = dragStartY - e.position.y;
        const float steps = std::round (diffY / 6.0f);
        setValue (pitchAtDragStart + steps * 0.1f);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY > 0.0f)
            setValue (currentPitch + 0.1f);
        else if (wheel.deltaY < 0.0f)
            setValue (currentPitch - 0.1f);
    }

    void labelTextChanged (juce::Label*) override
    {
        const float parsed = valueLabel.getText().getFloatValue();
        if (parsed >= kMinPitch && parsed <= kMaxPitch)
            setValue (parsed);
        else
            updateText();
    }

private:
    float currentPitch = kDefaultPitch;

    juce::Label valueLabel;
    bool upHovered   = false;
    bool downHovered = false;
    float dragStartY = 0.0f;
    float pitchAtDragStart = kDefaultPitch;

    void updateText()
    {
        valueLabel.setText (juce::String (currentPitch, 1), juce::dontSendNotification);
    }

    void updateHover (const juce::MouseEvent& e)
    {
        const auto arrowCol = getLocalBounds().removeFromRight (18);
        const bool inArrows = arrowCol.contains (e.getPosition());
        const bool newUp    = inArrows && (e.y < getHeight() / 2);
        const bool newDown  = inArrows && (e.y >= getHeight() / 2);

        if (newUp != upHovered || newDown != downHovered)
        {
            upHovered   = newUp;
            downHovered = newDown;
            repaint();
        }
    }
};