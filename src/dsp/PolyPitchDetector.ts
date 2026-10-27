export interface PolyStringResult {
  cents: number;
  energy: number;
  active: boolean;
}

function inharmOffsetCents(freqHz: number): number {
  if (freqHz < 90.0) return 3.45;
  if (freqHz < 120.0) return 2.8;
  if (freqHz < 160.0) return 2.95;
  if (freqHz < 210.0) return 1.65;
  return 1.0;
}

class ResonatorChannel {
  static readonly kRingSize = 32;

  targetFreqHz = 100.0;
  expectedPeriod = 240.0;
  minValidPeriod = 230.0;
  maxValidPeriod = 250.0;
  inharmCents = 0.0;

  hp_coeff = 0.98;
  hp_x1 = 0.0;
  hp_y1 = 0.0;

  nt_b0 = 1.0;
  nt_b1 = 0.0;
  nt_b2 = 1.0;
  nt_a1 = 0.0;
  nt_a2 = 0.0;
  nt_x1 = 0.0;
  nt_x2 = 0.0;
  nt_y1 = 0.0;
  nt_y2 = 0.0;

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
  s3_x1 = 0.0;
  s3_x2 = 0.0;
  s3_y1 = 0.0;
  s3_y2 = 0.0;

  periodRing: number[] = new Array(32).fill(0.0);
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

  rawRmsSmooth = 0.0;
  smoothedCents = 0.0;
  smoothedEnergy = 0.0;
  holdFrames = 0;
  lockAgeFrames = 0;
  isLocked = false;
  hasCentsInit = false;

  reset(): void {
    this.hp_x1 = this.hp_y1 = 0.0;
    this.nt_x1 = this.nt_x2 = this.nt_y1 = this.nt_y2 = 0.0;
    this.lp_x1 = this.lp_x2 = this.lp_y1 = this.lp_y2 = 0.0;
    this.s1_x1 = this.s1_x2 = this.s1_y1 = this.s1_y2 = 0.0;
    this.s2_x1 = this.s2_x2 = this.s2_y1 = this.s2_y2 = 0.0;
    this.s3_x1 = this.s3_x2 = this.s3_y1 = this.s3_y2 = 0.0;
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
    this.rawRmsSmooth = 0.0;
    this.smoothedCents = 0.0;
    this.smoothedEnergy = 0.0;
    this.holdFrames = 0;
    this.lockAgeFrames = 0;
    this.isLocked = false;
    this.hasCentsInit = false;
  }

  configure(freqHz: number, notchHz: number, sampleRateDec: number): void {
    this.targetFreqHz = Math.max(20.0, Math.min(sampleRateDec * 0.42, freqHz));
    this.expectedPeriod = sampleRateDec / this.targetFreqHz;
    this.inharmCents = inharmOffsetCents(this.targetFreqHz);
    this.minValidPeriod = this.expectedPeriod * 0.9685;
    this.maxValidPeriod = this.expectedPeriod * 1.0325;

    const twoPi = 2.0 * Math.PI;

    const hpCutoff = Math.max(18.0, Math.min(sampleRateDec * 0.35, this.targetFreqHz * 0.78));
    this.hp_coeff = Math.exp((-twoPi * hpCutoff) / sampleRateDec);

    const clampedNotch = Math.max(
      this.targetFreqHz * 1.08,
      Math.min(sampleRateDec * 0.44, notchHz)
    );
    const ntOmega = (twoPi * clampedNotch) / sampleRateDec;
    const ntSn = Math.sin(ntOmega);
    const ntCs = Math.cos(ntOmega);
    const ntAlpha = ntSn / (2.0 * 8.0);
    const ntInvA0 = 1.0 / (1.0 + ntAlpha);

    this.nt_b0 = ntInvA0;
    this.nt_b1 = -2.0 * ntCs * ntInvA0;
    this.nt_b2 = ntInvA0;
    this.nt_a1 = this.nt_b1;
    this.nt_a2 = (1.0 - ntAlpha) * ntInvA0;

    const lpCutoff = Math.max(35.0, Math.min(sampleRateDec * 0.45, this.targetFreqHz * 1.26));
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

    const Q = 18.0;
    const bpOmega = (twoPi * this.targetFreqHz) / sampleRateDec;
    const poleR = Math.exp(-bpOmega / (2.0 * Q));
    const poleR2 = poleR * poleR;

    this.bp_a1 = -2.0 * poleR * Math.cos(bpOmega);
    this.bp_a2 = poleR2;
    this.bp_b0 = 0.5 * (1.0 - poleR2);
    this.bp_b2 = -this.bp_b0;

    this.reset();
  }

