# Technical Report: C++23 Refactoring of StrobTuner & DSP Signal Analyzer Architecture (Solo & Polytune Modes)

---

## Part 1. C++17 to C++23 Refactoring Report

### 1.1. Migration Goals & Build Environment
The objective of the refactoring was to migrate the **StrobTuner** guitar/bass strobe and polyphonic tuner codebase (built on **JUCE 8.0.4** for **VST3, CLAP, and Standalone ASIO**) from C++17 to the **ISO C++23 standard** (`CMAKE_CXX_STANDARD 23`).
All changes strictly enforce **Real-Time Audio Thread Safety** and maximize SIMD auto-vectorization under the **MSVC compiler (Visual Studio / CLion on Windows x64)** using release flags `/O2 /Oi /Ot /arch:AVX2 /fp:fast /utf-8`.

---

### 1.2. Core C++23 Features Adopted Across the Codebase

#### 1. Monadic & Value-Based Error Handling with `std::expected<T, E>`
All exception handling constructs (`try / catch / throw`) and ambiguous magic error return values were eliminated in favor of C++23 `std::expected` (`<expected>`):
- **`Source/dsp/TunerMath.h`**:
  `TunerMath::frequencyToNote(...)` returns `std::expected<TunerResult, TunerError>` where `enum class TunerError : std::uint8_t { FrequencyOutOfRange, InvalidReferencePitch }`.
  This provides deterministic, zero-overhead validation of the detected frequency (`20..4500 Hz`) and `A4` reference pitch (`400..480 Hz`) inside the real-time audio callback without stack unwinding tables.
- **`Source/PluginProcessor.h` / `Source/PluginProcessor.cpp`**:
  Standalone state persistence methods `saveStandaloneStateFile()` and `loadStandaloneStateFile()` return `std::expected<void, juce::String>`, cleanly propagating XML serialization or disk I/O errors without throwing exceptions.

#### 2. Zero-Copy Buffer Views via `std::span<const T>`
Legacy raw pointer/length pairs `(const float* samples, int numSamples)` and heap-allocated `std::vector` returns were replaced with non-owning `std::span` views (`<span>`):
- **`MpmPitchDetector::process(std::span<const float> samples)`** and **`PolyPitchDetector::process(std::span<const float> samples, bool monoHasStrongSingleLock)`**:
  Audio blocks from `juce::AudioBuffer<float>` are passed directly into both DSP engines via `std::span<const float>` with zero copying and bounds-aware semantics.
- **`TuningPresets::getPresets(bool isBass)`**:
  Returns `constexpr std::span<const TuningPreset>`, exposing the 27 guitar presets and 14 bass presets directly from read-only static storage (`.rdata`) with zero heap allocations.

#### 3. Compile-Time Evaluation (`constexpr` / `inline constexpr`) & `std::string_view`
- **`Source/dsp/TuningPresets.h`**:
  All 27 guitar tuning presets and 14 bass tuning presets—including **Peterson GTR Sweetener**, **Eddie Van Halen (EVH)**, **James Taylor / ACU**, **Buzz Feiten (BFE)**, and Drop tunings—are generated entirely **at compile time** via the `constexpr` factory function `makeShifted(...)`.
- Preset names, categories, and note lookup tables use `std::string_view` and `inline constexpr std::array<std::string_view, 12>`, eliminating static constructor overhead and dynamic string allocations. Conversion to `juce::String` occurs strictly in the UI layer when painting components.
- Helper methods `TuningPresets::getActiveStrings(...)`, `getSweetenerOffsetForMidi(...)`, and `getDefaultFavoritesMask(...)` are marked `[[nodiscard]] static constexpr ... noexcept`.

