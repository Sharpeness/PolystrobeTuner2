#pragma once
#include "PluginProcessor.h"
#include "ui/NoteMatrixComponent.h"
#include "ui/StrobeArcComponent.h"
#include "ui/RefPitchComponent.h"
#include "ui/FootswitchComponent.h"
#include "ui/SettingsButton.h"
#include "ui/SettingsOverlayComponent.h"
#include "ui/SteppedKnobComponent.h"
#include "ui/PresetSelectorComponent.h"
#include "ui/PresetBrowserOverlay.h"
#include "ui/GuitarBassSwitchComponent.h"

class StrobTunerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       public juce::Timer
{
public:
    explicit StrobTunerAudioProcessorEditor (StrobTunerAudioProcessor&);
    ~StrobTunerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    StrobTunerAudioProcessor& audioProcessor;

    NoteMatrixComponent noteMatrix;
    StrobeArcComponent strobeArc;

    juce::Label freqLabel;
    juce::Label centsSignLabel;
    juce::Label centsValueLabel;
    RefPitchComponent refPitchBox;

    // Правая панель управления: Пресеты, 3 ручки (Strings, Capo, Accuracy), тумблер Guitar/Bass и кнопка Mute
    PresetSelectorComponent presetSelector;
    SteppedKnobComponent stringsKnob  { "Strings",   6,   8,   6, 1 };
    SteppedKnobComponent capoKnob     { "Capo",     -9,   9,   0, 1 };
    SteppedKnobComponent accuracyKnob { "Accuracy", 10, 100, 100, 5 };
    GuitarBassSwitchComponent guitarBassSwitch;

    SettingsButton settingsBtn;
    FootswitchComponent footSwitch;

    SettingsOverlayComponent settingsOverlay;
    PresetBrowserOverlay presetBrowserOverlay;

    bool isSettingsOpen = false;
    bool isPresetBrowserOpen = false;

    void updateOverlayVisibility();
    void refreshPresetUI();
    void drawDisplay3DBezel (juce::Graphics& g, juce::Rectangle<float> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrobTunerAudioProcessorEditor)
};