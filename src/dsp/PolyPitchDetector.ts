export interface PolyStringResult {
  cents: number;
  energy: number;
  active: boolean;
}

class ResonatorChannel {
  static readonly kRingSize = 16;

  targetFreqHz = 100.0;
  expectedPeriod = 240.0;
  minValidPeriod = 230.0;
  maxValidPeriod = 250.0;

  lp_b0 = 0.0;
  lp_b1 = 0.0;
  lp_b2 = 0.0;
  lp_a1 = 0.0;
  lp_a2 = 0.0;
  lp_x1 = 0.0;
  lp_x2 = 0.0;
  lp_y1 = 0.0;
  lp_y2 = 0.0;

  bp_b0 = 0.0;
  bp_b2 = 0.0;
  bp_a1 = 0.0;
  bp_a2 = 0.0;
  s1_x1 = 0.0;
  s1_x2 = 0.0;
  s1_y1 = 0.0;
  s1_y2 = 0.0;
  s2_x1 = 0.0;
  s2_x2 = 0.0;
  s2_y1 = 0.0;
  s2_y2 = 0.0;

  periodRing: number[] = new Array(16).fill(0.0);
  ringWriteIdx = 0;
  ringCount = 0;
  newPeriodsInWindow = 0;

  prevSample = 0.0;
  samplesSincePos = 0;
  samplesSinceNeg = 0;
  lastPosFrac = 0.0;
  lastNegFrac = 0.0;
  hasPosCross = false;
  hasNegCross = false;

  energySum = 0.0;
  energySamples = 0;

  smoothedCents = 0.0;
  smoothedEnergy = 0.0;
  holdFrames = 0;
  isLocked = false;
  hasCentsInit = false;

  reset(): void {
    this.lp_x1 = this.lp_x2 = this.lp_y1 = this.lp_y2 = 0.0;
    this.s1_x1 = this.s1_x2 = this.s1_y1 = this.s1_y2 = 0.0;
    this.s2_x1 = this.s2_x2 = this.s2_y1 = this.s2_y2 = 0.0;
    this.periodRing.fill(0.0);
    this.ringWriteIdx = 0;
    this.ringCount = 0;
    this.newPeriodsInWindow = 0;
    this.prevSample = 0.0;
    this.samplesSincePos = 0;
    this.samplesSinceNeg = 0;
    this.lastPosFrac = 0.0;
    this.lastNegFrac = 0.0;
    this.hasPosCross = false;
    this.hasNegCross = false;
    this.energySum = 0.0;
    this.energySamples = 0;
    this.smoothedCents = 0.0;
    this.smoothedEnergy = 0.0;
    this.holdFrames = 0;
    this.isLocked = false;
    this.hasCentsInit = false;
  }

  configure(freqHz: number, sampleRateDec: number): void {
    this.targetFreqHz = Math.max(20.0, Math.min(sampleRateDec * 0.42, freqHz));
    this.expectedPeriod = sampleRateDec / this.targetFreqHz;
    this.minValidPeriod = this.expectedPeriod * 0.9687;
    this.maxValidPeriod = this.expectedPeriod * 1.0323;

    const twoPi = 2.0 * Math.PI;

    const lpCutoff = Math.max(30.0, Math.min(sampleRateDec * 0.45, this.targetFreqHz * 1.34));
    const lpOmega = (twoPi * lpCutoff) / sampleRateDec;
    const lpSn = Math.sin(lpOmega);
    const lpCs = Math.cos(lpOmega);
    const lpAlpha = lpSn / Math.SQRT2;
    const lpInvA0 = 1.0 / (1.0 + lpAlpha);

    this.lp_b0 = (1.0 - lpCs) * 0.5 * lpInvA0;
    this.lp_b1 = (1.0 - lpCs) * lpInvA0;
    this.lp_b2 = this.lp_b0;
    this.lp_a1 = -2.0 * lpCs * lpInvA0;
    this.lp_a2 = (1.0 - lpAlpha) * lpInvA0;

    // Exact pole-angle placement (0.00c free-decay bias)
    const Q = 11.5;
    const bpOmega = (twoPi * this.targetFreqHz) / sampleRateDec;
    const poleR = Math.exp(-bpOmega / (2.0 * Q));
    const gainNorm = (1.0 - poleR) * 1.38;

    this.bp_b0 = gainNorm;
    this.bp_b2 = -gainNorm;
    this.bp_a1 = -2.0 * poleR * Math.cos(bpOmega);
    this.bp_a2 = poleR * poleR;
  }

  pushPeriod(period: number): void {
    if (period >= this.minValidPeriod && period <= this.maxValidPeriod) {
      this.periodRing[this.ringWriteIdx] = period;
      this.ringWriteIdx = (this.ringWriteIdx + 1) & (ResonatorChannel.kRingSize - 1);
      if (this.ringCount < ResonatorChannel.kRingSize) ++this.ringCount;
      ++this.newPeriodsInWindow;
    }
  }

