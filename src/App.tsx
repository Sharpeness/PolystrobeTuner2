import React, { useEffect, useRef, useState } from 'react';
import { Mic, MicOff, Volume2, VolumeX, Radio, RotateCcw, Layers } from 'lucide-react';
import { MpmPitchDetector } from './dsp/MpmPitchDetector';
import { PolyPitchDetector, PolyStringResult } from './dsp/PolyPitchDetector';
import { TunerMath } from './dsp/TunerMath';
import { TuningPresets } from './dsp/TuningPresets';
import {
  Display3DBezel,
  StrobeArcCanvas,
  NoteMatrixCanvas,
  SteppedKnob,
  GuitarBassSwitch,
  Footswitch,
  SettingsTabButton,
} from './ui/StrobeCanvasComponents';
import {
  PresetSelector,
  RefPitchBox,
  SettingsOverlay,
  PresetBrowserOverlay,
} from './ui/OverlaysAndControls';

const STORAGE_KEY = 'RockmanAudio_StrobeTunerState_v1';

interface SavedState {
  referencePitch: number;
  isMuted: boolean;
  fastStrobeThreshold: number;
  slowStrobeThreshold: number;
  inTuneTolerance: number;
  polytuneTolerance: number;
  accuracyPercent: number;
  slowStrobeAbove: boolean;
  showOctaveNumber: boolean;
  isBassMode: boolean;
  selectedPresetIndex: number;
  selectedBassPreset: number;
  numStrings: number;
  numBassStrings: number;
  capoSemitones: number;
  favoritePresetsMask: number;
  favoriteBassMask: number;
}

function getDefaultFavoritesMask(isBass: boolean): number {
  const presets = TuningPresets.getPresets(isBass);
  let mask = 0;
  for (let i = 0; i < presets.length; ++i) {
    if (presets[i].defaultFavorite) {
      mask |= 1 << i;
    }
  }
  return mask;
}

// Default per-string detunes (in cents) matching the 6 Polytune reference screenshots
const DEFAULT_POLY_DETUNES: number[] = [-14.0, 0.0, 5.5, -4.5, 1.8, 0.0, -1.8, 12.0];