  pushPeriod(period: number): void {
    if (period >= this.minValidPeriod && period <= this.maxValidPeriod) {
      this.periodRing[this.ringWriteIdx] = period;
      this.ringWriteIdx = (this.ringWriteIdx + 1) & (ResonatorChannel.kRingSize - 1);
      if (this.ringCount < ResonatorChannel.kRingSize) ++this.ringCount;
      ++this.newPeriodsInWindow;
    }
  }

  processSample(x: number, recordPeriods: boolean): void {
    const h0 = x - this.hp_x1 + this.hp_coeff * this.hp_y1;
    this.hp_x1 = x;
    this.hp_y1 = h0;

    const n0 =
      this.nt_b0 * h0 +
      this.nt_b1 * this.nt_x1 +
      this.nt_b2 * this.nt_x2 -
      this.nt_a1 * this.nt_y1 -
      this.nt_a2 * this.nt_y2;
    this.nt_x2 = this.nt_x1;
    this.nt_x1 = h0;
    this.nt_y2 = this.nt_y1;
    this.nt_y1 = n0;

    const y0 =
      this.lp_b0 * n0 +
      this.lp_b1 * this.lp_x1 +
      this.lp_b2 * this.lp_x2 -
      this.lp_a1 * this.lp_y1 -
      this.lp_a2 * this.lp_y2;
    this.lp_x2 = this.lp_x1;
    this.lp_x1 = n0;
    this.lp_y2 = this.lp_y1;
    this.lp_y1 = y0;

    const y1 =
      this.bp_b0 * y0 +
      this.bp_b2 * this.s1_x2 -
      this.bp_a1 * this.s1_y1 -
      this.bp_a2 * this.s1_y2;
    this.s1_x2 = this.s1_x1;
    this.s1_x1 = y0;
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

    const y3 =
      this.bp_b0 * y2 +
      this.bp_b2 * this.s3_x2 -
      this.bp_a1 * this.s3_y1 -
      this.bp_a2 * this.s3_y2;
    this.s3_x2 = this.s3_x1;
    this.s3_x1 = y2;
    this.s3_y2 = this.s3_y1;
    this.s3_y1 = y3;

    this.energySum += y3 * y3;
    ++this.energySamples;

    ++this.samplesSincePos;
    ++this.samplesSinceNeg;

    if (this.prevSample < 0.0 && y3 >= 0.0) {
      const denom = y3 - this.prevSample;
      const frac = denom > 1e-12 ? -this.prevSample / denom : 0.0;
      if (this.hasPosCross && recordPeriods) {
        const period = this.samplesSincePos - 1 + frac + (1.0 - this.lastPosFrac);
        this.pushPeriod(period);
      }
      this.samplesSincePos = 0;
      this.lastPosFrac = frac;
      this.hasPosCross = true;
    } else if (this.prevSample > 0.0 && y3 <= 0.0) {
      const denom = this.prevSample - y3;
      const frac = denom > 1e-12 ? this.prevSample / denom : 0.0;
      if (this.hasNegCross && recordPeriods) {
        const period = this.samplesSinceNeg - 1 + frac + (1.0 - this.lastNegFrac);
        this.pushPeriod(period);
      }
      this.samplesSinceNeg = 0;
      this.lastNegFrac = frac;
      this.hasNegCross = true;
    }

    this.prevSample = y3;
  }