  processSample(x: number): void {
    const lpOut =
      this.lp_b0 * x +
      this.lp_b1 * this.lp_x1 +
      this.lp_b2 * this.lp_x2 -
      this.lp_a1 * this.lp_y1 -
      this.lp_a2 * this.lp_y2;
    this.lp_x2 = this.lp_x1;
    this.lp_x1 = x;
    this.lp_y2 = this.lp_y1;
    this.lp_y1 = lpOut;

    const y1 =
      this.bp_b0 * lpOut +
      this.bp_b2 * this.s1_x2 -
      this.bp_a1 * this.s1_y1 -
      this.bp_a2 * this.s1_y2;
    this.s1_x2 = this.s1_x1;
    this.s1_x1 = lpOut;
    this.s1_y2 = this.s1_y1;
    this.s1_y1 = y1;

    const y2 =
      this.bp_b0 * y1 +
      this.bp_b2 * this.s2_x2 -
      this.bp_a1 * this.s2_y1 -
      this.bp_a2 * this.s2_y2;
    this.s2_x2 = this.s2_x1;
    this.s2_x1 = y1;
    this.s2_y2 = this.s2_y1;
    this.s2_y1 = y2;

    this.energySum += y2 * y2;
    ++this.energySamples;

    ++this.samplesSincePos;
    ++this.samplesSinceNeg;

    if (this.prevSample <= 0.0 && y2 > 0.0) {
      const denom = y2 - this.prevSample;
      const frac = denom > 1e-12 ? -this.prevSample / denom : 0.0;
      if (this.hasPosCross) {
        const period = this.samplesSincePos - this.lastPosFrac + frac;
        this.pushPeriod(period);
      }
      this.samplesSincePos = 0;
      this.lastPosFrac = frac;
      this.hasPosCross = true;
    } else if (this.prevSample >= 0.0 && y2 < 0.0) {
      const denom = this.prevSample - y2;
      const frac = denom > 1e-12 ? this.prevSample / denom : 0.0;
      if (this.hasNegCross) {
        const period = this.samplesSinceNeg - this.lastNegFrac + frac;
        this.pushPeriod(period);
      }
      this.samplesSinceNeg = 0;
      this.lastNegFrac = frac;
      this.hasNegCross = true;
    }

    if (this.samplesSincePos > 48000) {
      this.hasPosCross = false;
      this.samplesSincePos = 0;
    }
    if (this.samplesSinceNeg > 48000) {
      this.hasNegCross = false;
      this.samplesSinceNeg = 0;
    }

    this.prevSample = y2;
  }

  computeRobustPeriod(): number {
    if (this.ringCount === 0) return this.expectedPeriod;
    const sorted = this.periodRing.slice(0, this.ringCount).sort((a, b) => a - b);
    if (this.ringCount < 4) {
      return sorted[this.ringCount >> 1];
    }
    const trim = Math.max(1, this.ringCount >> 2);
    let sum = 0.0;
    let cnt = 0;
    for (let i = trim; i < this.ringCount - trim; ++i) {
      sum += sorted[i];
      ++cnt;
    }
    return cnt > 0 ? sum / cnt : sorted[this.ringCount >> 1];
  }
}

export class PolyPitchDetector {
  static readonly kMaxStrings = 8;

  private fs = 48000.0;
  private decimatedFs = 24000.0;
  private decimationFactor = 2;
  private decimationCounter = 0;

  private windowSizeSamples = 600;
  private samplesInWindow = 0;

  private numActiveStrings = 6;
  private isBassMode = false;
  private currentPresetIdx = -1;
  private currentA4Ref = 440.0;
  private sensitivity = 1.0;

  private polyModeConfirmed = false;
  private polyEnterCount = 0;
  private polyExitCount = 0;

  private prevTotalRms = 0.0;
  private attackBlankFrames = 0;

  private aa_b0 = 1.0;
  private aa_b1 = 0.0;
  private aa_b2 = 0.0;
  private aa_a1 = 0.0;
  private aa_a2 = 0.0;
  private aa_x1 = 0.0;
  private aa_x2 = 0.0;
  private aa_y1 = 0.0;
  private aa_y2 = 0.0;

  private channels: ResonatorChannel[] = Array.from({ length: 8 }, () => new ResonatorChannel());
  private results: PolyStringResult[] = Array.from({ length: 8 }, () => ({
    cents: 0.0,
    energy: 0.0,
    active: false,
  }));

