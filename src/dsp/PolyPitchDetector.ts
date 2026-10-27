export interface PolyStringResult {
  cents: number;
  energy: number;
  active: boolean;
}

function inharmOffsetCents(baseFreqHz: number, harmOrder: number): number {
  if (harmOrder > 1) return -1.35;
  if (baseFreqHz < 90.0) return 3.45;
  if (baseFreqHz < 120.0) return 2.8;
  if (baseFreqHz < 160.0) return 2.95;
  if (baseFreqHz < 210.0) return 1.65;
  return 1.0;
}

class ResonatorChannel {
  static readonly kRingSize = 32;

  baseFreqHz = 100.0;
  targetFreqHz = 100.0;
  sampleRateDec = 24000.0;
  defaultNotchHz = 200.0;
  harmOrder = 1;
  colliding3xIdx = -1;
  colliding4xIdx = -1;

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
  peakEnergy = 0.0;
  framesSincePeak = 100;
  channelBlankFrames = 0;

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
    this.peakEnergy = 0.0;
    this.framesSincePeak = 100;
    this.channelBlankFrames = 0;
    this.smoothedCents = 0.0;
    this.smoothedEnergy = 0.0;
    this.holdFrames = 0;
    this.lockAgeFrames = 0;
    this.isLocked = false;
    this.hasCentsInit = false;
  }

  trimOldPeriods(keepCount: number): void {
    if (this.ringCount <= keepCount) return;
    const temp = new Array<number>(keepCount).fill(0.0);
    for (let i = 0; i < keepCount; ++i) {
      const srcIdx =
        (this.ringWriteIdx + ResonatorChannel.kRingSize - keepCount + i) &
        (ResonatorChannel.kRingSize - 1);
      temp[i] = this.periodRing[srcIdx];
    }
    this.periodRing.fill(0.0);
    for (let i = 0; i < keepCount; ++i) {
      this.periodRing[i] = temp[i];
    }
    this.ringWriteIdx = keepCount & (ResonatorChannel.kRingSize - 1);
    this.ringCount = keepCount;
  }

  setNotchFilter(notchHz: number, qVal: number): void {
    const twoPi = 2.0 * Math.PI;
    const clampedNotch = Math.max(20.0, Math.min(this.sampleRateDec * 0.44, notchHz));
    const ntOmega = (twoPi * clampedNotch) / this.sampleRateDec;
    const ntSn = Math.sin(ntOmega);
    const ntCs = Math.cos(ntOmega);
    const ntAlpha = ntSn / (2.0 * qVal);
    const ntInvA0 = 1.0 / (1.0 + ntAlpha);

    this.nt_b0 = ntInvA0;
    this.nt_b1 = -2.0 * ntCs * ntInvA0;
    this.nt_b2 = ntInvA0;
    this.nt_a1 = this.nt_b1;
    this.nt_a2 = (1.0 - ntAlpha) * ntInvA0;
  }

  configure(
    baseHz: number,
    notchHz: number,
    harmonicMultiplier: number,
    colliding3xStringIdx: number,
    colliding4xStringIdx: number,
    resonatorQ: number,
    sampleRateDec: number
  ): void {
    this.sampleRateDec = sampleRateDec;
    this.baseFreqHz = Math.max(20.0, Math.min(sampleRateDec * 0.42, baseHz));
    this.harmOrder = Math.max(1, harmonicMultiplier);
    this.colliding3xIdx = colliding3xStringIdx;
    this.colliding4xIdx = colliding4xStringIdx;
    this.targetFreqHz = Math.max(
      20.0,
      Math.min(sampleRateDec * 0.42, this.baseFreqHz * this.harmOrder)
    );
    this.defaultNotchHz = notchHz;

    this.expectedPeriod = sampleRateDec / this.targetFreqHz;
    this.inharmCents = inharmOffsetCents(this.baseFreqHz, this.harmOrder);
    this.minValidPeriod = this.expectedPeriod * 0.9685;
    this.maxValidPeriod = this.expectedPeriod * 1.0325;

    const twoPi = 2.0 * Math.PI;

    const hpCutoff = Math.max(18.0, Math.min(sampleRateDec * 0.38, this.targetFreqHz * 0.8));
    this.hp_coeff = Math.exp((-twoPi * hpCutoff) / sampleRateDec);

    this.setNotchFilter(this.defaultNotchHz, this.harmOrder > 1 ? 6.0 : 8.0);

    const lpCutoff = Math.max(35.0, Math.min(sampleRateDec * 0.45, this.targetFreqHz * 1.22));
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

    const bpOmega = (twoPi * this.targetFreqHz) / sampleRateDec;
    const poleR = Math.exp(-bpOmega / (2.0 * resonatorQ));
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

  processSample(x: number, globalBlank: boolean): void {
    const recordPeriods = !globalBlank && this.channelBlankFrames === 0;

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
    const windowLen = this.harmOrder > 1 ? Math.min(this.ringCount, 16) : this.ringCount;
    const sub: number[] = new Array(windowLen);
    for (let i = 0; i < windowLen; ++i) {
      const srcIdx =
        (this.ringWriteIdx + ResonatorChannel.kRingSize - windowLen + i) &
        (ResonatorChannel.kRingSize - 1);
      sub[i] = this.periodRing[srcIdx];
    }
    const sorted = sub.sort((a, b) => a - b);
    if (windowLen >= 4) {
      const q1 = windowLen >> 2;
      const q3 = windowLen - 1 - q1;
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
        period: cnt > 0 ? sum / cnt : sorted[windowLen >> 1],
        iqrSpreadCents,
      };
    }
    return { period: sorted[windowLen >> 1], iqrSpreadCents: 0.0 };
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
    const baseFreqs = new Array<number>(clampedCount).fill(100.0);
    for (let i = 0; i < clampedCount; ++i) {
      const exactMidi = activeMidi[i] + activeOffsets[i] * 0.01;
      baseFreqs[i] = a4Ref * Math.pow(2.0, (exactMidi - 69.0) / 12.0);
    }

    for (let i = 0; i < clampedCount; ++i) {
      const baseHz = baseFreqs[i];
      let harmOrder = 1;
      let colliding3xIdx = -1;
      let colliding4xIdx = -1;
      let resonatorQ = 18.0;

      for (let low = 0; low < i; ++low) {
        const ratio = baseHz / baseFreqs[low];
        if (Math.abs(ratio - 3.0) < 0.035 && harmOrder === 1) {
          harmOrder = 2;
          colliding3xIdx = low;
          resonatorQ = 14.5;
        } else if (Math.abs(ratio - 4.0) < 0.035 && harmOrder === 1) {
          harmOrder = 2;
          colliding4xIdx = low;
          resonatorQ = 18.5;
        }
      }

      const effectiveHz = baseHz * harmOrder;
      let notchFreqHz = effectiveHz * 2.0;
      if (harmOrder === 1 && i >= 2) {
        const low2Harm2 = 2.0 * baseFreqs[i - 2];
        if (low2Harm2 > effectiveHz * 1.07 && low2Harm2 < effectiveHz * 1.25) {
          notchFreqHz = low2Harm2;
        }
      } else if (harmOrder === 2 && i >= 1) {
        notchFreqHz = 3.0 * baseFreqs[i - 1];
      }

      this.channels[i].configure(
        baseHz,
        notchFreqHz,
        harmOrder,
        colliding3xIdx,
        colliding4xIdx,
        resonatorQ,
        this.decimatedFs
      );
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

        const globalBlank = this.attackBlankFrames > 0;
        for (let s = 0; s < this.numActiveStrings; ++s) {
          this.channels[s].processSample(aaOut, globalBlank);
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
    const cutoff = 1800.0;
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

    const isPickAttack = frameInputRms > this.smoothInputRms * 1.55 && frameInputRms > 0.0015;
    this.smoothInputRms = this.smoothInputRms * 0.72 + frameInputRms * 0.28;

    if (isPickAttack) {
      this.attackBlankFrames = 5;
      for (let s = 0; s < this.numActiveStrings; ++s) {
        const ch = this.channels[s];
        ch.framesSincePeak = 0;
        ch.peakEnergy = 0.0;
        if (ch.harmOrder > 1) {
          ch.trimOldPeriods(4);
        }
      }
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
      const chAttack = r > ch.rawRmsSmooth * 1.5 && r > 0.00025;

      if (r > ch.rawRmsSmooth) {
        ch.rawRmsSmooth = ch.rawRmsSmooth * 0.3 + r * 0.7;
      } else {
        ch.rawRmsSmooth = ch.rawRmsSmooth * 0.86 + r * 0.14;
      }
      rawRms[s] = ch.rawRmsSmooth;

      if (chAttack) {
        ch.channelBlankFrames = 4;
        ch.peakEnergy = ch.rawRmsSmooth;
        ch.framesSincePeak = 0;
        if (ch.harmOrder > 1) {
          ch.trimOldPeriods(4);
        }
      } else {
        if (ch.channelBlankFrames > 0) {
          --ch.channelBlankFrames;
        } else {
          if (ch.rawRmsSmooth > ch.peakEnergy) ch.peakEnergy = ch.rawRmsSmooth;
          else ch.peakEnergy *= 0.992;
          ++ch.framesSincePeak;
        }
      }

      const highStringGain = 1.0 + (s / denomStrings) * 0.9;
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

    const absFloor = 0.00004 + (1.0 - this.sensitivity) * 0.00003;
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
        const ratio = ch.baseFreqHz / this.channels[low].baseFreqHz;
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

      if (ch.harmOrder === 2 && ch.colliding3xIdx >= 0) {
        const lowIdx = ch.colliding3xIdx;
        const lowCh = this.channels[lowIdx];
        const lowStringRinging = rawRms[lowIdx] > absFloor * 3.0;
        const pitchSeparated =
          lowCh.hasCentsInit &&
          (Math.abs(lowCh.smoothedCents) > 2.5 ||
            (ch.hasCentsInit && Math.abs(ch.smoothedCents - lowCh.smoothedCents) > 2.5));
        if (lowStringRinging && pitchSeparated) {
          const lowActualHz = lowCh.targetFreqHz * Math.pow(2.0, lowCh.smoothedCents / 1200.0);
          ch.setNotchFilter(lowActualHz * 6.0, 18.0);
        } else {
          ch.setNotchFilter(ch.defaultNotchHz, 6.0);
        }
      }

      const activeThresh = this.polyModeConfirmed
        ? Math.max(absFloor, maxChannelRms * 0.025)
        : displayGateThresh;
      const hasRecentCycles =
        ch.newPeriodsInWindow >= 1 || (ch.ringCount >= 4 && chRms[s] >= activeThresh * 1.2);
      const suppressHarmonicGhost = !this.polyModeConfirmed && isHarmonicOfLower[s];

      if (!suppressHarmonicGhost && chRms[s] >= activeThresh && hasRecentCycles && ch.ringCount >= 4) {
        const { period, iqrSpreadCents } = ch.computeStablePeriod();
        const isBlanked = this.attackBlankFrames > 0 || ch.channelBlankFrames > 0;

        let bassSwamping = false;
        const lowRefIdx = ch.colliding3xIdx >= 0 ? ch.colliding3xIdx : ch.colliding4xIdx;
        if (ch.harmOrder > 1 && lowRefIdx >= 0) {
          const lowRms = rawRms[lowRefIdx];
          if (
            ch.framesSincePeak > 80 &&
            lowRms > ch.rawRmsSmooth * 3.0 &&
            ch.rawRmsSmooth < ch.peakEnergy * 0.16
          ) {
            bassSwamping = true;
          }
        }

        const allowUpdate =
          ch.harmOrder > 1
            ? !isBlanked &&
              !bassSwamping &&
              ch.framesSincePeak <= 115 &&
              ch.rawRmsSmooth >= ch.peakEnergy * 0.1
            : !isBlanked && ch.rawRmsSmooth >= ch.peakEnergy * 0.08;
        const maxIqr = ch.harmOrder > 1 ? 6.5 : 8.0;

        if (allowUpdate && iqrSpreadCents < maxIqr) {
          const measuredHz = this.decimatedFs / Math.max(1.0, period);
          const rawCents = 1200.0 * Math.log2(measuredHz / ch.targetFreqHz) + ch.inharmCents;
          const clampedCents = Math.max(-50.0, Math.min(50.0, rawCents));

          if (!ch.hasCentsInit) {
            ch.smoothedCents = clampedCents;
            ch.hasCentsInit = true;
            ch.lockAgeFrames = 1;
          } else {
            ++ch.lockAgeFrames;
            const coherenceWeight = iqrSpreadCents < 3.5 ? 1.0 : 0.6;
            if (ch.harmOrder > 1) {
              const settleAlpha =
                ch.framesSincePeak <= 7
                  ? 0.16
                  : ch.framesSincePeak <= 45
                    ? 0.3
                    : 0.14 + curve * 0.18;
              ch.smoothedCents += (clampedCents - ch.smoothedCents) * (settleAlpha * coherenceWeight);
            } else if (ch.lockAgeFrames <= 6) {
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
          ch.holdFrames = 55;
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
