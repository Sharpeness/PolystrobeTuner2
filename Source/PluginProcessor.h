#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include "dsp/MpmPitchDetector.h"
#include "dsp/PolyPitchDetector.h"
#include "dsp/TunerMath.h"
#include "dsp/TuningPresets.h"

// Lock-Free состояние отдельной струны для полифонического режима (Polytune)
struct PolyStringState
{
    std::atomic<float> centsDeviation { 0.0f };
    std::atomic<bool>  isActive       { false };
};

// Lock-Free мост полифонического состояния (до 8 струн)
struct PolyTunerState
{
    std::atomic<bool> isPolyModeActive { false };
    std::array<PolyStringState, TuningPresets::kMaxStrings> strings {};
};

class StrobTunerAudioProcessor : public juce::AudioProcessor
{
public:
    StrobTunerAudioProcessor();
    ~StrobTunerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "StrobeTuner"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // 1. Lock-Free телеметрия монофонического стробоскопа (Audio Thread -> GUI Thread)
    std::atomic<char>  currentNote      { ' ' };
    std::atomic<bool>  isSharp          { false };
    std::atomic<float> centsDeviation   { 0.0f };
    std::atomic<bool>  isSignalActive   { false };
    std::atomic<float> currentFrequency { 0.0f };
    std::atomic<int>   currentOctave    { 4 };

    // 2. Lock-Free телеметрия полифонического режима (Polytune)
    PolyTunerState polyState {};

    // 3. Lock-Free параметры тюнера и отображения
    std::atomic<float> referencePitch      { 440.0f };
    std::atomic<bool>  isMuted             { false };
    std::atomic<float> fastStrobeThreshold { 0.1f };
    std::atomic<float> slowStrobeThreshold { 1.0f };
    std::atomic<float> inTuneTolerance     { 2.0f };
    std::atomic<float> polytuneTolerance   { 2.0f };
    std::atomic<bool>  slowStrobeAbove     { true };
    std::atomic<bool>  showOctaveNumber    { true };

    // 4. Переключатель Guitar (false) / Bass (true) и параметры пресетов
    std::atomic<bool>          isBassMode          { false };
    std::atomic<int>           selectedPresetIndex { 0 };
    std::atomic<int>           selectedBassPreset  { 0 };
    std::atomic<int>           numStrings          { 6 }; // 6..8 для гитары
    std::atomic<int>           numBassStrings      { 4 }; // 4..6 для баса
    std::atomic<int>           capoSemitones       { 0 };
    std::atomic<std::uint32_t> favoritePresetsMask { TuningPresets::getDefaultFavoritesMask (false) };
    std::atomic<std::uint32_t> favoriteBassMask    { TuningPresets::getDefaultFavoritesMask (true) };
    std::atomic<int>           accuracyPercent     { 100 }; // 10..100, шаг 5

    std::function<void()> onStateLoaded;

    std::expected<void, juce::String> saveStandaloneStateFile() const;
    std::expected<void, juce::String> loadStandaloneStateFile();

private:
    MpmPitchDetector  pitchDetector;
    PolyPitchDetector polyDetector;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrobTunerAudioProcessor)
};