  prepare(sampleRate: number): void {
    this.fs = sampleRate > 8000.0 ? sampleRate : 48000.0;
    this.decimationFactor = this.fs >= 70000.0 ? 4 : this.fs >= 36000.0 ? 2 : 1;
    this.decimatedFs = this.fs / this.decimationFactor;
    this.decimationCounter = 0;

    this.windowSizeSamples = Math.max(256, Math.round(this.decimatedFs * 0.025));
    this.samplesInWindow = 0;

    this.setupAntiAliasingFilter();

    for (const ch of this.channels) ch.reset();
    for (const r of this.results) {
      r.cents = 0.0;
      r.energy = 0.0;
      r.active = false;
    }

    this.polyModeConfirmed = false;
    this.polyEnterCount = 0;
    this.polyExitCount = 0;
    this.prevTotalRms = 0.0;
    this.attackBlankFrames = 0;
    this.currentPresetIdx = -1;
  }

  setTargetTuning(
    isBass: boolean,
    presetIdx: number,
    numStrings: number,
    a4Ref: number,
    activeMidi: readonly number[],
    activeOffsets: readonly number[],
    count: number
  ): void {
    const a4Changed = Math.abs(this.currentA4Ref - a4Ref) > 0.02;
    if (
      this.isBassMode === isBass &&
      this.currentPresetIdx === presetIdx &&
      this.numActiveStrings === numStrings &&
      !a4Changed
    ) {
      return;
    }

    this.isBassMode = isBass;
    this.currentPresetIdx = presetIdx;
    this.numActiveStrings = numStrings;
    this.currentA4Ref = a4Ref;

    const clampedCount = Math.max(0, Math.min(PolyPitchDetector.kMaxStrings, count));
    for (let i = 0; i < clampedCount; ++i) {
      const exactMidi = activeMidi[i] + activeOffsets[i] * 0.01;
      const freqHz = a4Ref * Math.pow(2.0, (exactMidi - 69.0) / 12.0);
      this.channels[i].configure(freqHz, this.decimatedFs);
      this.channels[i].reset();
    }
    this.numActiveStrings = clampedCount;
  }

  setSensitivity(accuracyPercent: number): void {
    this.sensitivity = Math.max(0.1, Math.min(1.0, accuracyPercent * 0.01));
  }

  process(samples: Float32Array): void {
    if (samples.length === 0 || this.numActiveStrings <= 0) return;

    for (let i = 0; i < samples.length; ++i) {
      const aaOut = this.processAntiAliasing(samples[i]);
      if (++this.decimationCounter >= this.decimationFactor) {
        this.decimationCounter = 0;
        for (let s = 0; s < this.numActiveStrings; ++s) {
          this.channels[s].processSample(aaOut);
        }
        if (++this.samplesInWindow >= this.windowSizeSamples) {
          this.samplesInWindow = 0;
          this.evaluateWindow();
        }
      }
    }
  }

  isPolyphonic(): boolean {
    return this.polyModeConfirmed;
  }

  getStringResults(): readonly PolyStringResult[] {
    return this.results;
  }

  private setupAntiAliasingFilter(): void {
    const cutoff = Math.min(1400.0, this.decimatedFs * 0.42);
    const omega = (2.0 * Math.PI * cutoff) / this.fs;
    const sn = Math.sin(omega);
    const cs = Math.cos(omega);
    const alpha = sn / Math.SQRT2;
    const invA0 = 1.0 / (1.0 + alpha);

    this.aa_b0 = (1.0 - cs) * 0.5 * invA0;
    this.aa_b1 = (1.0 - cs) * invA0;
    this.aa_b2 = this.aa_b0;
    this.aa_a1 = -2.0 * cs * invA0;
    this.aa_a2 = (1.0 - alpha) * invA0;
    this.aa_x1 = this.aa_x2 = this.aa_y1 = this.aa_y2 = 0.0;
  }

  private processAntiAliasing(x: number): number {
    const y =
      this.aa_b0 * x +
      this.aa_b1 * this.aa_x1 +
      this.aa_b2 * this.aa_x2 -
      this.aa_a1 * this.aa_y1 -
      this.aa_a2 * this.aa_y2;
    this.aa_x2 = this.aa_x1;
    this.aa_x1 = x;
    this.aa_y2 = this.aa_y1;
    this.aa_y1 = y;
    return y;
  }