  computeStablePeriod(): { period: number; iqrSpreadCents: number } {
    if (this.ringCount === 0) return { period: this.expectedPeriod, iqrSpreadCents: 0.0 };
    const sorted = this.periodRing.slice(0, this.ringCount).sort((a, b) => a - b);
    if (this.ringCount >= 4) {
      const q1 = this.ringCount >> 2;
      const q3 = this.ringCount - 1 - q1;
      const pLow = sorted[q1];
      const pHigh = sorted[q3];
      const iqrSpreadCents = pLow > 1.0 ? 1200.0 * Math.log2(pHigh / pLow) : 99.0;
      let sum = 0.0;
      let cnt = 0;
      for (let i = q1; i <= q3; ++i) {
        sum += sorted[i];
        ++cnt;
      }
      return {
        period: cnt > 0 ? sum / cnt : sorted[this.ringCount >> 1],
        iqrSpreadCents,
      };
    }
    return { period: sorted[this.ringCount >> 1], iqrSpreadCents: 0.0 };
  }
}

export class PolyPitchDetector {
  static readonly kMaxStrings = 8;

  private fs = 48000.0;
  private decimatedFs = 24000.0;
  private decimationFactor = 2;
  private decimationCounter = 0;

  private windowSizeSamples = 480;
  private samplesInWindow = 0;

  private numActiveStrings = 6;
  private isBassMode = false;
  private currentPresetIdx = -1;
  private currentA4Ref = 440.0;
  private sensitivity = 1.0;

  private polyModeConfirmed = false;
  private polyConfirmFrames = 0;
  private polyHoldFrames = 0;
  private soloOverrideFrames = 0;

  private inputSqSum = 0.0;
  private inputSamplesCount = 0;
  private smoothInputRms = 0.0;
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
    this.decimationFactor = this.fs >= 88200.0 ? 4 : this.fs >= 44100.0 ? 2 : 1;
    this.decimatedFs = this.fs / this.decimationFactor;
    this.decimationCounter = 0;

    this.windowSizeSamples = Math.max(240, Math.round(this.decimatedFs * 0.02));
    this.samplesInWindow = 0;

    this.setupAntiAliasingFilter();

    for (const ch of this.channels) ch.reset();
    for (const r of this.results) {
      r.cents = 0.0;
      r.energy = 0.0;
      r.active = false;
    }

    this.polyModeConfirmed = false;
    this.polyConfirmFrames = 0;
    this.polyHoldFrames = 0;
    this.soloOverrideFrames = 0;
    this.inputSqSum = 0.0;
    this.inputSamplesCount = 0;
    this.smoothInputRms = 0.0;
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
      let notchFreqHz = freqHz * 2.0;
      if (i >= 2) {
        const low2Midi = activeMidi[i - 2] + activeOffsets[i - 2] * 0.01;
        const low2Harm2 = 2.0 * a4Ref * Math.pow(2.0, (low2Midi - 69.0) / 12.0);
        if (low2Harm2 > freqHz * 1.07 && low2Harm2 < freqHz * 1.25) {
          notchFreqHz = low2Harm2;
        }
      }
      this.channels[i].configure(freqHz, notchFreqHz, this.decimatedFs);
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
        this.inputSqSum += aaOut * aaOut;
        ++this.inputSamplesCount;

