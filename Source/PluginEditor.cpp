#include "PluginProcessor.h"
#include "PluginEditor.h"

StrobTunerAudioProcessorEditor::StrobTunerAudioProcessorEditor (StrobTunerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // 1. Шестерёнка настроек
    addAndMakeVisible (settingsBtn);
    settingsBtn.onClick = [this]
    {
        isSettingsOpen = !isSettingsOpen;
        if (isSettingsOpen) isPresetBrowserOpen = false;
        updateOverlayVisibility();
    };

    // 2. Оверлей настроек
    settingsOverlay.fastStrobeBox.setValue (audioProcessor.fastStrobeThreshold.load (std::memory_order_relaxed), false);
    settingsOverlay.fastStrobeBox.onValueChange = [this] (float v) { audioProcessor.fastStrobeThreshold.store (v, std::memory_order_relaxed); };

    settingsOverlay.slowStrobeBox.setValue (audioProcessor.slowStrobeThreshold.load (std::memory_order_relaxed), false);
    settingsOverlay.slowStrobeBox.onValueChange = [this] (float v) { audioProcessor.slowStrobeThreshold.store (v, std::memory_order_relaxed); };

    settingsOverlay.inTuneBox.setValue (audioProcessor.inTuneTolerance.load (std::memory_order_relaxed), false);
    settingsOverlay.inTuneBox.onValueChange = [this] (float v) { audioProcessor.inTuneTolerance.store (v, std::memory_order_relaxed); };

    settingsOverlay.polytuneBox.setValue (audioProcessor.polytuneTolerance.load (std::memory_order_relaxed), false);
    settingsOverlay.polytuneBox.onValueChange = [this] (float v) { audioProcessor.polytuneTolerance.store (v, std::memory_order_relaxed); };

    settingsOverlay.slowAboveToggle.setChecked (audioProcessor.slowStrobeAbove.load (std::memory_order_relaxed), false);
    settingsOverlay.slowAboveToggle.onToggle = [this] (bool b) { audioProcessor.slowStrobeAbove.store (b, std::memory_order_relaxed); };

    settingsOverlay.showOctaveToggle.setChecked (audioProcessor.showOctaveNumber.load (std::memory_order_relaxed), false);
    settingsOverlay.showOctaveToggle.onToggle = [this] (bool b) { audioProcessor.showOctaveNumber.store (b, std::memory_order_relaxed); };

    settingsOverlay.onClose = [this]
    {
        isSettingsOpen = false;
        updateOverlayVisibility();
    };
    addChildComponent (settingsOverlay);

    // 3. Оверлей браузера пресетов
    presetBrowserOverlay.onClose = [this]
    {
        isPresetBrowserOpen = false;
        updateOverlayVisibility();
    };
    presetBrowserOverlay.onSelectPreset = [this] (int idx)
    {
        if (audioProcessor.isBassMode.load (std::memory_order_relaxed))
            audioProcessor.selectedBassPreset.store (idx, std::memory_order_relaxed);
        else
            audioProcessor.selectedPresetIndex.store (idx, std::memory_order_relaxed);
        refreshPresetUI();
    };
    presetBrowserOverlay.onFavoritesChanged = [this] (std::uint32_t mask)
    {
        if (audioProcessor.isBassMode.load (std::memory_order_relaxed))
            audioProcessor.favoriteBassMask.store (mask, std::memory_order_relaxed);
        else
            audioProcessor.favoritePresetsMask.store (mask, std::memory_order_relaxed);
        refreshPresetUI();
    };
    addChildComponent (presetBrowserOverlay);

    // 4. Селектор пресетов
    presetSelector.onPresetSelected = [this] (int idx)
    {
        if (audioProcessor.isBassMode.load (std::memory_order_relaxed))
            audioProcessor.selectedBassPreset.store (idx, std::memory_order_relaxed);
        else
            audioProcessor.selectedPresetIndex.store (idx, std::memory_order_relaxed);
        refreshPresetUI();
    };
    presetSelector.onStarClicked = [this]
    {
        isPresetBrowserOpen = !isPresetBrowserOpen;
        if (isPresetBrowserOpen) isSettingsOpen = false;
        refreshPresetUI();
        updateOverlayVisibility();
    };
    addAndMakeVisible (presetSelector);

    // 5. Три ручки: Strings, Capo, Accuracy
    stringsKnob.onValueChange = [this] (int val)
    {
        if (audioProcessor.isBassMode.load (std::memory_order_relaxed))
            audioProcessor.numBassStrings.store (val, std::memory_order_relaxed);
        else
            audioProcessor.numStrings.store (val, std::memory_order_relaxed);
        refreshPresetUI();
    };
    addAndMakeVisible (stringsKnob);

    capoKnob.setValue (audioProcessor.capoSemitones.load (std::memory_order_relaxed), false);
    capoKnob.onValueChange = [this] (int val)
    {
        audioProcessor.capoSemitones.store (val, std::memory_order_relaxed);
    };
    addAndMakeVisible (capoKnob);

    accuracyKnob.setValue (audioProcessor.accuracyPercent.load (std::memory_order_relaxed), false);
    accuracyKnob.onValueChange = [this] (int val)
    {
        audioProcessor.accuracyPercent.store (val, std::memory_order_relaxed);
    };
    addAndMakeVisible (accuracyKnob);

    // 6. Переключатель Guitar / Bass
    guitarBassSwitch.setBassMode (audioProcessor.isBassMode.load (std::memory_order_relaxed), false);
    guitarBassSwitch.onModeChange = [this] (bool bass)
    {
        audioProcessor.isBassMode.store (bass, std::memory_order_relaxed);
        refreshPresetUI();
    };
    addAndMakeVisible (guitarBassSwitch);

    refreshPresetUI();

    // 7. Элементы главного дисплея
    addAndMakeVisible (strobeArc);
    addAndMakeVisible (noteMatrix);

    freqLabel.setText ("", juce::dontSendNotification);
    freqLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    freqLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    freqLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (freqLabel);

    centsSignLabel.setText ("", juce::dontSendNotification);
    centsSignLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    centsSignLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    centsSignLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (centsSignLabel);

    centsValueLabel.setText ("", juce::dontSendNotification);
    centsValueLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    centsValueLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    centsValueLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (centsValueLabel);

    refPitchBox.setValue (audioProcessor.referencePitch.load (std::memory_order_relaxed), false);
    refPitchBox.onValueChange = [this] (float val)
    {
        audioProcessor.referencePitch.store (val, std::memory_order_relaxed);
    };
    addAndMakeVisible (refPitchBox);

    footSwitch.setMuted (audioProcessor.isMuted.load (std::memory_order_relaxed), false);
    footSwitch.onToggle = [this] (bool muted)
    {
        audioProcessor.isMuted.store (muted, std::memory_order_relaxed);
    };
    addAndMakeVisible (footSwitch);

    // Синхронизация при загрузке проекта DAW / Standalone
    audioProcessor.onStateLoaded = [this]()
    {
        settingsOverlay.fastStrobeBox.setValue      (audioProcessor.fastStrobeThreshold.load (std::memory_order_relaxed), false);
        settingsOverlay.slowStrobeBox.setValue      (audioProcessor.slowStrobeThreshold.load (std::memory_order_relaxed), false);
        settingsOverlay.inTuneBox.setValue          (audioProcessor.inTuneTolerance.load (std::memory_order_relaxed), false);
        settingsOverlay.polytuneBox.setValue        (audioProcessor.polytuneTolerance.load (std::memory_order_relaxed), false);
        settingsOverlay.slowAboveToggle.setChecked  (audioProcessor.slowStrobeAbove.load (std::memory_order_relaxed), false);
        settingsOverlay.showOctaveToggle.setChecked (audioProcessor.showOctaveNumber.load (std::memory_order_relaxed), false);

        refPitchBox.setValue      (audioProcessor.referencePitch.load (std::memory_order_relaxed), false);
        footSwitch.setMuted       (audioProcessor.isMuted.load (std::memory_order_relaxed), false);
        capoKnob.setValue         (audioProcessor.capoSemitones.load (std::memory_order_relaxed), false);
        accuracyKnob.setValue     (audioProcessor.accuracyPercent.load (std::memory_order_relaxed), false);
        guitarBassSwitch.setBassMode (audioProcessor.isBassMode.load (std::memory_order_relaxed), false);
        refreshPresetUI();
    };

    setSize (448, 260);
    startTimerHz (60);

    footSwitch.setBufferedToImage (true);
    settingsBtn.setBufferedToImage (true);
    refPitchBox.setBufferedToImage (true);
    noteMatrix.setBufferedToImage (true);
    stringsKnob.setBufferedToImage (true);
    capoKnob.setBufferedToImage (true);
    accuracyKnob.setBufferedToImage (true);
    guitarBassSwitch.setBufferedToImage (true);
}