#### 4. `<numbers>` Mathematical Constants, Branch Hint Attributes & Structured Bindings
- Legacy macros (`M_PI`) and wrapper constants were replaced with standard `<numbers>` variable templates: `std::numbers::pi_v<float>` and `std::numbers::sqrt2_v<float>`.
- Compiler optimization and safety attributes are used systematically:
  - `[[nodiscard]]` on all pure mathematical, DSP state query, and `std::expected`-returning functions;
  - `[[unlikely]]` on guard clauses inside `processBlock` and DSP `process` entry points to guide CPU branch prediction toward the hot audio path;
  - `[[maybe_unused]]` for explicitly ignored status results in Standalone lifecycle hooks.
- **Structured bindings** unpack `TunerResult` cleanly inside `StrobTunerAudioProcessor::processBlock`:
  ```cpp
  if (const auto result = TunerMath::frequencyToNote(freq, a4, sweetenerOffset, capo); result.has_value())
  {
      const auto& [noteChar, sharpFlag, midiNote, octaveNum, centsVal, validFlag] = *result;
      // ...
  }
  ```

---

### 1.3. Real-Time Audio Safety (`processBlock`) & MSVC AVX2 Optimization

1. **Strictly Zero Heap Allocations on the Audio Thread**:
   - Neither `MpmPitchDetector` nor `PolyPitchDetector` performs any dynamic memory operations (`new`, `delete`, `malloc`, `std::vector::resize`, `juce::String`).
   - All delay lines, autocorrelation buffers, and period ring buffers are pre-allocated inline using fixed-size `std::array` containers (`alignas(32) std::array<float, 8192> fullBuffer`, `std::array<ResonatorChannel, 8> channels`, `std::array<float, 32> periodRing`).
2. **Wait-Free / Lock-Free Audio-to-GUI Communication**:
   - No mutexes (`std::mutex`, `std::unique_lock`, `juce::CriticalSection`) exist anywhere in the codebase.
   - All processor parameters and real-time pitch telemetry (`currentFrequency`, `centsDeviation`, and the 8-string `PolyTunerState` structure) use `std::atomic<T>` with `std::memory_order_relaxed` for scalar values and `std::memory_order_release` / `std::memory_order_acquire` for frame-validity flags (`isSignalActive`, `isPolyModeActive`).
3. **AVX2 / FMA Auto-Vectorization for MSVC**:
   - Working buffers in `MpmPitchDetector` are 32-byte aligned (`alignas(32)`) to match 256-bit AVX2 YMM registers.
   - Inner-loop pointers are annotated with `__restrict` (`const float* __restrict p1`), guaranteeing no pointer aliasing to the MSVC vectorizer.
   - The core NSDF cross-correlation loop is unrolled 4-way (`acc0, acc1, acc2, acc3`) and completely free of inner-loop branches (`if/else`), allowing MSVC to emit packed `vfmadd231ps` instructions.
   - `juce::ScopedNoDenormals` is instantiated at the top of `processBlock`, enabling CPU `FTZ` (Flush-To-Zero) and `DAZ` (Denormals-Are-Zero) flags so decaying IIR resonator states never trigger subnormal floating-point microcode stalls.

---

## Part 2. Signal Analyzer Architecture (Solo & Polytune Modes), Acoustic Challenges & Engineering Solutions

StrobTuner runs two specialized pitch analysis engines in parallel:
1. **`MpmPitchDetector` (`Source/dsp/MpmPitchDetector.h`)** — High-precision monophonic fundamental detector driving the **Solo Strobe Mode**.
2. **`PolyPitchDetector` (`Source/dsp/PolyPitchDetector.h`)** — Time-domain multi-resonator bank driving the **Polyphonic Mode (Polytune)** on the 23×5 LED matrix.

---

### 2.1. Monophonic Analyzer (`MpmPitchDetector` — Solo Mode)

#### Algorithmic Architecture
The solo analyzer implements a **Dual-Rate McLeod Pitch Method (MPM)** based on the **Normalized Square Difference Function (NSDF)**:
$$n'(\tau) = \frac{2 \sum_{i=0}^{W-1} x_i \, x_{i+\tau}}{\sum_{i=0}^{W-1} \left( x_i^2 + x_{i+\tau}^2 \right)}$$

