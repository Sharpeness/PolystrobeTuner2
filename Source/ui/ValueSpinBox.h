#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

class ValueSpinBox : public juce::Component,
                     public juce::Label::Listener
{
public:
    std::function<void(float)> onValueChange;

    ValueSpinBox (const juce::String& titleText,
                  float initialVal, float minV, float maxV, float stepV,
                  int numDecimals)
        : defaultVal (initialVal), currentVal (initialVal),
          minVal (minV), maxVal (maxV), stepVal (stepV),
          decimals (numDecimals)
    {
        titleLabel.setText (titleText, juce::dontSendNotification);
        titleLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, juce::Colour (210, 215, 225));
        titleLabel.setJustificationType (juce::Justification::centredLeft);
        titleLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (titleLabel);

        valueLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        valueLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        valueLabel.setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
        valueLabel.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::transparentBlack);
        valueLabel.setColour (juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
        valueLabel.setJustificationType (juce::Justification::centredRight);
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
                ed->setJustification (juce::Justification::centredRight);
            }
        };
        addAndMakeVisible (valueLabel);

        updateText();
    }

    void setValue (float newVal, bool sendNotification = true)
    {
        const float rounded = std::clamp (std::round (newVal / stepVal) * stepVal, minVal, maxVal);

        if (std::abs (currentVal - rounded) > (stepVal * 0.05f))
        {
            currentVal = rounded;
            updateText();
            if (sendNotification && onValueChange)
                onValueChange (currentVal);
            repaint();
        }
    }

    [[nodiscard]] float getValue() const noexcept { return currentVal; }
    [[nodiscard]] float getDefaultValue() const noexcept { return defaultVal; }

    [[nodiscard]] bool isModified() const noexcept
    {
        return std::abs (currentVal - defaultVal) > (stepVal * 0.1f);
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const float midY  = bounds.getCentreY();

        // 1. Стрелки вверх/вниз
        const float ax = bounds.getRight() - 26.0f;

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

        // 2. Иконка сброса (круговая стрелка по часовой стрелке)
        if (isModified())
        {
            const float rx       = bounds.getRight() - 8.0f;
            const float ry       = midY;
            constexpr float arcR = 4.2f;
            constexpr float pi   = std::numbers::pi_v<float>;
            constexpr float twoPi = pi * 2.0f;

            juce::Path resetPath;
            resetPath.addCentredArc (rx, ry, arcR, arcR, 0.0f,
                                     pi * 0.30f,
                                     twoPi * 0.95f,
                                     true);

            g.setColour (resetHovered ? juce::Colours::white : juce::Colour (160, 165, 175));
            g.strokePath (resetPath, juce::PathStrokeType (1.3f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

            constexpr float endAngle = twoPi * 0.95f;
            const float tipX = rx + std::sin (endAngle) * arcR;
            const float tipY = ry - std::cos (endAngle) * arcR;

            juce::Path arrowHead;
            arrowHead.startNewSubPath (tipX - 2.8f, tipY - 1.2f);
            arrowHead.lineTo (tipX + 0.6f, tipY + 1.2f);
            arrowHead.lineTo (tipX - 0.4f, tipY + 3.8f);
            g.strokePath (arrowHead, juce::PathStrokeType (1.3f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
    }

    void resized() override
    {
        titleLabel.setBounds (0, 0, 150, getHeight());
        valueLabel.setBounds (getWidth() - 65 - 34, 1, 65, getHeight() - 2);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        updateHover (e);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        upHovered    = false;
        downHovered  = false;
        resetHovered = false;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartY     = e.position.y;
        valAtDragStart = currentVal;

        if (isModified())
        {
            const juce::Rectangle<int> resetZone (getWidth() - 17, 0, 17, getHeight());
            if (resetZone.contains (e.getPosition()))
            {
                setValue (defaultVal);
                return;
            }
        }

        const juce::Rectangle<int> arrowZone (getWidth() - 34, 0, 17, getHeight());
        if (arrowZone.contains (e.getPosition()))
        {
            if (e.y < getHeight() / 2)
                setValue (currentVal + stepVal);
            else
                setValue (currentVal - stepVal);
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.x <= 150)
            setValue (defaultVal);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const juce::Rectangle<int> arrowZone (getWidth() - 34, 0, 17, getHeight());
        if (arrowZone.contains (juce::Point<int> (static_cast<int> (e.mouseDownPosition.x),
                                                  static_cast<int> (e.mouseDownPosition.y))))
        {
            const float diffY = dragStartY - e.position.y;
            const float steps = std::round (diffY / 5.0f);
            setValue (valAtDragStart + steps * stepVal);
        }
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY > 0.0f)
            setValue (currentVal + stepVal);
        else if (wheel.deltaY < 0.0f)
            setValue (currentVal - stepVal);
    }

    void labelTextChanged (juce::Label*) override
    {
        const float parsed = valueLabel.getText().trim().getFloatValue();
        if (parsed >= minVal && parsed <= maxVal)
            setValue (parsed);
        else
            updateText();
    }

private:
    float defaultVal;
    float currentVal;
    float minVal, maxVal, stepVal;
    int decimals;

    juce::Label titleLabel;
    juce::Label valueLabel;

    bool upHovered    = false;
    bool downHovered  = false;
    bool resetHovered = false;
    float dragStartY     = 0.0f;
    float valAtDragStart = 0.0f;

    void updateText()
    {
        valueLabel.setText (juce::String (currentVal, decimals), juce::dontSendNotification);
    }

    void updateHover (const juce::MouseEvent& e)
    {
        const juce::Rectangle<int> arrowZone (getWidth() - 34, 0, 17, getHeight());
        const bool inArrows = arrowZone.contains (e.getPosition());
        const bool newUp    = inArrows && (e.y < getHeight() / 2);
        const bool newDown  = inArrows && (e.y >= getHeight() / 2);

        bool inReset = false;
        if (isModified())
        {
            const juce::Rectangle<int> resetZone (getWidth() - 17, 0, 17, getHeight());
            inReset = resetZone.contains (e.getPosition());
        }

        if (newUp != upHovered || newDown != downHovered || inReset != resetHovered)
        {
            upHovered    = newUp;
            downHovered  = newDown;
            resetHovered = inReset;
            repaint();
        }
    }
};