export function App() {
  // Processor parameters (matching StrobTunerAudioProcessor)
  const [referencePitch, setReferencePitch] = useState<number>(440.0);
  const [isMuted, setIsMuted] = useState<boolean>(false);
  const [fastStrobeThreshold, setFastStrobeThreshold] = useState<number>(0.1);
  const [slowStrobeThreshold, setSlowStrobeThreshold] = useState<number>(1.0);
  const [inTuneTolerance, setInTuneTolerance] = useState<number>(2.0);
  const [polytuneTolerance, setPolytuneTolerance] = useState<number>(2.0);
  const [accuracyPercent, setAccuracyPercent] = useState<number>(100);
  const [slowStrobeAbove, setSlowStrobeAbove] = useState<boolean>(true);
  const [showOctaveNumber, setShowOctaveNumber] = useState<boolean>(true);

  const [isBassMode, setIsBassMode] = useState<boolean>(false);
  const [selectedPresetIndex, setSelectedPresetIndex] = useState<number>(0);
  const [selectedBassPreset, setSelectedBassPreset] = useState<number>(0);
  const [numStrings, setNumStrings] = useState<number>(6);
  const [numBassStrings, setNumBassStrings] = useState<number>(4);
  const [capoSemitones, setCapoSemitones] = useState<number>(0);
  const [favoritePresetsMask, setFavoritePresetsMask] = useState<number>(() =>
    getDefaultFavoritesMask(false)
  );
  const [favoriteBassMask, setFavoriteBassMask] = useState<number>(() =>
    getDefaultFavoritesMask(true)
  );

  // Overlays
  const [isSettingsOpen, setIsSettingsOpen] = useState<boolean>(false);
  const [isPresetBrowserOpen, setIsPresetBrowserOpen] = useState<boolean>(false);

  // Real-time DSP output state
  const [currentNote, setCurrentNote] = useState<string>(' ');
  const [isSharp, setIsSharp] = useState<boolean>(false);
  const [currentOctave, setCurrentOctave] = useState<number>(4);
  const [centsDeviation, setCentsDeviation] = useState<number>(0.0);
  const [isSignalActive, setIsSignalActive] = useState<boolean>(false);
  const [currentFrequency, setCurrentFrequency] = useState<number>(0.0);

  // Real-time Polytune DSP output state
  const [isPolyModeActive, setIsPolyModeActive] = useState<boolean>(false);
  const [polyColumns, setPolyColumns] = useState<PolyStringResult[]>(() =>
    Array.from({ length: 8 }, () => ({ cents: 0, energy: 0, active: false }))
  );

  // Audio Input / Built-in Signal Generator state (for standalone browser testing & live mic)
  const [inputMode, setInputMode] = useState<'mic' | 'synth'>('synth');
  const [micActive, setMicActive] = useState<boolean>(false);
  const [micError, setMicError] = useState<string | null>(null);
  const [synthActive, setSynthActive] = useState<boolean>(true);
  const [synthPolyStrum, setSynthPolyStrum] = useState<boolean>(false);
  const [selectedStringIdx, setSelectedStringIdx] = useState<number>(0);
  const [synthDetuneCents, setSynthDetuneCents] = useState<number>(0.0);
  const [polyStringDetunes, setPolyStringDetunes] = useState<number[]>(DEFAULT_POLY_DETUNES);
  const [audibleSynth, setAudibleSynth] = useState<boolean>(false);
  const [uiScale, setUiScale] = useState<number>(1.35);

  // Load saved state on mount
  useEffect(() => {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (raw) {
        const parsed: Partial<SavedState> = JSON.parse(raw);
        if (typeof parsed.referencePitch === 'number') setReferencePitch(parsed.referencePitch);
        if (typeof parsed.isMuted === 'boolean') setIsMuted(parsed.isMuted);
        if (typeof parsed.fastStrobeThreshold === 'number')
          setFastStrobeThreshold(parsed.fastStrobeThreshold);
        if (typeof parsed.slowStrobeThreshold === 'number')
          setSlowStrobeThreshold(parsed.slowStrobeThreshold);
        if (typeof parsed.inTuneTolerance === 'number')
          setInTuneTolerance(parsed.inTuneTolerance);
        if (typeof parsed.polytuneTolerance === 'number')
          setPolytuneTolerance(parsed.polytuneTolerance);
        if (typeof parsed.accuracyPercent === 'number')
          setAccuracyPercent(Math.max(10, Math.min(100, parsed.accuracyPercent)));
        if (typeof parsed.slowStrobeAbove === 'boolean')
          setSlowStrobeAbove(parsed.slowStrobeAbove);
        if (typeof parsed.showOctaveNumber === 'boolean')
          setShowOctaveNumber(parsed.showOctaveNumber);
        if (typeof parsed.isBassMode === 'boolean') setIsBassMode(parsed.isBassMode);
        if (typeof parsed.selectedPresetIndex === 'number')
          setSelectedPresetIndex(parsed.selectedPresetIndex);
        if (typeof parsed.selectedBassPreset === 'number')
          setSelectedBassPreset(parsed.selectedBassPreset);
        if (typeof parsed.numStrings === 'number')
          setNumStrings(Math.max(6, Math.min(8, parsed.numStrings)));
        if (typeof parsed.numBassStrings === 'number')
          setNumBassStrings(Math.max(4, Math.min(6, parsed.numBassStrings)));
        if (typeof parsed.capoSemitones === 'number')
          setCapoSemitones(Math.max(-9, Math.min(9, parsed.capoSemitones)));
        if (typeof parsed.favoritePresetsMask === 'number' && parsed.favoritePresetsMask !== 0)
          setFavoritePresetsMask(parsed.favoritePresetsMask);
        if (typeof parsed.favoriteBassMask === 'number' && parsed.favoriteBassMask !== 0)
          setFavoriteBassMask(parsed.favoriteBassMask);
      }
    } catch {
      // Ignore storage errors
    }
  }, []);

  // Save state on change
  useEffect(() => {
    try {
      const stateToSave: SavedState = {
        referencePitch,
        isMuted,
        fastStrobeThreshold,
        slowStrobeThreshold,
        inTuneTolerance,
        polytuneTolerance,
        accuracyPercent,
        slowStrobeAbove,
        showOctaveNumber,
        isBassMode,
        selectedPresetIndex,
        selectedBassPreset,
        numStrings,
        numBassStrings,
        capoSemitones,
        favoritePresetsMask,
        favoriteBassMask,
      };
      localStorage.setItem(STORAGE_KEY, JSON.stringify(stateToSave));
    } catch {
      // Ignore storage errors
    }
  }, [
    referencePitch,
    isMuted,
    fastStrobeThreshold,
    slowStrobeThreshold,
    inTuneTolerance,
    polytuneTolerance,
    accuracyPercent,
    slowStrobeAbove,
    showOctaveNumber,
    isBassMode,
    selectedPresetIndex,
    selectedBassPreset,
    numStrings,
    numBassStrings,
    capoSemitones,
    favoritePresetsMask,
    favoriteBassMask,
  ]);

  // Keep latest processor parameters in a ref for the DSP loop
  const paramsRef = useRef({
    referencePitch,
    isMuted,
    accuracyPercent,
    isBassMode,
    selectedPresetIndex,
    selectedBassPreset,
    numStrings,
    numBassStrings,
    capoSemitones,
    inputMode,
    synthActive,
    synthPolyStrum,
    selectedStringIdx,
    synthDetuneCents,
    polyStringDetunes,
  });

  useEffect(() => {
    paramsRef.current = {
      referencePitch,
      isMuted,
      accuracyPercent,
      isBassMode,
      selectedPresetIndex,
      selectedBassPreset,
      numStrings,
      numBassStrings,
      capoSemitones,
      inputMode,
      synthActive,
      synthPolyStrum,
      selectedStringIdx,
      synthDetuneCents,
      polyStringDetunes,
    };
  }, [
    referencePitch,
    isMuted,
    accuracyPercent,
    isBassMode,
    selectedPresetIndex,
    selectedBassPreset,
    numStrings,
    numBassStrings,
    capoSemitones,
    inputMode,
    synthActive,
    synthPolyStrum,
    selectedStringIdx,
    synthDetuneCents,
    polyStringDetunes,
  ]);

  // DSP MPM + PolyPitchDetector instances
  const detectorRef = useRef<MpmPitchDetector | null>(null);
  const polyDetectorRef = useRef<PolyPitchDetector | null>(null);
  const audioCtxRef = useRef<AudioContext | null>(null);
  const micStreamRef = useRef<MediaStream | null>(null);
  const scriptNodeRef = useRef<ScriptProcessorNode | null>(null);
  const oscRef = useRef<OscillatorNode | null>(null);
  const gainRef = useRef<GainNode | null>(null);

  // Compute active preset & strings
  const activePresets = TuningPresets.getPresets(isBassMode);
  const activePresetIdx = isBassMode ? selectedBassPreset : selectedPresetIndex;
  const safePresetIdx = Math.max(0, Math.min(activePresets.length - 1, activePresetIdx));
  const currentPreset = activePresets[safePresetIdx];
  const activeStringsCount = isBassMode ? numBassStrings : numStrings;
  const activeStringInfo = TuningPresets.getActiveStringNotes(
    currentPreset,
    isBassMode,
    activeStringsCount
  );

  useEffect(() => {
    if (selectedStringIdx >= activeStringInfo.count) {
      setSelectedStringIdx(0);
    }
  }, [activeStringInfo.count, selectedStringIdx]);

  // Process a block of audio samples through MpmPitchDetector + PolyPitchDetector + TunerMath
  const processAudioSamples = (samples: Float32Array, sampleRate: number) => {
    if (!detectorRef.current) {
      detectorRef.current = new MpmPitchDetector();
      detectorRef.current.prepare(sampleRate);
    }
    if (!polyDetectorRef.current) {
      polyDetectorRef.current = new PolyPitchDetector();
      polyDetectorRef.current.prepare(sampleRate);
    }

    const p = paramsRef.current;
    const detector = detectorRef.current;
    const polyDetector = polyDetectorRef.current;

    const a4 = p.referencePitch;
    const presets = TuningPresets.getPresets(p.isBassMode);
    const presetIdx = Math.max(
      0,
      Math.min(
        presets.length - 1,
        p.isBassMode ? p.selectedBassPreset : p.selectedPresetIndex
      )
    );
    const strings = p.isBassMode ? p.numBassStrings : p.numStrings;
    const capo = p.capoSemitones;
    const activeView = TuningPresets.getActiveStringNotes(presets[presetIdx], p.isBassMode, strings);

    detector.setBassMode(p.isBassMode);
    detector.setSensitivity(p.accuracyPercent);
    detector.process(samples);

    polyDetector.setTargetTuning(
      p.isBassMode,
      presetIdx,
      strings,
      a4,
      activeView.midi,
      activeView.offsets,
      activeView.count
    );
    polyDetector.setSensitivity(p.accuracyPercent);
    polyDetector.process(samples);

    setIsPolyModeActive(polyDetector.isPolyphonic());
    setPolyColumns(polyDetector.getStringResults().map((r) => ({ ...r })));

    if (detector.hasSignal()) {
      const freq = detector.getFrequency();
      setCurrentFrequency(freq);

      const rawMidi = Math.round(69.0 + 12.0 * Math.log2(freq / a4));
      const sweetenerOffset = TuningPresets.getSweetenerOffsetForMidi(
        p.isBassMode,
        presetIdx,
        strings,
        rawMidi
      );

      const result = TunerMath.frequencyToNote(freq, a4, sweetenerOffset, capo);
      if (result.isValid) {
        setCurrentNote(result.note);
        setIsSharp(result.isSharp);
        setCentsDeviation(result.cents);
        const oct = Math.max(0, Math.min(8, Math.floor(result.midiNote / 12.0) - 1));
        setCurrentOctave(oct);
        setIsSignalActive(true);
      }
    } else {
      setIsSignalActive(false);
      setCurrentFrequency(0.0);
    }
  };

  // Built-in synthesized test signal loop when inputMode === 'synth'
  useEffect(() => {
    if (inputMode !== 'synth') return;

    const sampleRate = 48000;
    const blockSize = 512;
    const buf = new Float32Array(blockSize);
    const phases = [0, 0, 0, 0, 0, 0, 0, 0];

    if (!detectorRef.current) {
      detectorRef.current = new MpmPitchDetector();
    }
    detectorRef.current.prepare(sampleRate);

    if (!polyDetectorRef.current) {
      polyDetectorRef.current = new PolyPitchDetector();
    }
    polyDetectorRef.current.prepare(sampleRate);

    const interval = window.setInterval(() => {
      const p = paramsRef.current;
      if (!p.synthActive) {
        buf.fill(0);
        processAudioSamples(buf, sampleRate);
        return;
      }

      const presets = TuningPresets.getPresets(p.isBassMode);
      const pIdx = Math.max(
        0,
        Math.min(
          presets.length - 1,
          p.isBassMode ? p.selectedBassPreset : p.selectedPresetIndex
        )
      );
      const strCount = p.isBassMode ? p.numBassStrings : p.numStrings;
      const strData = TuningPresets.getActiveStringNotes(presets[pIdx], p.isBassMode, strCount);

      if (p.synthPolyStrum) {
        // Polyphonic strum: synthesize all active strings simultaneously (strictly preset notes, ignoring Capo)
        buf.fill(0);
        const invCount = 1.0 / Math.max(1, strData.count);
        for (let s = 0; s < strData.count; ++s) {
          const targetMidi = strData.midi[s];
          const sweetener = strData.offsets[s];
          const detune = p.polyStringDetunes[s] ?? 0.0;
          const exactMidi = targetMidi + (sweetener + detune) / 100.0;
          const targetFreq = p.referencePitch * Math.pow(2.0, (exactMidi - 69.0) / 12.0);
          const phaseStep = (2.0 * Math.PI * targetFreq) / sampleRate;
          let ph = phases[s];
          for (let i = 0; i < blockSize; ++i) {
            buf[i] +=
              invCount *
              (0.62 * Math.sin(ph) + 0.16 * Math.sin(ph * 2.0) + 0.05 * Math.sin(ph * 3.0));
            ph += phaseStep;
            if (ph > Math.PI * 200) ph -= Math.PI * 200;
          }
          phases[s] = ph;
        }
      } else {
        const sIdx = Math.max(0, Math.min(strData.count - 1, p.selectedStringIdx));
        const targetMidi = strData.midi[sIdx];
        const sweetener = strData.offsets[sIdx];
        const totalCentsOffset = sweetener + p.synthDetuneCents;
        const exactMidi = targetMidi + totalCentsOffset / 100.0;
        const targetFreq = p.referencePitch * Math.pow(2.0, (exactMidi - 69.0) / 12.0);

        const phaseStep = (2.0 * Math.PI * targetFreq) / sampleRate;
        let ph = phases[0];
        for (let i = 0; i < blockSize; ++i) {
          buf[i] =
            0.55 * Math.sin(ph) + 0.22 * Math.sin(ph * 2.0) + 0.08 * Math.sin(ph * 3.0);
          ph += phaseStep;
          if (ph > Math.PI * 200) ph -= Math.PI * 200;
        }
        phases[0] = ph;
      }

      processAudioSamples(buf, sampleRate);
    }, (blockSize / sampleRate) * 1000);

    return () => clearInterval(interval);
  }, [inputMode]);

  // Optional audible synth tone (respects pedal Mute footswitch!)
  useEffect(() => {
    if (inputMode === 'synth' && synthActive && audibleSynth && !isMuted) {
      if (!audioCtxRef.current) {
        audioCtxRef.current = new AudioContext();
      }
      const ctx = audioCtxRef.current;
      if (ctx.state === 'suspended') {
        ctx.resume();
      }

      if (!oscRef.current) {
        const osc = ctx.createOscillator();
        const gain = ctx.createGain();
        osc.type = 'triangle';
        gain.gain.value = 0.08;
        osc.connect(gain);
        gain.connect(ctx.destination);
        osc.start();
        oscRef.current = osc;
        gainRef.current = gain;
      }

      const sIdx = Math.max(0, Math.min(activeStringInfo.count - 1, selectedStringIdx));
      const targetMidi = activeStringInfo.midi[sIdx] + capoSemitones;
      const sweetener = activeStringInfo.offsets[sIdx];
      const detune = synthPolyStrum ? (polyStringDetunes[sIdx] ?? 0.0) : synthDetuneCents;
      const exactMidi = targetMidi + (sweetener + detune) / 100.0;
      const targetFreq = referencePitch * Math.pow(2.0, (exactMidi - 69.0) / 12.0);
      oscRef.current.frequency.setTargetAtTime(targetFreq, ctx.currentTime, 0.015);
    } else {
      if (oscRef.current) {
        try {
          oscRef.current.stop();
          oscRef.current.disconnect();
        } catch {
          // ignore
        }
        oscRef.current = null;
      }
    }
  }, [
    inputMode,
    synthActive,
    synthPolyStrum,
    audibleSynth,
    isMuted,
    selectedStringIdx,
    activeStringInfo,
    capoSemitones,
    synthDetuneCents,
    polyStringDetunes,
    referencePitch,
  ]);

  // Toggle Live Microphone Input
  const toggleMicrophone = async () => {
    if (inputMode === 'mic' && micActive) {
      stopMicrophone();
      setInputMode('synth');
      return;
    }

    setMicError(null);
    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: {
          echoCancellation: false,
          autoGainControl: false,
          noiseSuppression: false,
        },
      });
      const ctx = new AudioContext();
      const source = ctx.createMediaStreamSource(stream);
      const processor = ctx.createScriptProcessor(1024, 1, 1);

      detectorRef.current = new MpmPitchDetector();
      detectorRef.current.prepare(ctx.sampleRate);
      polyDetectorRef.current = new PolyPitchDetector();
      polyDetectorRef.current.prepare(ctx.sampleRate);

      processor.onaudioprocess = (e) => {
        const inputData = e.inputBuffer.getChannelData(0);
        processAudioSamples(inputData, ctx.sampleRate);
      };

      source.connect(processor);
      processor.connect(ctx.destination);

      audioCtxRef.current = ctx;
      micStreamRef.current = stream;
      scriptNodeRef.current = processor;
      setMicActive(true);
      setInputMode('mic');
    } catch (err: any) {
      setMicError(err?.message || 'Microphone access denied or unavailable');
      setMicActive(false);
    }
  };

  const stopMicrophone = () => {
    if (scriptNodeRef.current) {
      scriptNodeRef.current.disconnect();
      scriptNodeRef.current = null;
    }
    if (micStreamRef.current) {
      micStreamRef.current.getTracks().forEach((t) => t.stop());
      micStreamRef.current = null;
    }
    setMicActive(false);
  };

  useEffect(() => {
    return () => {
      stopMicrophone();
    };
  }, []);

  // Format frequency and cents labels matching PluginEditor::timerCallback()
  let freqText = '';
  let centsSignText = '';
  let centsValText = '';

  if (!isPolyModeActive && isSignalActive && currentFrequency > 0.0) {
    freqText = `${currentFrequency.toFixed(2)} Hz`;
    const absCents = Math.abs(centsDeviation);
    if (absCents < 0.005) {
      centsSignText = '';
      centsValText = '0.00 c';
    } else if (centsDeviation > 0.0) {
      centsSignText = '+';
      centsValText = `${absCents.toFixed(2)} c`;
    } else {
      centsSignText = '- ';
      centsValText = `${absCents.toFixed(2)} c`;
    }
  }

  const hideMainDisplay = isSettingsOpen || isPresetBrowserOpen;

  return (
    <div className="min-h-screen bg-[#14171f] text-slate-100 flex flex-col items-center justify-center p-4 gap-6">
      {/* Top Standalone Header Bar */}
      <div className="w-full max-w-[640px] flex flex-wrap items-center justify-between gap-3 bg-[#1d222e] border border-[#2d3446] rounded-lg px-4 py-2.5 shadow-lg">
        <div className="flex items-center gap-2.5">
          <div className="w-2.5 h-2.5 rounded-full bg-[#64ff41] shadow-[0_0_8px_#64ff41]" />
          <div>
            <h1 className="text-sm font-bold tracking-wide text-white leading-tight">
              StrobeTuner <span className="text-xs font-normal text-slate-400">by RockmanAudio</span>
            </h1>
            <p className="text-[11px] text-slate-400">
              C++23 JUCE 8 • Mono Strobe &amp; Polytune Matrix • Sweetener Presets
            </p>
          </div>
        </div>

        {/* Zoom / Scale Selector & Input Mode */}
        <div className="flex items-center gap-2">
          <div className="flex items-center bg-[#14171f] rounded-md p-0.5 border border-[#2c3344] text-xs">
            {[1, 1.25, 1.35, 1.5].map((sc) => (
              <button
                key={sc}
                onClick={() => setUiScale(sc)}
                className={`px-2 py-1 rounded transition-colors cursor-pointer ${
                  uiScale === sc
                    ? 'bg-[#2e4468] text-white font-semibold'
                    : 'text-slate-400 hover:text-white'
                }`}
              >
                {Math.round(sc * 100)}%
              </button>
            ))}
          </div>

          <button
            onClick={toggleMicrophone}
            className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-semibold border transition-all cursor-pointer ${
              inputMode === 'mic' && micActive
                ? 'bg-emerald-600/20 border-emerald-500/60 text-emerald-300'
                : 'bg-[#252b3b] border-[#384157] text-slate-200 hover:bg-[#2e364a]'
            }`}
          >
            {inputMode === 'mic' && micActive ? (
              <>
                <Mic className="w-3.5 h-3.5 text-emerald-400 animate-pulse" />
                <span>Mic Active</span>
              </>
            ) : (
              <>
                <MicOff className="w-3.5 h-3.5 text-slate-400" />
                <span>Use Microphone</span>
              </>
            )}
          </button>
        </div>
      </div>

      {micError && (
        <div className="w-full max-w-[640px] bg-red-950/60 border border-red-500/40 rounded-md px-3.5 py-2 text-xs text-red-200">
          Microphone notice: {micError}. Using built-in string signal generator below.
        </div>
      )}

      {/* =================================================================== */}
      {/* EXACT 448x260 JUCE PLUGIN EDITOR HARDWARE PEDAL                     */}
      {/* =================================================================== */}
      <div
        style={{
          width: 448 * uiScale,
          height: 260 * uiScale,
        }}
        className="relative flex items-center justify-center my-1"
      >
        <div
          style={{
            width: 448,
            height: 260,
            transform: `scale(${uiScale})`,
            transformOrigin: 'center center',
          }}
          className="relative rounded-[14px] overflow-hidden shadow-[0_20px_50px_rgba(0,0,0,0.65)] border border-white/20 select-none"
        >
          {/* 1. Background & 3D Display Bezel */}
          <Display3DBezel width={448} height={260} />

          {/* 2. Main Display Content (hidden when Settings or Preset Browser overlay is open) */}
          {!hideMainDisplay && (
            <>
              {/* Strobe Arc: setBounds(16, 14, 262, 115) */}
              <div style={{ left: 16, top: 14, width: 262, height: 115 }} className="absolute">
                <StrobeArcCanvas
                  cents={centsDeviation}
                  active={isSignalActive}
                  fastThresh={fastStrobeThreshold}
                  slowThresh={slowStrobeThreshold}
                  slowAbove={slowStrobeAbove}
                  isPolyMode={isPolyModeActive}
                  polyColumns={polyColumns}
                  numStrings={activeStringsCount}
                  polyTolerance={polytuneTolerance}
                  width={262}
                  height={115}
                />
              </div>

              {/* Note Matrix: setBounds(47, 95, 200, 100) */}
              <div style={{ left: 47, top: 95, width: 200, height: 100 }} className="absolute">
                <NoteMatrixCanvas
                  noteName={currentNote}
                  isSharp={isSharp}
                  octaveNum={currentOctave}
                  active={!isPolyModeActive && isSignalActive}
                  cents={centsDeviation}
                  inTuneTolerance={inTuneTolerance}
                  showOctave={showOctaveNumber}
                  width={200}
                  height={100}
                />
              </div>

              {/* Frequency Label: setBounds(22, 138, 85, 20) */}
              <div
                style={{
                  left: 22,
                  top: 138,
                  width: 85,
                  height: 20,
                  color: 'rgba(255, 255, 255, 0.9)',
                }}
                className="absolute flex items-center justify-start text-[13px] font-bold tracking-tight pointer-events-none"
              >
                {freqText}
              </div>

              {/* Cents Sign Label: setBounds(22, 162, 19, 20) */}
              <div
                style={{
                  left: 22,
                  top: 162,
                  width: 19,
                  height: 20,
                  color: 'rgba(255, 255, 255, 0.9)',
                }}
                className="absolute flex items-center justify-end pr-0.5 text-[13px] font-bold pointer-events-none"
              >
                {centsSignText}
              </div>

              {/* Cents Value Label: setBounds(34, 162, 75, 20) */}
              <div
                style={{
                  left: 34,
                  top: 162,
                  width: 75,
                  height: 20,
                  color: 'rgba(255, 255, 255, 0.9)',
                }}
                className="absolute flex items-center justify-start text-[13px] font-bold tracking-tight pointer-events-none"
              >
                {centsValText}
              </div>

              {/* Reference Pitch Box: setBounds(162, 217, 110, 24) */}
              <div style={{ left: 162, top: 217, width: 110, height: 24 }} className="absolute">
                <RefPitchBox value={referencePitch} onChange={setReferencePitch} />
              </div>
            </>
          )}

          {/* 3. Settings Overlay: setBounds(14, 14, 266, 232) */}
          {isSettingsOpen && (
            <div style={{ left: 14, top: 14, width: 266, height: 232 }} className="absolute z-20">
              <SettingsOverlay
                fastStrobe={fastStrobeThreshold}
                slowStrobe={slowStrobeThreshold}
                inTune={inTuneTolerance}
                polytune={polytuneTolerance}
                slowAbove={slowStrobeAbove}
                showOctave={showOctaveNumber}
                onChangeFastStrobe={setFastStrobeThreshold}
                onChangeSlowStrobe={setSlowStrobeThreshold}
                onChangeInTune={setInTuneTolerance}
                onChangePolytune={setPolytuneTolerance}
                onChangeSlowAbove={setSlowStrobeAbove}
                onChangeShowOctave={setShowOctaveNumber}
                onClose={() => setIsSettingsOpen(false)}
              />
            </div>
          )}

          {/* 4. Preset Browser Overlay: setBounds(14, 14, 266, 232) */}
          {isPresetBrowserOpen && (
            <div style={{ left: 14, top: 14, width: 266, height: 232 }} className="absolute z-20">
              <PresetBrowserOverlay
                isBass={isBassMode}
                selectedPresetIdx={safePresetIdx}
                numStrings={activeStringsCount}
                capoSemitones={0}
                favoritesMask={isBassMode ? favoriteBassMask : favoritePresetsMask}
                onSelectPreset={(idx) => {
                  if (isBassMode) setSelectedBassPreset(idx);
                  else setSelectedPresetIndex(idx);
                }}
                onFavoritesChanged={(mask) => {
                  if (isBassMode) setFavoriteBassMask(mask);
                  else setFavoritePresetsMask(mask);
                }}
                onClose={() => setIsPresetBrowserOpen(false)}
              />
            </div>
          )}

          {/* 5. Right Control Panel */}
          {/* PresetSelector: setBounds(291, 12, 154, 37) */}
          <div style={{ left: 291, top: 12, width: 154, height: 37 }} className="absolute z-30">
            <PresetSelector
              isBass={isBassMode}
              presetIdx={safePresetIdx}
              numStrings={activeStringsCount}
              capoSemitones={0}
              favoritesMask={isBassMode ? favoriteBassMask : favoritePresetsMask}
              onSelectPreset={(idx) => {
                if (isBassMode) setSelectedBassPreset(idx);
                else setSelectedPresetIndex(idx);
              }}
              onStarClicked={() => {
                setIsPresetBrowserOpen((prev) => {
                  const next = !prev;
                  if (next) setIsSettingsOpen(false);
                  return next;
                });
              }}
            />
          </div>

          {/* Strings Knob: setBounds(291, 55, 50, 68) */}
          <div style={{ left: 291, top: 55, width: 50, height: 68 }} className="absolute">
            <SteppedKnob
              title="Strings"
              minVal={isBassMode ? 4 : 6}
              maxVal={isBassMode ? 6 : 8}
              defaultVal={isBassMode ? 4 : 6}
              value={activeStringsCount}
              stepVal={1}
              onChange={(val) => {
                if (isBassMode) setNumBassStrings(val);
                else setNumStrings(val);
              }}
              width={50}
              height={68}
            />
          </div>

          {/* Capo Knob: setBounds(341, 55, 50, 68) */}
          <div style={{ left: 341, top: 55, width: 50, height: 68 }} className="absolute">
            <SteppedKnob
              title="Capo"
              minVal={-9}
              maxVal={9}
              defaultVal={0}
              value={capoSemitones}
              stepVal={1}
              onChange={setCapoSemitones}
              width={50}
              height={68}
            />
          </div>

          {/* Accuracy Knob: setBounds(391, 55, 52, 68) */}
          <div style={{ left: 391, top: 55, width: 52, height: 68 }} className="absolute">
            <SteppedKnob
              title="Accuracy"
              minVal={10}
              maxVal={100}
              defaultVal={100}
              value={accuracyPercent}
              stepVal={5}
              onChange={setAccuracyPercent}
              width={52}
              height={68}
            />
          </div>

          {/* Guitar / Bass Switch: setBounds(300, 130, 132, 22) */}
          <div style={{ left: 300, top: 130, width: 132, height: 22 }} className="absolute">
            <GuitarBassSwitch isBass={isBassMode} onChange={setIsBassMode} width={132} height={22} />
          </div>

          {/* Settings Gear Button: setBounds(287, 206, 22, 22) */}
          <div style={{ left: 287, top: 206, width: 22, height: 22 }} className="absolute">
            <SettingsTabButton
              onClick={() => {
                setIsSettingsOpen((prev) => {
                  const next = !prev;
                  if (next) setIsPresetBrowserOpen(false);
                  return next;
                });
              }}
              width={22}
              height={22}
            />
          </div>

          {/* Mute Footswitch: setBounds(337, 174, 62, 78) */}
          <div style={{ left: 337, top: 174, width: 62, height: 78 }} className="absolute">
            <Footswitch muted={isMuted} onToggle={setIsMuted} width={62} height={78} />
          </div>
        </div>
      </div>

      {/* =================================================================== */}
      {/* INTERACTIVE TEST SIGNAL GENERATOR (MONO STROBE & POLYTUNE STRUM)    */}
      {/* =================================================================== */}
      <div className="w-full max-w-[640px] bg-[#1d222e] border border-[#2d3446] rounded-lg p-4 shadow-lg flex flex-col gap-3.5">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <div className="flex items-center gap-2">
            <Radio className="w-4 h-4 text-cyan-400" />
            <span className="text-xs font-bold uppercase tracking-wider text-slate-200">
              Instrument Signal Simulator (Mono Strobe &amp; Polytune)
            </span>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            <button
              onClick={() => {
                if (inputMode === 'mic') stopMicrophone();
                setInputMode('synth');
                setSynthActive(true);
                setSynthPolyStrum((p) => !p);
              }}
              className={`flex items-center gap-1.5 px-2.5 py-1 rounded text-xs font-semibold border transition-colors cursor-pointer ${
                inputMode === 'synth' && synthActive && synthPolyStrum
                  ? 'bg-emerald-500/20 border-emerald-400/60 text-emerald-300'
                  : 'bg-[#14171f] border-[#2c3344] text-slate-300 hover:text-white'
              }`}
              title="Strum all strings simultaneously to trigger Polytune column mode"
            >
              <Layers className="w-3.5 h-3.5" />
              <span>{synthPolyStrum ? 'Mode: Polytune (All Strings)' : 'Mode: Single String'}</span>
            </button>

            <button
              onClick={() => {
                if (inputMode === 'mic') stopMicrophone();
                setInputMode('synth');
                setSynthActive((s) => !s);
              }}
              className={`px-2.5 py-1 rounded text-xs font-semibold border transition-colors cursor-pointer ${
                inputMode === 'synth' && synthActive
                  ? 'bg-cyan-500/20 border-cyan-400/60 text-cyan-300'
                  : 'bg-[#14171f] border-[#2c3344] text-slate-400 hover:text-white'
              }`}
            >
              {inputMode === 'synth' && synthActive ? 'Signal: ON' : 'Signal: OFF'}
            </button>

            <button
              onClick={() => setAudibleSynth((a) => !a)}
              className={`flex items-center gap-1 px-2.5 py-1 rounded text-xs font-semibold border transition-colors cursor-pointer ${
                audibleSynth
                  ? 'bg-amber-500/20 border-amber-400/60 text-amber-300'
                  : 'bg-[#14171f] border-[#2c3344] text-slate-400 hover:text-white'
              }`}
              title="Play synthesized tone through speakers (muted when pedal footswitch is engaged)"
            >
              {audibleSynth && !isMuted ? (
                <Volume2 className="w-3.5 h-3.5" />
              ) : (
                <VolumeX className="w-3.5 h-3.5" />
              )}
              <span>Audio Monitor</span>
            </button>
          </div>
        </div>

        {/* String Pluck Selector Buttons */}
        <div className="flex flex-col gap-1.5">
          <div className="flex items-center justify-between text-[11px] text-slate-400">
            <span>
              Active Preset: <strong className="text-slate-200">{currentPreset.name}</strong> (
              {isBassMode ? `${numBassStrings}-String Bass` : `${numStrings}-String Guitar`})
            </span>
            <span>
              {synthPolyStrum
                ? 'Polytune active — click a string below to adjust its individual detune:'
                : 'Click a string to feed its pitch into the MPM detector:'}
            </span>
          </div>

          <div className="grid grid-cols-4 sm:grid-cols-8 gap-1.5">
            {Array.from({ length: activeStringInfo.count }).map((_, i) => {
              const badgeMidi = activeStringInfo.midi[i];
              const offset = activeStringInfo.offsets[i];
              const noteLabel = TuningPresets.midiToNoteName(badgeMidi);
              const oct = Math.floor((badgeMidi + capoSemitones) / 12) - 1;
              const stringNumber = activeStringInfo.count - i;
              const isSelected =
                inputMode === 'synth' && synthActive && selectedStringIdx === i;
              const polyDetune = polyStringDetunes[i] ?? 0;

              return (
                <button
                  key={i}
                  onClick={() => {
                    if (inputMode === 'mic') stopMicrophone();
                    setInputMode('synth');
                    setSynthActive(true);
                    setSelectedStringIdx(i);
                  }}
                  className={`flex flex-col items-center justify-center py-2 px-1 rounded border transition-all cursor-pointer ${
                    isSelected
                      ? 'bg-[#2a3e5c] border-cyan-400 text-white shadow-[0_0_12px_rgba(56,189,248,0.25)]'
                      : 'bg-[#14171f] border-[#2c3344] text-slate-300 hover:border-slate-500'
                  }`}
                >
                  <span className="text-[10px] text-slate-400">Str {stringNumber}</span>
                  <span className="text-sm font-bold">
                    {noteLabel}
                    <sub className="text-[10px] font-normal">{oct}</sub>
                  </span>
                  {synthPolyStrum ? (
                    <span
                      className={`text-[9px] font-mono ${
                        Math.abs(polyDetune) <= polytuneTolerance * 0.5
                          ? 'text-emerald-400'
                          : 'text-rose-400'
                      }`}
                    >
                      {polyDetune > 0 ? `+${polyDetune.toFixed(1)}c` : `${polyDetune.toFixed(1)}c`}
                    </span>
                  ) : (
                    offset !== 0 && (
                      <span className="text-[9px] text-cyan-300 font-mono">
                        {offset > 0 ? `+${offset}c` : `${offset}c`}
                      </span>
                    )
                  )}
                </button>
              );
            })}
          </div>
        </div>

        {/* Fine Detune Slider (-25 cents to +25 cents) */}
        <div className="flex items-center gap-3 pt-1">
          <span className="text-xs text-slate-400 w-36 shrink-0">
            {synthPolyStrum ? `Str ${activeStringInfo.count - selectedStringIdx} Detune:` : 'String Detune:'}{' '}
            <strong className="text-white font-mono">
              {(() => {
                const val = synthPolyStrum
                  ? (polyStringDetunes[selectedStringIdx] ?? 0.0)
                  : synthDetuneCents;
                return val > 0 ? `+${val.toFixed(1)}` : val.toFixed(1);
              })()}{' '}
              c
            </strong>
          </span>

          <input
            type="range"
            min={-25}
            max={25}
            step={0.1}
            value={
              synthPolyStrum
                ? (polyStringDetunes[selectedStringIdx] ?? 0.0)
                : synthDetuneCents
            }
            onChange={(e) => {
              const v = parseFloat(e.target.value);
              if (synthPolyStrum) {
                setPolyStringDetunes((prev) => {
                  const next = [...prev];
                  next[selectedStringIdx] = v;
                  return next;
                });
              } else {
                setSynthDetuneCents(v);
              }
            }}
            className="flex-1 accent-cyan-400 cursor-pointer"
          />

          <button
            onClick={() => {
              if (synthPolyStrum) {
                setPolyStringDetunes((prev) => {
                  const next = [...prev];
                  next[selectedStringIdx] = 0.0;
                  return next;
                });
              } else {
                setSynthDetuneCents(0.0);
              }
            }}
            className="flex items-center gap-1 px-2 py-1 rounded bg-[#14171f] border border-[#2c3344] text-[11px] text-slate-300 hover:text-white cursor-pointer"
            title="Reset Detune to 0.0 cents (In Tune)"
          >
            <RotateCcw className="w-3 h-3" />
            <span>0.0 c</span>
          </button>
        </div>
      </div>
    </div>
  );
}