  private evaluateWindow(): void {
    let maxChannelRms = 0.0;
    let secondMaxRms = 0.0;
    let sumSqRms = 0.0;
    const rawRms = new Array<number>(PolyPitchDetector.kMaxStrings).fill(0.0);

    for (let s = 0; s < this.numActiveStrings; ++s) {
      const ch = this.channels[s];
      let rms =
        ch.energySamples > 0 ? Math.sqrt(ch.energySum / ch.energySamples) : 0.0;
      const highStringBoost =
        1.0 +
        (s / Math.max(1, this.numActiveStrings - 1)) * (this.isBassMode ? 0.55 : 0.95);
      rms *= highStringBoost;

      rawRms[s] = rms;
      sumSqRms += rms * rms;

      if (rms > maxChannelRms) {
        secondMaxRms = maxChannelRms;
        maxChannelRms = rms;
      } else if (rms > secondMaxRms) {
        secondMaxRms = rms;
      }

      ch.energySum = 0.0;
      ch.energySamples = 0;
    }

    const totalRms = Math.sqrt(sumSqRms);
    if (totalRms > Math.max(0.0008, this.prevTotalRms * 2.2)) {
      this.attackBlankFrames = 3;
    }
    this.prevTotalRms = totalRms * 0.75 + this.prevTotalRms * 0.25;
    if (this.attackBlankFrames > 0) --this.attackBlankFrames;

    const absFloor = this.isBassMode ? 0.00009 : 0.000055;
    const triggerGateThresh = Math.max(absFloor * 1.8, maxChannelRms * 0.26);
    const displayGateThresh = Math.max(
      absFloor,
      maxChannelRms * (this.polyModeConfirmed ? 0.055 : 0.14)
    );

    const slowDown = (1.0 - this.sensitivity) / 0.9;
    const centsSmooth = 0.24 - slowDown * 0.14;

    let triggerCandidateStrings = 0;
    let sustainedActiveStrings = 0;

    for (let s = 0; s < PolyPitchDetector.kMaxStrings; ++s) {
      if (s >= this.numActiveStrings) {
        this.results[s] = { active: false, cents: 0.0, energy: 0.0 };
        continue;
      }

      const ch = this.channels[s];
      const rms = rawRms[s];
      ch.smoothedEnergy = ch.smoothedEnergy * 0.65 + rms * 0.35;

      const minCrossings = ch.expectedPeriod > 220.0 ? 1 : 2;
      const hasTriggerEnergy =
        rms >= triggerGateThresh &&
        ch.ringCount >= 2 &&
        ch.newPeriodsInWindow >= minCrossings;
      const hasDisplayEnergy =
        rms >= displayGateThresh &&
        ch.ringCount >= 1 &&
        (ch.newPeriodsInWindow >= 1 || ch.holdFrames > 0);
      ch.newPeriodsInWindow = 0;

      if (hasTriggerEnergy) ++triggerCandidateStrings;

      if (hasDisplayEnergy) {
        const measuredPeriod = ch.computeRobustPeriod();
        const measuredFreq = this.decimatedFs / Math.max(1.0, measuredPeriod);
        const rawCents = 1200.0 * Math.log2(measuredFreq / ch.targetFreqHz);
        const clampedCents = Math.max(-50.0, Math.min(50.0, rawCents));

        if (!ch.hasCentsInit) {
          ch.smoothedCents = clampedCents;
          ch.hasCentsInit = true;
        } else {
          const diff = Math.abs(clampedCents - ch.smoothedCents);
          const adaptiveAlpha = diff < 1.2 ? centsSmooth * 0.55 : centsSmooth;
          ch.smoothedCents += (clampedCents - ch.smoothedCents) * adaptiveAlpha;
        }

        ch.holdFrames = this.polyModeConfirmed ? 26 : 10;
        ch.isLocked = true;
      } else {
        if (ch.holdFrames > 0) {
          --ch.holdFrames;
        } else {
          ch.isLocked = false;
          ch.hasCentsInit = false;
          ch.ringCount = 0;
        }
      }

      if (ch.isLocked) ++sustainedActiveStrings;

      this.results[s].active = ch.isLocked;
      this.results[s].cents = ch.smoothedCents;
      this.results[s].energy = ch.smoothedEnergy;
    }

    const singleStringDominant =
      maxChannelRms < absFloor * 1.5 || secondMaxRms < maxChannelRms * 0.24;

    if (!this.polyModeConfirmed) {
      if (
        this.attackBlankFrames === 0 &&
        triggerCandidateStrings >= 3 &&
        !singleStringDominant
      ) {
        if (++this.polyEnterCount >= 3) {
          this.polyModeConfirmed = true;
          this.polyExitCount = 0;
        }
      } else {
        this.polyEnterCount = 0;
      }
    } else {
      if (
        sustainedActiveStrings <= 1 ||
        maxChannelRms < absFloor ||
        (singleStringDominant && triggerCandidateStrings <= 1)
      ) {
        const exitStep = singleStringDominant ? 2 : 1;
        this.polyExitCount += exitStep;
        if (this.polyExitCount >= 8) {
          this.polyModeConfirmed = false;
          this.polyEnterCount = 0;
          for (let s = 0; s < PolyPitchDetector.kMaxStrings; ++s) {
            this.channels[s].holdFrames = 0;
            this.channels[s].isLocked = false;
            this.results[s].active = false;
          }
        }
      } else {
        this.polyExitCount = 0;
      }
    }
  }
}