        const recordPeriods = this.attackBlankFrames === 0;
        for (let s = 0; s < this.numActiveStrings; ++s) {
          this.channels[s].processSample(aaOut, recordPeriods);
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
    const cutoff = 1600.0;
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
    const frameInputRms =
      this.inputSamplesCount > 0 ? Math.sqrt(this.inputSqSum / this.inputSamplesCount) : 0.0;
    this.inputSqSum = 0.0;
    this.inputSamplesCount = 0;

    const isPickAttack = frameInputRms > this.smoothInputRms * 1.55 && frameInputRms > 0.0012;
    this.smoothInputRms = this.smoothInputRms * 0.72 + frameInputRms * 0.28;

    if (isPickAttack) {
      this.attackBlankFrames = 5;
    } else if (this.attackBlankFrames > 0) {
      --this.attackBlankFrames;
    }

    let maxChannelRms = 0.0;
    const rawRms = new Array<number>(PolyPitchDetector.kMaxStrings).fill(0.0);
    const chRms = new Array<number>(PolyPitchDetector.kMaxStrings).fill(0.0);
    const denomStrings = Math.max(1, this.numActiveStrings - 1);

    for (let s = 0; s < this.numActiveStrings; ++s) {
      const ch = this.channels[s];
      const r = ch.energySamples > 0 ? Math.sqrt(ch.energySum / ch.energySamples) : 0.0;

      if (r > ch.rawRmsSmooth) {
        ch.rawRmsSmooth = ch.rawRmsSmooth * 0.3 + r * 0.7;
      } else {
        ch.rawRmsSmooth = ch.rawRmsSmooth * 0.86 + r * 0.14;
      }
      rawRms[s] = ch.rawRmsSmooth;

      const highStringGain = 1.0 + (s / denomStrings) * 0.85;
      const boosted = r * highStringGain;

      if (boosted > ch.smoothedEnergy) {
        ch.smoothedEnergy = ch.smoothedEnergy * 0.3 + boosted * 0.7;
      } else {
        ch.smoothedEnergy = ch.smoothedEnergy * 0.86 + boosted * 0.14;
      }

      chRms[s] = ch.smoothedEnergy;
      maxChannelRms = Math.max(maxChannelRms, ch.smoothedEnergy);

      ch.energySum = 0.0;
      ch.energySamples = 0;
    }

    const absFloor = 0.000045 + (1.0 - this.sensitivity) * 0.00003;
    const displayGateThresh = Math.max(absFloor, maxChannelRms * 0.045);
    const curve = this.sensitivity * this.sensitivity;

    const isHarmonicOfLower = new Array<boolean>(PolyPitchDetector.kMaxStrings).fill(false);
    let maxNonHarmonicRms = 0.0;
    let secondNonHarmonicRms = 0.0;

    for (let s = 0; s < this.numActiveStrings; ++s) {
      const ch = this.channels[s];
      let harmonicFlag = false;
      for (let low = 0; low < s; ++low) {
        if (rawRms[low] < absFloor * 3.0 || rawRms[low] < rawRms[s] * 0.08) continue;
        const ratio = ch.targetFreqHz / this.channels[low].targetFreqHz;
        if (
          Math.abs(ratio - 2.0) < 0.035 ||
          Math.abs(ratio - 3.0) < 0.035 ||
          Math.abs(ratio - 4.0) < 0.035
        ) {
          harmonicFlag = true;
          break;
        }
      }
      isHarmonicOfLower[s] = harmonicFlag;

      if (!harmonicFlag) {
        if (chRms[s] > maxNonHarmonicRms) {
          secondNonHarmonicRms = maxNonHarmonicRms;
          maxNonHarmonicRms = chRms[s];
        } else if (chRms[s] > secondNonHarmonicRms) {
          secondNonHarmonicRms = chRms[s];
        }
      }
    }

    const nonHarmTriggerThresh = Math.max(absFloor * 3.5, maxNonHarmonicRms * 0.24);
    let nonHarmonicActiveCount = 0;

    for (let s = 0; s < this.numActiveStrings; ++s) {
      const ch = this.channels[s];
      const hasCycles = ch.newPeriodsInWindow >= 1 || ch.ringCount >= 4;
      if (!isHarmonicOfLower[s] && chRms[s] >= nonHarmTriggerThresh && hasCycles) {
        ++nonHarmonicActiveCount;
      }
    }

    const minChordStrings = this.numActiveStrings <= 4 ? 2 : 3;
    const singleStringDominant =
      maxNonHarmonicRms > absFloor * 3.0 &&
      secondNonHarmonicRms < maxNonHarmonicRms * 0.18 &&
      nonHarmonicActiveCount <= 1;

    if (!this.polyModeConfirmed) {
      this.soloOverrideFrames = 0;
      if (
        !singleStringDominant &&
        nonHarmonicActiveCount >= minChordStrings &&
        maxNonHarmonicRms >= 0.0012 &&
        this.attackBlankFrames === 0
      ) {
        if (++this.polyConfirmFrames >= 5) {
          this.polyModeConfirmed = true;
          this.polyHoldFrames = 65;
        }
      } else {
        this.polyConfirmFrames = 0;
      }
    } else {
      if (nonHarmonicActiveCount >= 2 && maxNonHarmonicRms >= absFloor * 2.5) {
        this.polyHoldFrames = Math.max(this.polyHoldFrames, 55);
        this.soloOverrideFrames = 0;
      } else if (singleStringDominant && this.attackBlankFrames === 0) {
        if (++this.soloOverrideFrames >= 6) {
          this.polyModeConfirmed = false;
          this.polyHoldFrames = 0;
          this.polyConfirmFrames = 0;
          this.soloOverrideFrames = 0;
        }
      } else {
        this.soloOverrideFrames = 0;
        if (this.polyHoldFrames > 0) {
          --this.polyHoldFrames;
        } else {
          this.polyModeConfirmed = false;
        }
      }
    }

    for (let s = 0; s < PolyPitchDetector.kMaxStrings; ++s) {
      if (s >= this.numActiveStrings) {
        this.results[s] = { active: false, cents: 0.0, energy: 0.0 };
        continue;
      }

      const ch = this.channels[s];
      const activeThresh = this.polyModeConfirmed
        ? Math.max(absFloor, maxChannelRms * 0.03)
        : displayGateThresh;
      const hasRecentCycles =
        ch.newPeriodsInWindow >= 1 || (ch.ringCount >= 4 && chRms[s] >= activeThresh * 1.2);
      const suppressHarmonicGhost = !this.polyModeConfirmed && isHarmonicOfLower[s];

      if (!suppressHarmonicGhost && chRms[s] >= activeThresh && hasRecentCycles && ch.ringCount >= 4) {
        const { period, iqrSpreadCents } = ch.computeStablePeriod();
        if (this.attackBlankFrames === 0 && iqrSpreadCents < 8.0) {
          const measuredHz = this.decimatedFs / Math.max(1.0, period);
          const rawCents = 1200.0 * Math.log2(measuredHz / ch.targetFreqHz) + ch.inharmCents;
          const clampedCents = Math.max(-50.0, Math.min(50.0, rawCents));

          if (!ch.hasCentsInit) {
            ch.smoothedCents = clampedCents;
            ch.hasCentsInit = true;
            ch.lockAgeFrames = 1;
          } else {
            ++ch.lockAgeFrames;
            const coherenceWeight = iqrSpreadCents < 3.5 ? 1.0 : 0.45;
            if (ch.lockAgeFrames <= 6) {
              ch.smoothedCents += (clampedCents - ch.smoothedCents) * (0.35 * coherenceWeight);
            } else {
              const centsDiff = Math.abs(clampedCents - ch.smoothedCents);
              const minW = centsDiff < 1.5 ? 0.025 : centsDiff < 4.0 ? 0.05 : 0.1;
              const maxW = centsDiff < 1.5 ? 0.12 : centsDiff < 4.0 ? 0.22 : 0.35;
              const alpha = (minW + curve * (maxW - minW)) * coherenceWeight;
              ch.smoothedCents += (clampedCents - ch.smoothedCents) * alpha;
            }
          }
        }

        if (ch.hasCentsInit) {
          ch.isLocked = true;
          ch.holdFrames = 45;
        }
      } else if (ch.holdFrames > 0 && this.polyModeConfirmed) {
        --ch.holdFrames;
        if (ch.holdFrames === 0) ch.isLocked = false;
      } else {
        ch.isLocked = false;
        ch.holdFrames = 0;
        ch.lockAgeFrames = 0;
        if (chRms[s] < absFloor * 0.4) {
          ch.ringCount = 0;
          ch.hasCentsInit = false;
        }
      }

      ch.newPeriodsInWindow = 0;
      this.results[s].active = ch.isLocked;
      this.results[s].cents = ch.smoothedCents;
      this.results[s].energy = ch.smoothedEnergy;
    }
  }
}