StrobTunerAudioProcessorEditor::~StrobTunerAudioProcessorEditor()
{
    audioProcessor.onStateLoaded = nullptr;
    if (audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        [[maybe_unused]] const auto res = audioProcessor.saveStandaloneStateFile();
    stopTimer();
}

void StrobTunerAudioProcessorEditor::timerCallback()
{
    if (!isShowing())
        return;

    const bool polyActive  = audioProcessor.polyState.isPolyModeActive.load (std::memory_order_acquire);
    const bool monoActive  = audioProcessor.isSignalActive.load (std::memory_order_acquire);
    const char note        = audioProcessor.currentNote.load (std::memory_order_relaxed);
    const bool sharp       = audioProcessor.isSharp.load (std::memory_order_relaxed);
    const int octave       = audioProcessor.currentOctave.load (std::memory_order_relaxed);
    const float cents      = audioProcessor.centsDeviation.load (std::memory_order_relaxed);
    const float freq       = audioProcessor.currentFrequency.load (std::memory_order_relaxed);

    const float inTuneTol  = audioProcessor.inTuneTolerance.load (std::memory_order_relaxed);
    const float polyTol    = audioProcessor.polytuneTolerance.load (std::memory_order_relaxed);
    const float fastThresh = audioProcessor.fastStrobeThreshold.load (std::memory_order_relaxed);
    const float slowThresh = audioProcessor.slowStrobeThreshold.load (std::memory_order_relaxed);
    const bool slowAbove   = audioProcessor.slowStrobeAbove.load (std::memory_order_relaxed);
    const bool showOctave  = audioProcessor.showOctaveNumber.load (std::memory_order_relaxed);

    footSwitch.updateAnimation();

    if (polyActive)
    {
        const bool bass   = audioProcessor.isBassMode.load (std::memory_order_relaxed);
        const int strings = bass ? audioProcessor.numBassStrings.load (std::memory_order_relaxed)
                                 : audioProcessor.numStrings.load (std::memory_order_relaxed);

        std::array<StrobeArcComponent::PolyColumnData, TuningPresets::kMaxStrings> cols {};
        for (std::size_t i = 0; i < TuningPresets::kMaxStrings; ++i)
        {
            cols[i].cents  = audioProcessor.polyState.strings[i].centsDeviation.load (std::memory_order_relaxed);
            cols[i].active = audioProcessor.polyState.strings[i].isActive.load (std::memory_order_relaxed);
        }

        strobeArc.updatePolytune (cols, strings, polyTol);
        // В полифоническом режиме центральная матрица ноты и текстовые индикаторы погашены
        noteMatrix.setNote (note, sharp, octave, false, 0.0f, inTuneTol, showOctave);

        if (freqLabel.getText().isNotEmpty())
        {
            freqLabel.setText ("", juce::dontSendNotification);
            centsSignLabel.setText ("", juce::dontSendNotification);
            centsValueLabel.setText ("", juce::dontSendNotification);
        }
        return;
    }

    noteMatrix.setNote (note, sharp, octave, monoActive, cents, inTuneTol, showOctave);
    strobeArc.updateStrobe (cents, monoActive, fastThresh, slowThresh, slowAbove);

    if (monoActive && freq > 0.0f)
    {
        const juce::String newFreqText = juce::String (freq, 2) + " Hz";
        if (freqLabel.getText() != newFreqText)
            freqLabel.setText (newFreqText, juce::dontSendNotification);

        const float absCents = std::abs (cents);
        juce::String newSign, newVal;
        if (absCents < 0.005f)
        {
            newSign = "";
            newVal  = "0.00 c";
        }
        else if (cents > 0.0f)
        {
            newSign = "+";
            newVal  = juce::String (absCents, 2) + " c";
        }
        else
        {
            newSign = "- ";
            newVal  = juce::String (absCents, 2) + " c";
        }

        if (centsSignLabel.getText() != newSign)
            centsSignLabel.setText (newSign, juce::dontSendNotification);
        if (centsValueLabel.getText() != newVal)
            centsValueLabel.setText (newVal, juce::dontSendNotification);
    }
    else if (freqLabel.getText().isNotEmpty())
    {
        freqLabel.setText ("", juce::dontSendNotification);
        centsSignLabel.setText ("", juce::dontSendNotification);
        centsValueLabel.setText ("", juce::dontSendNotification);
    }
}

void StrobTunerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (236, 238, 241));

    const auto displayArea = juce::Rectangle<float> (12.0f, 12.0f, 270.0f, 236.0f);
    drawDisplay3DBezel (g, displayArea);
}