Processing proceeds in 4 stages:
1. **Conditioning Filter & Dual-Rate Buffering**:
   The input signal passes through a 1st-order DC/subsonic high-pass filter (`35 Hz` for guitar, `14 Hz` for bass) and a 2nd-order Butterworth low-pass filter (`520..1150 Hz` for guitar, `350..480 Hz` for bass, scaled by the `Accuracy` knob). Each filtered sample is written simultaneously into:
   - a full-rate circular buffer `fullBuffer` at native sample rate $f_s$ (`44.1..96 kHz`), and
   - a decimated buffer `inputBuffer` (`1024` samples at $f_s / D \approx 11..12\text{ kHz}$, hop size `256` samples $\approx 21\text{ ms}$).
2. **Coarse NSDF Peak Picking on Decimated Audio**:
   Over window $W = 512$, `nsdf[tau]` is computed branchlessly. All positive local maxima are collected, and per McLeod's criterion, the **first peak** exceeding `cutoff = max(minClarity, highestVal * 0.85f)` is selected.
3. **Full-Rate Fine NSDF Refinement (`computeFineNsdfAt`)**:
   The coarse lag $\tau_{\text{dec}}$ is projected back to the native sample rate ($\tau_{\text{full}} = \tau_{\text{dec}} \cdot D$) and refined across a local neighborhood $\pm D$ samples directly on `fullBuffer`.
4. **Parabolic Sub-Sample Interpolation**:
   A 3-point parabola is fitted across $(\tau_{\text{best}}-1, \tau_{\text{best}}, \tau_{\text{best}}+1)$:
   $$\delta = \frac{\alpha - \gamma}{2(\alpha - 2\beta + \gamma)}, \quad T_{\text{exact}} = \tau_{\text{best}} + \delta, \quad f = \frac{f_s}{T_{\text{exact}}}$$
   This yields sub-0.1-cent pitch resolution with minimal CPU load.

