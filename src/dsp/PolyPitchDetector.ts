export interface PolyStringResult {
  cents: number;
  energy: number;
  active: boolean;
}

const kMaxStrings = 8;

function inharmOffsetCents(baseFreqHz: number, harmOrder: number): number {
  if (harmOrder > 1) return 0.0;
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
  currentNotchHz = 200.0;
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

  periodRing = new Float32Array(ResonatorChannel.kRingSize);
  ringWriteIdx = 0;
  ringCount = 0;
  newPeriodsInWindow = 0;

  prevSample = 0.0;
  samplesSincePos = 0;
  prevPosFrac = 0.0;
  hasPosCross = false;

  samplesSinceNeg = 0;
  prevNegFrac = 0.0;
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
    this.flushPeriods();
    this.prevSample = 0.0;
    this.samplesSincePos = 0;
    this.prevPosFrac = 0.0;
    this.hasPosCross = false;
    this.samplesSinceNeg = 0;
    this.prevNegFrac = 0.0;
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

  flushPeriods(): void {
    this.periodRing.fill(0.0);
    this.ringWriteIdx = 0;
    this.ringCount = 0;
    this.newPeriodsInWindow = 0;
    this.hasPosCross = false;
    this.hasNegCross = false;
  }

  setNotchFilter(notchHz: number, qVal: number): void {
    const clampedNotch = Math.max(20.0, Math.min(this.sampleRateDec * 0.44, notchHz));
    if (Math.abs(clampedNotch - this.currentNotchHz) < 0.15) return;
    this.currentNotchHz = clampedNotch;

    const twoPi = Math.PI * 2.0;
    const ntOmega = (twoPi * clampedNotch) / this.sampleRateDec;
    const ntSn = Math.sin(ntOmega);
    const ntCs = Math.cos(ntOmega);
    const ntAlpha = ntSn / (2.0 * qVal);
    const ntInvA0 = 1.0 / (1.0 + ntAlpha);

    this.nt_b0 = 1.0 * ntInvA0;
    this.nt_b1 = -2.0 * ntCs * ntInvA0;
    this.nt_b2 = 1.0 * ntInvA0;
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
    fsDec: number
  ): void {
    this.sampleRateDec = fsDec;
    this.baseFreqHz = Math.max(20.0, Math.min(this.sampleRateDec * 0.42, baseHz));
    this.harmOrder = Math.max(1, harmonicMultiplier);
    this.colliding3xIdx = colliding3xStringIdx;
    this.colliding4xIdx = colliding4xStringIdx;
    this.targetFreqHz = Math.max(20.0, Math.min(this.sampleRateDec * 0.42, this.baseFreqHz * this.harmOrder));
    this.defaultNotchHz = notchHz;
    this.currentNotchHz = -1.0;

    this.expectedPeriod = this.sampleRateDec / this.targetFreqHz;
    this.inharmCents = inharmOffsetCents(this.baseFreqHz, this.harmOrder);

    this.minValidPeriod = this.expectedPeriod * 0.9685;
    this.maxValidPeriod = this.expectedPeriod * 1.0325;

    const twoPi = Math.PI * 2.0;

    const hpCutoff = Math.max(18.0, Math.min(this.sampleRateDec * 0.38, this.targetFreqHz * 0.8));
    this.hp_coeff = Math.exp((-twoPi * hpCutoff) / this.sampleRateDec);

    this.setNotchFilter(this.defaultNotchHz, this.harmOrder > 1 ? 6.0 : 8.0);

    const lpCutoff = Math.max(35.0, Math.min(this.sampleRateDec * 0.45, this.targetFreqHz * 1.22));
    const lpOmega = (twoPi * lpCutoff) / this.sampleRateDec;
    const lpSn = Math.sin(lpOmega);
    const lpCs = Math.cos(lpOmega);
    const lpAlpha = lpSn / Math.SQRT2;
    const lpInvA0 = 1.0 / (1.0 + lpAlpha);

    this.lp_b0 = (1.0 - lpCs) * 0.5 * lpInvA0;
    this.lp_b1 = (1.0 - lpCs) * lpInvA0;
    this.lp_b2 = this.lp_b0;
    this.lp_a1 = -2.0 * lpCs * lpInvA0;
    this.lp_a2 = (1.0 - lpAlpha) * lpInvA0;

    const omega0 = (twoPi * this.targetFreqHz) / this.sampleRateDec;
    const poleR = Math.exp(-omega0 / (2.0 * resonatorQ));
    const poleR2 = poleR * poleR;

    this.bp_a1 = -2.0 * poleR * Math.cos(omega0);
    this.bp_a2 = poleR2;
    this.bp_b0 = 0.5 * (1.0 - poleR2);
    this.bp_b2 = -this.bp_b0;

    this.reset();
  }

  pushPeriod(period: number): void {
    if (period >= this.minValidPeriod && period <= this.maxValidPeriod) {
      this.periodRing[this.ringWriteIdx] = period;
      this.ringWriteIdx = (this.ringWriteIdx + 1) & (ResonatorChannel.kRingSize - 1);
      if (this.ringCount < ResonatorChannel.kRingSize) {
        this.ringCount++;
      }
      this.newPeriodsInWindow++;
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

    const y1 = this.bp_b0 * y0 + this.bp_b2 * this.s1_x2 - this.bp_a1 * this.s1_y1 - this.bp_a2 * this.s1_y2;
    this.s1_x2 = this.s1_x1;
    this.s1_x1 = y0;
    this.s1_y2 = this.s1_y1;
    this.s1_y1 = y1;

    const y2 = this.bp_b0 * y1 + this.bp_b2 * this.s2_x2 - this.bp_a1 * this.s2_y1 - this.bp_a2 * this.s2_y2;
    this.s2_x2 = this.s2_x1;
    this.s2_x1 = y1;
    this.s2_y2 = this.s2_y1;
    this.s2_y1 = y2;

    const y3 = this.bp_b0 * y2 + this.bp_b2 * this.s3_x2 - this.bp_a1 * this.s3_y1 - this.bp_a2 * this.s3_y2;
    this.s3_x2 = this.s3_x1;
    this.s3_x1 = y2;
    this.s3_y2 = this.s3_y1;
    this.s3_y1 = y3;

    this.energySum += y3 * y3;
    this.energySamples++;

    if (!recordPeriods) {
      this.samplesSincePos = 0;
      this.samplesSinceNeg = 0;
      this.hasPosCross = false;
      this.hasNegCross = false;
      this.prevSample = y3;
      return;
    }

    this.samplesSincePos++;
    this.samplesSinceNeg++;

    if (this.prevSample < 0.0 && y3 >= 0.0) {
      const denom = y3 - this.prevSample;
      const frac = denom > 1e-12 ? -this.prevSample / denom : 0.0;
      if (this.hasPosCross) {
        const period = this.samplesSincePos - 1 + frac + (1.0 - this.prevPosFrac);
        this.pushPeriod(period);
      }
      this.samplesSincePos = 0;
      this.prevPosFrac = frac;
      this.hasPosCross = true;
    } else if (this.prevSample > 0.0 && y3 <= 0.0) {
      const denom = this.prevSample - y3;
      const frac = denom > 1e-12 ? this.prevSample / denom : 0.0;
      if (this.hasNegCross) {
        const period = this.samplesSinceNeg - 1 + frac + (1.0 - this.prevNegFrac);
        this.pushPeriod(period);
      }
      this.samplesSinceNeg = 0;
      this.prevNegFrac = frac;
      this.hasNegCross = true;
    }

    this.prevSample = y3;
  }

  computeStablePeriod(): { period: number; iqrSpreadCents: number } {
    if (this.ringCount === 0) {
      return { period: this.expectedPeriod, iqrSpreadCents: 0.0 };
    }

    const sorted = Array.from(this.periodRing.subarray(0, this.ringCount)).sort((a, b) => a - b);

    if (this.ringCount >= 4) {
      const q1 = Math.floor(this.ringCount / 4);
      const q3 = this.ringCount - 1 - q1;
      const pLow = sorted[q1];
      const pHigh = sorted[q3];
      const iqrSpreadCents = pLow > 1.0 ? 1200.0 * Math.log2(pHigh / pLow) : 99.0;

      let sum = 0.0;
      let cnt = 0;
      for (let i = q1; i <= q3; i++) {
        sum += sorted[i];
        cnt++;
      }
      return {
        period: cnt > 0 ? sum / cnt : sorted[Math.floor(this.ringCount / 2)],
        iqrSpreadCents,
      };
    }

    return { period: sorted[Math.floor(this.ringCount / 2)], iqrSpreadCents: 0.0 };
  }
}

export class PolyPitchDetector {
  private channels: ResonatorChannel[] = Array.from({ length: kMaxStrings }, () => new ResonatorChannel());
  private results: PolyStringResult[] = Array.from({ length: kMaxStrings }, () => ({
    cents: 0,
    energy: 0,
    active: false,
  }));

  private lastMidiNotes = new Int32Array(kMaxStrings);
  private lastCentOffsets = new Float32Array(kMaxStrings);

  private fs = 48000.0;
  private decimatedFs = 24000.0;
  private decimationFactor = 2;
  private decimationCounter = 0;
  private samplesSinceUpdate = 0;
  private updateIntervalSamples = 480;

  private numConfiguredStrings = 0;
  private lastA4Ref = 440.0;
  private sensitivity = 1.0;

  private activeStringCount = 0;
  private polyModeConfirmed = false;
  private polyConfirmFrames = 0;
  private polyHoldFrames = 0;
  private soloOverrideFrames = 0;
  private attackBlankFrames = 0;

  private inputSqSum = 0.0;
  private inputSamplesCount = 0;
  private smoothInputRms = 0.0;

  private aa_b0 = 0.0;
  private aa_b1 = 0.0;
  private aa_b2 = 0.0;
  private aa_a1 = 0.0;
  private aa_a2 = 0.0;
  private aa_x1 = 0.0;
  private aa_x2 = 0.0;
  private aa_y1 = 0.0;
  private aa_y2 = 0.0;

  constructor(sampleRate = 48000.0) {
    this.prepare(sampleRate);
  }

  prepare(sampleRate: number): void {
    this.fs = sampleRate;
    if (sampleRate >= 88200.0) this.decimationFactor = 4;
    else if (sampleRate >= 44100.0) this.decimationFactor = 2;
    else this.decimationFactor = 1;

    this.decimatedFs = this.fs / this.decimationFactor;
    this.decimationCounter = 0;
    this.samplesSinceUpdate = 0;
    this.updateIntervalSamples = Math.floor(this.decimatedFs * 0.02);

    this.resetState();
    this.setupAntiAliasFilter(sampleRate);
    this.numConfiguredStrings = 0;
  }

  resetState(): void {
    for (const ch of this.channels) ch.reset();
    for (let i = 0; i < kMaxStrings; i++) {
      this.results[i] = { cents: 0, energy: 0, active: false };
    }
    this.activeStringCount = 0;
    this.polyModeConfirmed = false;
    this.polyConfirmFrames = 0;
    this.polyHoldFrames = 0;
    this.soloOverrideFrames = 0;
    this.attackBlankFrames = 0;
    this.inputSqSum = 0.0;
    this.inputSamplesCount = 0;
    this.smoothInputRms = 0.0;
    this.aa_x1 = this.aa_x2 = this.aa_y1 = this.aa_y2 = 0.0;
  }

  setSensitivity(accuracyPercent: number): void {
    this.sensitivity = Math.max(0.1, Math.min(1.0, accuracyPercent * 0.01));
  }

  private setupAntiAliasFilter(sampleRate: number): void {
    const cutoff = 1800.0;
    const omega = (2.0 * Math.PI * cutoff) / sampleRate;
    const sn = Math.sin(omega);
    const cs = Math.cos(omega);
    const alpha = sn / Math.SQRT2;
    const invA0 = 1.0 / (1.0 + alpha);

    this.aa_b0 = (1.0 - cs) * 0.5 * invA0;
    this.aa_b1 = (1.0 - cs) * invA0;
    this.aa_b2 = this.aa_b0;
    this.aa_a1 = -2.0 * cs * invA0;
    this.aa_a2 = (1.0 - alpha) * invA0;
  }

  private processAntiAlias(x: number): number {
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

  setTargetTuning(
    _isBass: boolean,
    _presetIdx: number,
    _numStrings: number,
    a4Ref: number,
    midiNotes: number[],
    centOffsets: number[],
    count: number
  ): void {
    const clampedA4 = Math.max(400.0, Math.min(480.0, a4Ref));

    let needsUpdate =
      count !== this.numConfiguredStrings || Math.abs(clampedA4 - this.lastA4Ref) > 0.02;

    if (!needsUpdate) {
      for (let i = 0; i < count; i++) {
        if (
          midiNotes[i] !== this.lastMidiNotes[i] ||
          Math.abs(centOffsets[i] - this.lastCentOffsets[i]) > 0.01
        ) {
          needsUpdate = true;
          break;
        }
      }
    }

    if (!needsUpdate) return;

    this.numConfiguredStrings = count;
    this.lastA4Ref = clampedA4;

    const baseFreqs = new Float32Array(kMaxStrings);
    for (let i = 0; i < this.numConfiguredStrings; i++) {
      this.lastMidiNotes[i] = midiNotes[i];
      this.lastCentOffsets[i] = centOffsets[i];

      const targetMidi = midiNotes[i] + centOffsets[i] * 0.01;
      baseFreqs[i] = clampedA4 * Math.pow(2.0, (targetMidi - 69.0) / 12.0);
    }

    for (let i = 0; i < this.numConfiguredStrings; i++) {
      const baseHz = baseFreqs[i];
      let harmOrder = 1;
      let colliding3xIdx = -1;
      let colliding4xIdx = -1;
      let resonatorQ = 18.0;

      for (let low = 0; low < i; low++) {
        const ratio = baseHz / baseFreqs[low];
        if (Math.abs(ratio - 3.0) < 0.035) {
          colliding3xIdx = low;
          if (harmOrder === 1) {
            harmOrder = 2;
            resonatorQ = 15.0;
          }
        } else if (Math.abs(ratio - 4.0) < 0.035) {
          colliding4xIdx = low;
          if (harmOrder === 1) {
            harmOrder = 2;
            resonatorQ = 16.5;
          }
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
  }

  process(samples: Float32Array, monoHasStrongSingleLock = false): void {
    if (samples.length === 0 || this.numConfiguredStrings <= 0) return;

    for (let i = 0; i < samples.length; i++) {
      const filtered = this.processAntiAlias(samples[i]);
      if (++this.decimationCounter >= this.decimationFactor) {
        this.decimationCounter = 0;
        this.inputSqSum += filtered * filtered;
        this.inputSamplesCount++;

        const globalBlank = this.attackBlankFrames > 0;
        for (let s = 0; s < this.numConfiguredStrings; s++) {
          this.channels[s].processSample(filtered, globalBlank);
        }

        if (++this.samplesSinceUpdate >= this.updateIntervalSamples) {
          this.samplesSinceUpdate = 0;
          this.evaluateChannels(monoHasStrongSingleLock);
        }
      }
    }
  }

  private evaluateChannels(monoHasStrongSingleLock: boolean): void {
    const frameInputRms =
      this.inputSamplesCount > 0 ? Math.sqrt(this.inputSqSum / this.inputSamplesCount) : 0.0;
    this.inputSqSum = 0.0;
    this.inputSamplesCount = 0;

    const isPickAttack = frameInputRms > this.smoothInputRms * 1.55 && frameInputRms > 0.0015;
    this.smoothInputRms = this.smoothInputRms * 0.72 + frameInputRms * 0.28;

    if (isPickAttack) {
      this.attackBlankFrames = 4;
      for (let s = 0; s < this.numConfiguredStrings; s++) {
        const ch = this.channels[s];
        ch.framesSincePeak = 0;
        ch.peakEnergy = 0.0;
        if (ch.harmOrder > 1) {
          ch.channelBlankFrames = 6;
          ch.flushPeriods();
          ch.lockAgeFrames = 0;
        }
      }
    } else if (this.attackBlankFrames > 0) {
      this.attackBlankFrames--;
    }

    let maxChannelRms = 0.0;
    const rawRms = new Float32Array(kMaxStrings);
    const chRms = new Float32Array(kMaxStrings);
    const denomStrings = Math.max(1, this.numConfiguredStrings - 1);

    for (let s = 0; s < this.numConfiguredStrings; s++) {
      const ch = this.channels[s];
      const r = ch.energySamples > 0 ? Math.sqrt(ch.energySum / ch.energySamples) : 0.0;

      const chAttack = r > ch.rawRmsSmooth * 1.55 && r > 0.00025;

      if (r > ch.rawRmsSmooth) ch.rawRmsSmooth = ch.rawRmsSmooth * 0.3 + r * 0.7;
      else ch.rawRmsSmooth = ch.rawRmsSmooth * 0.86 + r * 0.14;
      rawRms[s] = ch.rawRmsSmooth;

      if (chAttack) {
        ch.channelBlankFrames = ch.harmOrder > 1 ? 6 : 3;
        ch.peakEnergy = ch.rawRmsSmooth;
        ch.framesSincePeak = 0;
        if (ch.harmOrder > 1) {
          ch.flushPeriods();
          ch.lockAgeFrames = 0;
        }
      } else if (ch.channelBlankFrames > 0 || this.attackBlankFrames > 0) {
        if (ch.channelBlankFrames > 0) ch.channelBlankFrames--;
        ch.peakEnergy = ch.rawRmsSmooth;
        ch.framesSincePeak = 0;
        if (ch.harmOrder > 1) ch.flushPeriods();
      } else {
        if (ch.rawRmsSmooth > ch.peakEnergy) ch.peakEnergy = ch.rawRmsSmooth;
        ch.framesSincePeak++;
      }

      const highStringGain = 1.0 + (s / denomStrings) * 0.9;
      const boosted = r * highStringGain;

      if (boosted > ch.smoothedEnergy) ch.smoothedEnergy = ch.smoothedEnergy * 0.3 + boosted * 0.7;
      else ch.smoothedEnergy = ch.smoothedEnergy * 0.86 + boosted * 0.14;

      chRms[s] = ch.smoothedEnergy;
      maxChannelRms = Math.max(maxChannelRms, ch.smoothedEnergy);

      ch.energySum = 0.0;
      ch.energySamples = 0;
    }

    const absFloor = 0.00004 + (1.0 - this.sensitivity) * 0.00003;
    const displayGateThresh = Math.max(absFloor, maxChannelRms * 0.045);
    const curve = this.sensitivity * this.sensitivity;

    const isHarmonicOfLower = new Array<boolean>(kMaxStrings).fill(false);
    let maxNonHarmonicRms = 0.0;
    let secondNonHarmonicRms = 0.0;

    for (let s = 0; s < this.numConfiguredStrings; s++) {
      const ch = this.channels[s];
      let harmonicFlag = false;

      for (let low = 0; low < s; low++) {
        if (rawRms[low] < absFloor * 3.0 || rawRms[low] < rawRms[s] * 0.08) continue;

        const ratio = ch.baseFreqHz / this.channels[low].baseFreqHz;
        if (Math.abs(ratio - 2.0) < 0.035 || Math.abs(ratio - 3.0) < 0.035 || Math.abs(ratio - 4.0) < 0.035) {
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

    for (let s = 0; s < this.numConfiguredStrings; s++) {
      const ch = this.channels[s];
      const hasCycles = ch.newPeriodsInWindow >= 1 || ch.ringCount >= 4;
      if (!isHarmonicOfLower[s] && chRms[s] >= nonHarmTriggerThresh && hasCycles) {
        nonHarmonicActiveCount++;
      }
    }

    const minChordStrings = this.numConfiguredStrings <= 4 ? 2 : 3;
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
        this.polyConfirmFrames++;
        if (this.polyConfirmFrames >= 5) {
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
      } else if (singleStringDominant && monoHasStrongSingleLock && this.attackBlankFrames === 0) {
        if (++this.soloOverrideFrames >= 6) {
          this.polyModeConfirmed = false;
          this.polyHoldFrames = 0;
          this.polyConfirmFrames = 0;
          this.soloOverrideFrames = 0;
        }
      } else {
        this.soloOverrideFrames = 0;
        if (this.polyHoldFrames > 0) this.polyHoldFrames--;
        else this.polyModeConfirmed = false;
      }
    }

    let currentlyLocked = 0;
    for (let s = 0; s < this.numConfiguredStrings; s++) {
      const ch = this.channels[s];

      if (ch.harmOrder === 2 && ch.colliding4xIdx < 0 && ch.colliding3xIdx >= 0) {
        const lowCh = this.channels[ch.colliding3xIdx];
        if (
          rawRms[ch.colliding3xIdx] > absFloor * 3.0 &&
          lowCh.hasCentsInit &&
          Math.abs(lowCh.smoothedCents) > 4.0
        ) {
          const lowActualHz = lowCh.targetFreqHz * Math.pow(2.0, lowCh.smoothedCents / 1200.0);
          ch.setNotchFilter(lowActualHz * 6.0, 16.0);
        } else {
          ch.setNotchFilter(ch.defaultNotchHz, 6.0);
        }
      }

      const activeThresh = this.polyModeConfirmed
        ? Math.max(absFloor, maxChannelRms * 0.025)
        : displayGateThresh;
      const minRings = ch.harmOrder > 1 ? 12 : 4;
      const hasRecentCycles =
        ch.newPeriodsInWindow >= 1 || (ch.ringCount >= minRings && chRms[s] >= activeThresh * 1.2);
      const suppressHarmonicGhost = !this.polyModeConfirmed && isHarmonicOfLower[s];

      if (!suppressHarmonicGhost && chRms[s] >= activeThresh && hasRecentCycles && ch.ringCount >= minRings) {
        const { period, iqrSpreadCents } = ch.computeStablePeriod();
        const isBlanked = this.attackBlankFrames > 0 || ch.channelBlankFrames > 0;

        let maxCollidingBassRms = 0.0;
        if (ch.colliding3xIdx >= 0) {
          maxCollidingBassRms = Math.max(maxCollidingBassRms, rawRms[ch.colliding3xIdx]);
        }
        if (ch.colliding4xIdx >= 0) {
          maxCollidingBassRms = Math.max(maxCollidingBassRms, rawRms[ch.colliding4xIdx]);
        }

        const thinSustainValid =
          (ch.framesSincePeak <= 42 && ch.rawRmsSmooth >= ch.peakEnergy * 0.22) ||
          (maxCollidingBassRms < ch.rawRmsSmooth * 1.5 && ch.rawRmsSmooth >= ch.peakEnergy * 0.08);

        const allowUpdate =
          ch.harmOrder > 1
            ? !isBlanked && thinSustainValid
            : !isBlanked && ch.rawRmsSmooth >= ch.peakEnergy * 0.08;
        const maxIqr = ch.harmOrder > 1 ? 5.5 : 8.0;

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
            const coherenceWeight = iqrSpreadCents < 3.0 ? 1.0 : 0.55;
            if (ch.lockAgeFrames <= 6) {
              const initRate = ch.harmOrder > 1 ? 0.28 : 0.35;
              ch.smoothedCents += (clampedCents - ch.smoothedCents) * (initRate * coherenceWeight);
            } else {
              const centsDiff = Math.abs(clampedCents - ch.smoothedCents);
              const minW = centsDiff < 1.5 ? 0.025 : centsDiff < 4.0 ? 0.06 : 0.12;
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

      if (ch.isLocked) currentlyLocked++;
    }

    this.activeStringCount = currentlyLocked;
  }

  isPolyphonic(): boolean {
    return this.polyModeConfirmed;
  }

  getNumActiveStrings(): number {
    return this.activeStringCount;
  }

  getStringResults(): PolyStringResult[] {
    return this.results.slice(0, this.numConfiguredStrings);
  }
}