void StrobTunerAudioProcessorEditor::drawDisplay3DBezel (juce::Graphics& g, juce::Rectangle<float> area)
{
    constexpr float r = 14.0f;

    for (int i = 1; i <= 3; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.07f * static_cast<float> (4 - i)));
        g.drawRoundedRectangle (area.expanded (static_cast<float> (i) * 1.2f).translated (0.0f, static_cast<float> (i) * 1.1f),
                                r + static_cast<float> (i) * 1.2f, 1.6f);
    }

    g.setColour (juce::Colour (130, 136, 148));
    g.drawRoundedRectangle (area.expanded (5.0f), r + 5.0f, 1.2f);

    juce::ColourGradient frameGrad (juce::Colour (255, 255, 255), area.getX(), area.getY(),
                                    juce::Colour (110, 115, 125), area.getRight(), area.getBottom(), false);
    frameGrad.addColour (0.35, juce::Colour (220, 225, 235));
    frameGrad.addColour (0.70, juce::Colour (160, 165, 175));
    g.setGradientFill (frameGrad);
    g.drawRoundedRectangle (area.expanded (3.0f), r + 3.0f, 4.0f);

    g.setColour (juce::Colour (12, 14, 18));
    g.drawRoundedRectangle (area.expanded (0.6f), r + 0.6f, 1.6f);

    g.setColour (juce::Colour (23, 26, 37));
    g.fillRoundedRectangle (area, r);

    juce::Path glassHighlight;
    glassHighlight.addRoundedRectangle (area.getX() + 2.0f, area.getY() + 1.5f, area.getWidth() - 4.0f, 30.0f, r - 2.0f);
    juce::ColourGradient highlightGrad (juce::Colours::white.withAlpha (0.07f), area.getCentreX(), area.getY(),
                                        juce::Colours::transparentWhite, area.getCentreX(), area.getY() + 30.0f, false);
    g.setGradientFill (highlightGrad);
    g.fillPath (glassHighlight);
}