#### Solo Mode Acoustic Challenges & Solutions in Code
| Acoustic Problem | Physical Cause | Solution in `MpmPitchDetector.h` |
| :--- | :--- | :--- |
| **1. Missing Fundamental & Octave Errors** | On electric guitars (especially bridge pickups) and basses, the 2nd and 3rd harmonics frequently have 2–3× higher amplitude than the fundamental $f_1$. Naive autocorrelation maximum picking locks onto $T_0 / 2$ (one octave high). | McLeod's first-significant-peak rule (`val >= highestVal * 0.85f`) selects the earliest delay $\tau = T_0$ where all harmonics align in phase, preventing octave jumps. |
| **2. Phantom Subharmonic Lock During Chords (~46.3 Hz F#1/G1)** | When strings 4, 3, and 2 ring together in a chord, their frequencies form an approximate $3:4:5$ ratio, producing a strong acoustic beat period of $\approx 21.6\text{ ms}$ (`46.3 Hz`). Standard MPM mistakes this beat for a single low bass note and blocks Polytune mode. | Periodicity clarity gating (`line 388`): in guitar mode, frequencies below `65 Hz` are accepted only if `highestVal >= 0.78f` (a true single fundamental). Chord intermodulation beats have `highestVal` around `0.48..0.62` and are rejected. |
| **3. Pick-Attack Pitch Spike (`+40..+50 cents`)** | During the first 20–40 ms of a pick strike, broadband pick scrape and initial transverse wave dispersion distort the waveform period. | Instantaneous attack relocking (`line 394`) requires `highestVal >= 0.82f`. Otherwise, a 3-frame hysteresis counter (`candidateHits`) filters out transient pitch spikes. |
| **4. Decoupling Capo from Sweetener Offsets** | A capo transposes the displayed note name in Solo mode, whereas Polytune must always match the physical open-string frequencies defined by the preset. | `TunerMath::frequencyToNote` computes `cents` strictly against the true acoustic MIDI note minus `sweetenerCentsOffset`, while `capoSemitones` only shifts `res.midiNote` for visual display. `PolyPitchDetector` ignores `Capo` entirely. |

---

### 2.2. Polyphonic Analyzer (`PolyPitchDetector` — Polytune Mode)

#### Why Traditional FFT Fails for Guitar Polyphonic Tuning
On the 6th guitar string (`E2 = 82.41 Hz`) or 5th bass string (`B0 = 30.87 Hz`), a **1-cent** pitch deviation corresponds to just **0.047 Hz** (`E2`) and **0.018 Hz** (`B0`).
At $f_s = 48\,000\text{ Hz}$, even a massive $65\,536$-point FFT (which introduces **1.36 seconds** of latency) has a bin spacing of $\Delta f = 48000 / 65536 = \mathbf{0.73\text{ Hz}}$ ($\approx 15\text{ cents}$ per bin on E2). Spectral leakage and harmonic overlap between simultaneously ringing strings corrupt FFT phase-vocoder estimates.

#### `PolyPitchDetector` Architecture: Time-Domain Parallel Resonator Bank + Sub-Sample Zero-Crossing Estimator
Instead of FFT, `PolyPitchDetector` implements a parallel bank of dedicated time-domain resonator channels (`ResonatorChannel`, up to 8 strings) updated every `20 ms` (`50 Hz`):

1. **Anti-Aliasing Filter & Decimation to `~22..24 kHz`**:
   The input passes through a 2nd-order Butterworth LPF at `1800 Hz` and is decimated by `2x` (at 44.1/48 kHz) or `4x` (at 88.2/96 kHz).
2. **5-Stage Per-String Isolation Cascade (`ResonatorChannel::processSample`)**:
   Each string channel is tuned to its preset target frequency $f_{\text{target}}$ (including Sweetener offsets) and processes each sample through:
   - **Stage 0 (1st-order HPF at $0.80 \cdot f_{\text{target}}$)** — attenuates lower-pitched bass strings;
   - **Stage 1 (2nd-order Biquad Notch Filter)** — rejects the primary colliding harmonic from lower strings;
   - **Stage 2 (2nd-order Butterworth LPF at $1.22 \cdot f_{\text{target}}$)** — suppresses upper harmonics and higher strings;
   - **Stages 3–5 (3-Stage Cascaded Bandpass Resonator, 6th-order total, $Q = 15..18$)** — isolates the target string oscillations within a $\pm 55\text{ cent}$ capture window.
3. **Dual-Slope Sub-Sample Zero-Crossing Period Measurement**:
   Both rising (`prevSample < 0 && y3 >= 0`) and falling (`prevSample > 0 && y3 <= 0`) zero-crossings of the 3rd resonator stage (`y3`) are interpolated to fractional sample precision (`frac`). Valid half-periods within $\pm 55\text{ cents}$ (`minValidPeriod..maxValidPeriod`) are pushed into a 32-element circular buffer `periodRing`.
4. **Interquartile Mean (IQR) Statistical Filter (`computeStablePeriod`)**:
   Every `20 ms`, the 32 periods in `periodRing` are sorted, the lowest 25% and highest 25% outliers are discarded, and the mean of the central 50% ($q_1 \dots q_3$) is computed alongside the interquartile spread `iqrSpreadCents` (used as a phase-coherence confidence metric).

---

### 2.3. Key Physical Challenges in Polyphonic Mode & Their Solutions in Code

#### Challenge 1: Harmonic Collision of Strings 6 & 5 with Thin Strings 1 & 2
- **Physical Cause**:
  In standard guitar tuning (and all parallel drop/step-down tunings):
  - The **3rd harmonic of String 6** ($3 \cdot f_6 = 3 \cdot 82.41 = 247.23\text{ Hz}$) coincides with the **fundamental of String 2** ($f_2 = \text{B3} = 246.94\text{ Hz}$, within 2 cents).
  - The **4th harmonic of String 6** ($4 \cdot f_6 = 329.63\text{ Hz}$) and the **3rd harmonic of String 5** ($3 \cdot f_5 = 330.0\text{ Hz}$) coincide with the **fundamental of String 1** ($f_1 = \text{E4} = 329.63\text{ Hz}$).
  Because wound bass strings carry far greater energy and sustain than thin unwound strings 1 and 2, any resonator tuned to $f_1$ or $f_2$ is overwhelmed by harmonics of Strings 6 and 5—causing Strings 1 and 2 indicators to move whenever String 6 is detuned.
- **Solution in `PolyPitchDetector.h`**:
  1. **2nd-Harmonic Tracking (`harmOrder = 2`) for Colliding Thin Strings**:
     During `setTargetStrings` (lines 119–138), any string whose fundamental matches $3\times$ or $4\times$ a lower string is automatically configured with `harmOrder = 2`, shifting its resonator to track its **2nd harmonic** ($2 \cdot f_2 = 466..494\text{ Hz}$ and $2 \cdot f_1 = 622..659\text{ Hz}$). At these doubled frequencies, the 3rd and 4th harmonics of String 6 and String 5 lie a full octave below the passband and are rejected by $> 65\text{ dB}$.
  2. **Targeted Notch Filtering**:
     - String 1 (`2 * f1`) places its notch filter at $3 \cdot f_2$ (the 3rd harmonic of String 2).
     - String 2 (`2 * f2`) places its default notch at $3 \cdot f_3$, and when String 6 is ringing and detuned by more than `4.0 cents` (`std::abs(lowCh.smoothedCents) > 4.0f`), dynamically moves the notch to `6.0f * lowActualHz` ($Q = 16.0$) to reject String 6's 6th harmonic. When String 6 is within $\pm 4.0\text{ cents}$ of target, the dynamic notch stays at `defaultNotchHz` so it never introduces phase dispersion inside String 2's passband.

#### Challenge 2: Post-Attack Tension Pitch-Spike & Rollback ("Откат струны после удара") on Thin Strings
- **Physical Cause**:
  When a guitar string is plucked, the initial transverse displacement increases string tension ($\Delta T \propto A^2$), causing pitch to read `+4..+10 cents` sharp during the first `80..120 ms` before rolling back ("откат") to its steady-state pitch over `150..300 ms`.
  Because unwound steel strings (Strings 1 and 2) have a high attack-to-sustain crest factor, latching `peakEnergy` at the moment of the pick click (`t = 0 ms`) caused the envelope gate to shut off at `~200 ms`—freezing the display on the sharp attack pitch before the string settled.
- **Solution in `PolyPitchDetector.h`**:
  1. **120 ms Pre-Rollback Attack Blanking (`channelBlankFrames = 6`) with `flushPeriods()`**:
     On pick attack (`isPickAttack` or `chAttack`), channels with `harmOrder > 1` blank period collection for `6` frames (`~120 ms`), call `flushPeriods()`, and reset zero-crossing state (`hasPosCross = false`, `hasNegCross = false`). Zero periods from the initial sharp tension spike enter `periodRing`.
  2. **Post-Blanking Sustain-Onset `peakEnergy` Latching**:
     While blanking is active (`channelBlankFrames > 0 || attackBlankFrames > 0`), `ch.peakEnergy = ch.rawRmsSmooth` and `ch.framesSincePeak = 0` track the envelope continuously so that when blanking ends at `t = 120 ms`, `peakEnergy` represents the settled sustain onset level rather than the initial pick transient spike.
  3. **Dual-Condition Sustain Tracking (`thinSustainValid`)**:
     - For `42` frames after blanking (up to **~1.0 second** after the pluck) while `rawRmsSmooth >= peakEnergy * 0.22f`, updates remain open so the analyzer captures the full post-attack pitch rollback.
     - Furthermore, whenever colliding bass strings (Strings 6 and 5) are not overpowering the thin string (`maxCollidingBassRms < ch.rawRmsSmooth * 1.5f`), updates remain open down to `rawRmsSmooth >= peakEnergy * 0.08f` without frame limit—allowing the player to **tune thin strings 1 and 2 continuously on the fly** just like thick strings 3–6.

#### Challenge 3: Wound-String Stiffness Inharmonicity
- **Physical Cause**:
  Core stiffness in wound strings stretches upper partials according to $f_n = n \, f_1 \sqrt{1 + B \, n^2}$. In Solo mode, wide-band MPM locks onto the harmonic center of gravity (shifted slightly sharp bypartials $n=2..5$), whereas the narrowband Polytune resonator on strings 3–6 isolates the pure fundamental $f_1$. Without compensation, wound strings tuned to `0.0 cents` in Solo mode appear `1.6..3.5 cents` flat in Polytune mode.
- **Solution in `PolyPitchDetector.h`**:
  `inharmOffsetCents(baseFreqHz, harmOrder)` applies calibrated physical stiffness offsets (`+3.45c` for `< 90 Hz`, `+2.80c` for `< 120 Hz`, `+2.95c` for `< 160 Hz`, `+1.65c` for `< 210 Hz`, and `0.0c` for `harmOrder > 1`), aligning Solo and Polytune green zones within `< 0.5 cents`.

#### Challenge 4: Glitch-Free `Solo <-> Polytune` Mode Switching
- **Physical Cause**:
  Plucking a single low string (e.g., String 6) excites its 2nd, 3rd, and 4th harmonics, which coincide with higher string frequencies. Counting all active resonator channels would falsely trigger Polytune mode when tuning a single bass string.
- **Solution in `PolyPitchDetector.h`**:
  1. `isHarmonicOfLower[s]` identifies any channel whose base frequency is a $2\times, 3\times$, or $4\times$ multiple of a currently ringing lower string and excludes it from `nonHarmonicActiveCount`.
  2. Entering Polytune requires at least `3` (for 6/7/8-string guitars) or `2` (for 4-string bass) **independent non-harmonic strings** active simultaneously for `5` consecutive frames (`~100 ms`).
  3. Returning to Solo mode occurs within `6` frames (`~120 ms`) as soon as a single non-harmonic string dominates (`secondNonHarmonicRms < maxNonHarmonicRms * 0.18f`) and `MpmPitchDetector` confirms a valid single-note lock.

#### Challenge 5: Continuous LED Incandescence Crossfade on the 23×5 Matrix
- Rather than quantizing each string column to discrete rows, the UI computes a continuous vertical centroid:
  $$\text{posY} = 2.0 - \frac{\text{cents}}{\text{polytuneTolerance}} \cdot 0.65$$
- Each of the 5 vertical LEDs in a string column computes its brightness using a smooth raised-cosine ($\cos^2$) spatial window around `posY`. As pitch approaches `0 cents`, the outer red LEDs smoothly dim while the center green LED (`row = 2`) brightens proportionally, replicating the analog-style LED filament motion of hardware polyphonic tuners.

---

## Part 3. Key Operational Notes & Recommendations

1. **Signal Chain Placement & Input Level**:
   - Place **StrobTuner** as the **very first plugin** in the DAW insert chain (before noise gates, compressors, overdrive pedals, or amp simulators). Distortion and compression generate intermodulation products and boost upper harmonics, which reduces polyphonic string separation.
2. **Interaction Between `In tune, cent`, `Polytune, cent`, and `Accuracy`**:
   - **`Accuracy` (`10%..100%`)** controls both the front-end LPF cutoff in Solo mode and the adaptive IIR smoothing rate in both modes. Use `70%..100%` for fast real-time peg adjustments and `30%..60%` for ultra-steady intonation setup.
   - **`Polytune, cent`** defines the green center-bar tolerance in polyphonic mode (`2.0..3.5 cents` is ideal for quick full-chord strumming verification).
3. **Extended-Range Instruments (7/8-String Guitars & 5/6-String Basses)**:
   - On low-tension strings (`F#1 = 46.25 Hz`, `B0 = 30.87 Hz`), the physical post-attack stabilization time is `140..180 ms`. Strumming with a medium, even pick stroke and letting the chord ring for `0.5..1.5 seconds` yields the highest polyphonic accuracy.
