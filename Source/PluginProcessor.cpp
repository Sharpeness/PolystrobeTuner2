#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <span>

namespace
{
    [[nodiscard]] juce::File getStandaloneSettingsFile()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("RockmanAudio")
                       .getChildFile ("StrobeTuner");
        dir.createDirectory();
        return dir.getChildFile ("StrobeTunerState.xml");
    }
}

StrobTunerAudioProcessor::StrobTunerAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    favoritePresetsMask.store (TuningPresets::getDefaultFavoritesMask (false), std::memory_order_relaxed);
    favoriteBassMask.store    (TuningPresets::getDefaultFavoritesMask (true),  std::memory_order_relaxed);

    if (wrapperType == wrapperType_Standalone)
        [[maybe_unused]] const auto loadRes = loadStandaloneStateFile();
}

StrobTunerAudioProcessor::~StrobTunerAudioProcessor()
{
    if (wrapperType == wrapperType_Standalone)
        [[maybe_unused]] const auto saveRes = saveStandaloneStateFile();
}

void StrobTunerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pitchDetector.prepare (sampleRate, samplesPerBlock);
    polyDetector.prepare (sampleRate);
}

void StrobTunerAudioProcessor::releaseResources() {}

void StrobTunerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) [[unlikely]]
        return;

    const bool bass     = isBassMode.load (std::memory_order_relaxed);
    const int acc       = accuracyPercent.load (std::memory_order_relaxed);
    const float a4      = referencePitch.load (std::memory_order_relaxed);
    const int presetIdx = bass ? selectedBassPreset.load (std::memory_order_relaxed)
                               : selectedPresetIndex.load (std::memory_order_relaxed);
    const int strings   = bass ? numBassStrings.load (std::memory_order_relaxed)
                               : numStrings.load (std::memory_order_relaxed);
    const int capo      = capoSemitones.load (std::memory_order_relaxed);

    pitchDetector.setBassMode (bass);
    pitchDetector.setSensitivity (static_cast<float> (acc));

    const std::span<const float> inputSamples {
        buffer.getReadPointer (0),
        static_cast<std::size_t> (numSamples)
    };

    // 1. Монофонический детектор основного тона (MPM)
    pitchDetector.process (inputSamples);

    // 2. Полифонический резонансный детектор струн (Polytune) — Capo НЕ влияет на полифонический режим
    const auto allPresets   = TuningPresets::getPresets (bass);
    const int safePresetIdx = std::clamp (presetIdx, 0, static_cast<int> (allPresets.size()) - 1);
    const auto activeView   = TuningPresets::getActiveStrings (
        allPresets[static_cast<std::size_t> (safePresetIdx)], bass, strings);

    polyDetector.setTargetStrings (activeView, a4, static_cast<float> (acc));
    polyDetector.process (inputSamples, pitchDetector.hasSignal());

    const auto& polyResults = polyDetector.getResults();
    for (std::size_t i = 0; i < TuningPresets::kMaxStrings; ++i)
    {
        polyState.strings[i].centsDeviation.store (polyResults[i].cents,  std::memory_order_relaxed);
        polyState.strings[i].isActive.store       (polyResults[i].active, std::memory_order_relaxed);
    }
    polyState.isPolyModeActive.store (polyDetector.isPolyMode(), std::memory_order_release);

    // 3. Обновление монофонической телеметрии (Capo влияет только на отображаемое имя ноты в центральной матрице)
    if (pitchDetector.hasSignal())
    {
        const float freq = pitchDetector.getFrequency();
        currentFrequency.store (freq, std::memory_order_relaxed);

        const int rawMidi = static_cast<int> (std::lround (69.0f + 12.0f * std::log2 (freq / a4)));
        const float sweetenerOffset = TuningPresets::getSweetenerOffsetForMidi (bass, safePresetIdx, strings, rawMidi);

        if (const auto result = TunerMath::frequencyToNote (freq, a4, sweetenerOffset, capo); result.has_value())
        {
            const auto& [noteChar, sharpFlag, midiNote, octaveNum, centsVal, validFlag] = *result;
            currentNote.store      (noteChar,  std::memory_order_relaxed);
            isSharp.store          (sharpFlag, std::memory_order_relaxed);
            centsDeviation.store   (centsVal,  std::memory_order_relaxed);
            currentOctave.store    (octaveNum, std::memory_order_relaxed);
            isSignalActive.store   (true,      std::memory_order_release);
        }
    }
    else
    {
        isSignalActive.store   (false, std::memory_order_release);
        currentFrequency.store (0.0f,  std::memory_order_relaxed);
    }

    if (isMuted.load (std::memory_order_relaxed))
    {
        buffer.clear();
    }
}

void StrobTunerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("StrobeTunerState");

    xml.setAttribute ("referencePitch",      static_cast<double> (referencePitch.load (std::memory_order_relaxed)));
    xml.setAttribute ("isMuted",             isMuted.load (std::memory_order_relaxed));
    xml.setAttribute ("fastStrobeThreshold", static_cast<double> (fastStrobeThreshold.load (std::memory_order_relaxed)));
    xml.setAttribute ("slowStrobeThreshold", static_cast<double> (slowStrobeThreshold.load (std::memory_order_relaxed)));
    xml.setAttribute ("inTuneTolerance",     static_cast<double> (inTuneTolerance.load (std::memory_order_relaxed)));
    xml.setAttribute ("polytuneTolerance",   static_cast<double> (polytuneTolerance.load (std::memory_order_relaxed)));
    xml.setAttribute ("accuracyPercent",     accuracyPercent.load (std::memory_order_relaxed));
    xml.setAttribute ("slowStrobeAbove",     slowStrobeAbove.load (std::memory_order_relaxed));
    xml.setAttribute ("showOctaveNumber",    showOctaveNumber.load (std::memory_order_relaxed));

    xml.setAttribute ("isBassMode",          isBassMode.load (std::memory_order_relaxed));
    xml.setAttribute ("selectedPresetIndex", selectedPresetIndex.load (std::memory_order_relaxed));
    xml.setAttribute ("selectedBassPreset",  selectedBassPreset.load (std::memory_order_relaxed));
    xml.setAttribute ("numStrings",          numStrings.load (std::memory_order_relaxed));
    xml.setAttribute ("numBassStrings",      numBassStrings.load (std::memory_order_relaxed));
    xml.setAttribute ("capoSemitones",       capoSemitones.load (std::memory_order_relaxed));
    xml.setAttribute ("favoritePresetsMask", static_cast<int> (favoritePresetsMask.load (std::memory_order_relaxed)));
    xml.setAttribute ("favoriteBassMask",    static_cast<int> (favoriteBassMask.load (std::memory_order_relaxed)));

    copyXmlToBinary (xml, destData);
}

void StrobTunerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName ("StrobeTunerState"))
    {
        referencePitch.store      (static_cast<float> (xmlState->getDoubleAttribute ("referencePitch", 440.0)),      std::memory_order_relaxed);
        isMuted.store             (xmlState->getBoolAttribute ("isMuted", false),                                    std::memory_order_relaxed);
        fastStrobeThreshold.store (static_cast<float> (xmlState->getDoubleAttribute ("fastStrobeThreshold", 0.1)),   std::memory_order_relaxed);
        slowStrobeThreshold.store (static_cast<float> (xmlState->getDoubleAttribute ("slowStrobeThreshold", 1.0)),   std::memory_order_relaxed);
        inTuneTolerance.store     (static_cast<float> (xmlState->getDoubleAttribute ("inTuneTolerance", 2.0)),       std::memory_order_relaxed);
        polytuneTolerance.store   (static_cast<float> (xmlState->getDoubleAttribute ("polytuneTolerance", 2.0)),     std::memory_order_relaxed);
        accuracyPercent.store     (std::clamp (xmlState->getIntAttribute ("accuracyPercent", 100), 10, 100),         std::memory_order_relaxed);
        slowStrobeAbove.store     (xmlState->getBoolAttribute ("slowStrobeAbove", true),                             std::memory_order_relaxed);
        showOctaveNumber.store    (xmlState->getBoolAttribute ("showOctaveNumber", true),                            std::memory_order_relaxed);

        isBassMode.store          (xmlState->getBoolAttribute ("isBassMode", false),                                 std::memory_order_relaxed);

        constexpr int maxG = static_cast<int> (TuningPresets::kNumGuitarPresets);
        selectedPresetIndex.store (std::clamp (xmlState->getIntAttribute ("selectedPresetIndex", 0), 0, maxG - 1),   std::memory_order_relaxed);

        constexpr int maxB = static_cast<int> (TuningPresets::kNumBassPresets);
        selectedBassPreset.store  (std::clamp (xmlState->getIntAttribute ("selectedBassPreset", 0), 0, maxB - 1),    std::memory_order_relaxed);

        numStrings.store          (std::clamp (xmlState->getIntAttribute ("numStrings", 6), 6, 8),                   std::memory_order_relaxed);
        numBassStrings.store      (std::clamp (xmlState->getIntAttribute ("numBassStrings", 4), 4, 6),               std::memory_order_relaxed);
        capoSemitones.store       (std::clamp (xmlState->getIntAttribute ("capoSemitones", 0), -9, 9),               std::memory_order_relaxed);

        if (xmlState->hasAttribute ("favoritePresetsMask"))
        {
            const auto m = static_cast<std::uint32_t> (xmlState->getIntAttribute (
                "favoritePresetsMask",
                static_cast<int> (favoritePresetsMask.load (std::memory_order_relaxed))));
            if (m != 0)
                favoritePresetsMask.store (m, std::memory_order_relaxed);
        }
        if (xmlState->hasAttribute ("favoriteBassMask"))
        {
            const auto m = static_cast<std::uint32_t> (xmlState->getIntAttribute (
                "favoriteBassMask",
                static_cast<int> (favoriteBassMask.load (std::memory_order_relaxed))));
            if (m != 0)
                favoriteBassMask.store (m, std::memory_order_relaxed);
        }

        if (onStateLoaded)
            juce::MessageManager::callAsync (onStateLoaded);
    }
}

std::expected<void, juce::String> StrobTunerAudioProcessor::saveStandaloneStateFile() const
{
    juce::MemoryBlock mb;
    const_cast<StrobTunerAudioProcessor*> (this)->getStateInformation (mb);
    auto xml = getXmlFromBinary (mb.getData(), static_cast<int> (mb.getSize()));
    if (xml == nullptr)
        return std::unexpected (juce::String ("Failed to serialize state XML"));

    if (!xml->writeTo (getStandaloneSettingsFile()))
        return std::unexpected (juce::String ("Failed to write state file to disk"));

    return {};
}

std::expected<void, juce::String> StrobTunerAudioProcessor::loadStandaloneStateFile()
{
    const auto file = getStandaloneSettingsFile();
    if (!file.existsAsFile())
        return std::unexpected (juce::String ("State file does not exist"));

    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return std::unexpected (juce::String ("Failed to parse state XML file"));

    juce::MemoryBlock mb;
    copyXmlToBinary (*xml, mb);
    setStateInformation (mb.getData(), static_cast<int> (mb.getSize()));
    return {};
}

juce::AudioProcessorEditor* StrobTunerAudioProcessor::createEditor()
{
    return new StrobTunerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StrobTunerAudioProcessor();
}