void StrobTunerAudioProcessorEditor::resized()
{
    settingsOverlay.setBounds (14, 14, 266, 232);
    presetBrowserOverlay.setBounds (14, 14, 266, 232);

    strobeArc.setBounds (16, 14, 262, 115);
    noteMatrix.setBounds (47, 95, 200, 100);

    freqLabel.setBounds (22, 138, 85, 20);
    centsSignLabel.setBounds (22, 162, 19, 20);
    centsValueLabel.setBounds (34, 162, 75, 20);
    refPitchBox.setBounds (162, 217, 110, 24);

    // Правая панель управления (по макету Guitar_Bass_Accuracy.png)
    presetSelector.setBounds   (291, 12, 154, 37);

    stringsKnob.setBounds      (291, 55, 50, 68);
    capoKnob.setBounds         (341, 55, 50, 68);
    accuracyKnob.setBounds     (391, 55, 52, 68);

    guitarBassSwitch.setBounds (300, 130, 132, 22);

    settingsBtn.setBounds      (287, 206, 22, 22);
    footSwitch.setBounds       (337, 174, 62, 78);
}

void StrobTunerAudioProcessorEditor::updateOverlayVisibility()
{
    const bool hideMain = isSettingsOpen || isPresetBrowserOpen;

    strobeArc.setVisible (!hideMain);
    noteMatrix.setVisible (!hideMain);
    freqLabel.setVisible (!hideMain);
    centsSignLabel.setVisible (!hideMain);
    centsValueLabel.setVisible (!hideMain);
    refPitchBox.setVisible (!hideMain);

    settingsOverlay.setVisible (isSettingsOpen);
    presetBrowserOverlay.setVisible (isPresetBrowserOpen);
}

void StrobTunerAudioProcessorEditor::refreshPresetUI()
{
    const bool bass = audioProcessor.isBassMode.load (std::memory_order_relaxed);
    if (bass)
    {
        stringsKnob.setRange (4, 6, 4);
        stringsKnob.setValue (audioProcessor.numBassStrings.load (std::memory_order_relaxed), false);
    }
    else
    {
        stringsKnob.setRange (6, 8, 6);
        stringsKnob.setValue (audioProcessor.numStrings.load (std::memory_order_relaxed), false);
    }

    const int pIdx = bass ? audioProcessor.selectedBassPreset.load (std::memory_order_relaxed)
                          : audioProcessor.selectedPresetIndex.load (std::memory_order_relaxed);
    const int str  = bass ? audioProcessor.numBassStrings.load (std::memory_order_relaxed)
                          : audioProcessor.numStrings.load (std::memory_order_relaxed);
    const std::uint32_t fav = bass ? audioProcessor.favoriteBassMask.load (std::memory_order_relaxed)
                                   : audioProcessor.favoritePresetsMask.load (std::memory_order_relaxed);

    presetSelector.updateState (bass, pIdx, str, 0, fav);
    presetBrowserOverlay.syncState (bass, pIdx, str, 0, fav);
}