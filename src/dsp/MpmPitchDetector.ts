export class MpmPitchDetector {
  private fs: number = 48000.0;
  private decimatedFs: number = 12000.0;
  private decimationFactor: number = 4;
  private decimationCounter: number = 0;

  private sensitivity: number = 1.0;
  private isBassMode: boolean = false;

  private bufferSize: number = 1024;
  private hopSize: number = 256;
  private inputBuffer: Float32Array = new Float32Array(1024);
  private sqBuffer: Float32Array = new Float32Array(1024);
  private nsdf: Float32Array = new Float32Array(512);
  private writePos: number = 0;

  private fullBufferSize: number = 4096;
  private fullBuffer: Float32Array = new Float32Array(4096);
  private fullWritePos: number = 0;

  private detectedFrequency: number = 0.0;
  private lockedFrequency: number = 0.0;
  private candidateFrequency: number = 0.0;
  private candidateHits: number = 0;
  private hasSignalFlag: boolean = false;

  private smoothRms: number = 0.0;
  private silenceSamples: number = 0;

  private lp_b0: number = 0.0;
  private lp_b1: number = 0.0;
  private lp_b2: number = 0.0;
  private lp_a1: number = 0.0;
  private lp_a2: number = 0.0;
  private lp_x1: number = 0.0;
  private lp_x2: number = 0.0;
  private lp_y1: number = 0.0;
  private lp_y2: number = 0.0;
  private hp_x1: number = 0.0;
  private hp_y1: number = 0.0;
  private hp_coeff: number = 0.998;

  prepare(sampleRate: number): void {
    this.fs = sampleRate;

    if (sampleRate >= 88200.0) this.decimationFactor = 8;
    else if (sampleRate >= 44100.0) this.decimationFactor = 4;
    else this.decimationFactor = 2;

    this.decimatedFs = this.fs / this.decimationFactor;

    this.bufferSize = 1024;
    this.hopSize = 256;

    this.fullBufferSize = this.bufferSize * this.decimationFactor;
    this.fullBuffer = new Float32Array(this.fullBufferSize);
    this.fullWritePos = 0;

    this.inputBuffer = new Float32Array(this.bufferSize);
    this.sqBuffer = new Float32Array(this.bufferSize);
    this.nsdf = new Float32Array(this.bufferSize / 2);
    this.writePos = 0;
    this.decimationCounter = 0;

    this.detectedFrequency = 0.0;
    this.lockedFrequency = 0.0;
    this.candidateFrequency = 0.0;
    this.candidateHits = 0;
    this.hasSignalFlag = false;

    this.smoothRms = 0.0;
    this.silenceSamples = 0;

    this.setupFilters(sampleRate);
  }

  setSensitivity(accuracyPercent: number): void {
    const newSens = Math.max(0.1, Math.min(1.0, accuracyPercent * 0.01));
    if (Math.abs(this.sensitivity - newSens) > 0.005) {
      this.sensitivity = newSens;
      if (this.fs > 0.0) {
        this.setupFilters(this.fs);
      }
    }
  }

  setBassMode(bass: boolean): void {
    if (this.isBassMode !== bass) {
      this.isBassMode = bass;
      if (this.fs > 0.0) {
        this.setupFilters(this.fs);
      }
    }
  }

  process(samples: Float32Array): void {
    const numSamples = samples.length;
    if (numSamples === 0) return;

    let sumSquares = 0.0;
    for (let i = 0; i < numSamples; ++i) {
      sumSquares += samples[i] * samples[i];
    }

    const blockRms = Math.sqrt(sumSquares / numSamples);

    const alphaRms = 0.85;
    this.smoothRms = this.smoothRms * alphaRms + blockRms * (1.0 - alphaRms);

    const noiseFloor = 0.00045 + (1.0 - this.sensitivity) * 0.00015;
    const isAttack = blockRms > this.smoothRms * 1.35 && blockRms > 0.001;

    for (let i = 0; i < numSamples; ++i) {
      const filtered = this.processFilter(samples[i]);

      this.fullBuffer[this.fullWritePos] = filtered;
      this.fullWritePos = (this.fullWritePos + 1) & (this.fullBufferSize - 1);

      if (++this.decimationCounter >= this.decimationFactor) {
        this.decimationCounter = 0;
        this.inputBuffer[this.writePos++] = filtered;

        if (this.writePos >= this.bufferSize) {
          this.writePos = this.bufferSize - this.hopSize;

          if (blockRms >= noiseFloor) {
            this.silenceSamples = 0;
            this.runDetection(isAttack, blockRms);
          } else {
            this.silenceSamples += this.hopSize;
            if (this.silenceSamples > Math.floor(this.decimatedFs * 0.22)) {
              this.hasSignalFlag = false;
              this.lockedFrequency = 0.0;
              this.candidateFrequency = 0.0;
              this.candidateHits = 0;
            }
          }

          this.inputBuffer.copyWithin(0, this.hopSize, this.bufferSize);
        }
      }
    }
  }

  getFrequency(): number {
    return this.detectedFrequency;
  }

  hasSignal(): boolean {
    return this.hasSignalFlag;
  }

  private setupFilters(sampleRate: number): void {
    const hpFreq = this.isBassMode ? 14.0 : 35.0;
    this.hp_coeff = Math.exp((-2.0 * Math.PI * hpFreq) / sampleRate);

    const slowDown = (1.0 - this.sensitivity) / 0.9;
    const guitarCutoff = 1150.0 - slowDown * 630.0;
    const bassCutoff = 480.0 - slowDown * 130.0;
    const cutoff = this.isBassMode ? bassCutoff : guitarCutoff;

    const omega = (2.0 * Math.PI * cutoff) / sampleRate;
    const sn = Math.sin(omega);
    const cs = Math.cos(omega);
    const alpha = sn / (2.0 * 0.7071);

    const a0 = 1.0 + alpha;
    this.lp_b0 = ((1.0 - cs) * 0.5) / a0;
    this.lp_b1 = (1.0 - cs) / a0;
    this.lp_b2 = ((1.0 - cs) * 0.5) / a0;
    this.lp_a1 = (-2.0 * cs) / a0;
    this.lp_a2 = (1.0 - alpha) / a0;
  }

  private processFilter(input: number): number {
    const hpOut = input - this.hp_x1 + this.hp_coeff * this.hp_y1;
    this.hp_x1 = input;
    this.hp_y1 = hpOut;

    const lpOut =
      this.lp_b0 * hpOut +
      this.lp_b1 * this.lp_x1 +
      this.lp_b2 * this.lp_x2 -
      this.lp_a1 * this.lp_y1 -
      this.lp_a2 * this.lp_y2;
    this.lp_x2 = this.lp_x1;
    this.lp_x1 = hpOut;
    this.lp_y2 = this.lp_y1;
    this.lp_y1 = lpOut;

    return lpOut;
  }

  private computeFineNsdfAt(tauFull: number, fineWindow: number): number {
    const mask = this.fullBufferSize - 1;
    const startPos = (this.fullWritePos - fineWindow * 2 + this.fullBufferSize) & mask;

    let acf = 0.0;
    let m0 = 0.0;

    for (let i = 0; i < fineWindow; ++i) {
      const x1 = this.fullBuffer[(startPos + i) & mask];
      const x2 = this.fullBuffer[(startPos + i + tauFull) & mask];
      acf += x1 * x2;
      m0 += x1 * x1 + x2 * x2;
    }

    return m0 > 1e-9 ? (2.0 * acf) / m0 : 0.0;
  }

  private runDetection(isAttack: boolean, currentRms: number): void {
    const W = this.bufferSize >> 1; // 512
    const inp = this.inputBuffer;
    const sq = this.sqBuffer;

    for (let i = 0; i < this.bufferSize; ++i) {
      sq[i] = inp[i] * inp[i];
    }

    let sumX1 = 0.0;
    for (let i = 0; i < W; ++i) {
      sumX1 += sq[i];
    }

    let sumX2 = sumX1;
    this.nsdf[0] = 1.0;

    const minTauSearch = this.isBassMode ? 12 : 5;

    for (let tau = 1; tau < W; ++tau) {
      sumX2 += sq[W + tau - 1] - sq[tau - 1];

      if (tau < minTauSearch) {
        this.nsdf[tau] = 0.0;
        continue;
      }

      let acf = 0.0;
      for (let i = 0; i < W; ++i) {
        acf += inp[i] * inp[i + tau];
      }

      const m0 = sumX1 + sumX2;
      this.nsdf[tau] = m0 > 1e-9 ? (2.0 * acf) / m0 : 0.0;
    }

    const peaks: { tau: number; value: number }[] = [];
    for (let tau = minTauSearch + 1; tau < W - 1; ++tau) {
      const val = this.nsdf[tau];
      if (val > 0.0 && val > this.nsdf[tau - 1] && val >= this.nsdf[tau + 1]) {
        if (peaks.length < 64) {
          peaks.push({ tau, value: val });
        }
      }
    }

    if (peaks.length === 0) return;

    let highestVal = 0.0;
    for (let i = 0; i < peaks.length; ++i) {
      if (peaks[i].value > highestVal) {
        highestVal = peaks[i].value;
      }
    }

    const minClarity = currentRms < 0.002 ? 0.6 : 0.45;
    if (highestVal < minClarity) return;

    const cutoff = Math.max(minClarity, highestVal * 0.85);

    let selectedTau = -1;
    for (let i = 0; i < peaks.length; ++i) {
      if (peaks[i].value >= cutoff) {
        selectedTau = peaks[i].tau;
        break;
      }
    }

    if (selectedTau <= 0 || selectedTau >= W - 1) return;

    const slowDown = (1.0 - this.sensitivity) / 0.9;
    const centerFullTau = selectedTau * this.decimationFactor;
    const minFineW = 256 + Math.floor(slowDown * 512.0);
    const maxFineW = 1024 + Math.floor(slowDown * 512.0);
    const fineW = Math.max(
      minFineW,
      Math.min(maxFineW, centerFullTau * (2 + Math.floor(slowDown * 2.0)))
    );

    let bestFullTau = centerFullTau;
    let bestFullVal = -1.0;
    const searchRadius = this.decimationFactor;
    const localNsdf = new Float32Array(2 * searchRadius + 1);

    for (let offset = -searchRadius; offset <= searchRadius; ++offset) {
      const t = centerFullTau + offset;
      const v = t > 2 ? this.computeFineNsdfAt(t, fineW) : 0.0;
      localNsdf[offset + searchRadius] = v;
      if (v > bestFullVal) {
        bestFullVal = v;
        bestFullTau = t;
      }
    }

    const bestIdx = bestFullTau - centerFullTau + searchRadius;
    const alpha =
      bestIdx > 0 ? localNsdf[bestIdx - 1] : this.computeFineNsdfAt(bestFullTau - 1, fineW);
    const beta = bestFullVal;
    const gamma =
      bestIdx < 2 * searchRadius
        ? localNsdf[bestIdx + 1]
        : this.computeFineNsdfAt(bestFullTau + 1, fineW);

    const denom = 2.0 * (alpha - 2.0 * beta + gamma);
    let delta = Math.abs(denom) > 1e-7 ? (alpha - gamma) / denom : 0.0;
    delta = Math.max(-1.0, Math.min(1.0, delta));

    const exactFullPeriod = bestFullTau + delta;
    if (exactFullPeriod <= 1.0) return;

    const rawFreq = this.fs / exactFullPeriod;

    const minAllowedFreq = this.isBassMode ? 20.0 : 36.0;
    const maxAllowedFreq = this.isBassMode ? 800.0 : 2000.0;
    if (rawFreq < minAllowedFreq || rawFreq > maxAllowedFreq) return;

    if (this.lockedFrequency <= 0.0 || (isAttack && this.sensitivity > 0.7)) {
      this.lockedFrequency = rawFreq;
      this.detectedFrequency = rawFreq;
      this.candidateFrequency = 0.0;
      this.candidateHits = 0;
      this.hasSignalFlag = true;
      return;
    }

    const ratio = rawFreq / this.lockedFrequency;

    if (ratio >= 0.94 && ratio <= 1.06) {
      const centsDiff = Math.abs(1200.0 * Math.log2(ratio));
      const baseWeight = centsDiff < 0.25 ? 0.1 : centsDiff < 1.5 ? 0.25 : 0.45;
      const minWeight = centsDiff < 0.25 ? 0.008 : centsDiff < 1.5 ? 0.018 : 0.035;

      const curve = this.sensitivity * this.sensitivity;
      const newWeight = minWeight + curve * (baseWeight - minWeight);

      this.lockedFrequency = this.lockedFrequency * (1.0 - newWeight) + rawFreq * newWeight;
      this.detectedFrequency = this.lockedFrequency;
      this.candidateFrequency = 0.0;
      this.candidateHits = 0;
      this.hasSignalFlag = true;
      return;
    }

    const requiredFastHits = 2 + Math.round(slowDown * 4.0);
    const requiredSlowHits = 3 + Math.round(slowDown * 5.0);

    if (highestVal >= 0.75 || currentRms > 0.004) {
      this.candidateHits++;
      if (this.candidateHits >= requiredFastHits) {
        this.lockedFrequency = rawFreq;
        this.detectedFrequency = rawFreq;
        this.candidateFrequency = 0.0;
        this.candidateHits = 0;
        this.hasSignalFlag = true;
        return;
      }
    } else {
      const candRatio = this.candidateFrequency > 0.0 ? rawFreq / this.candidateFrequency : 0.0;
      if (candRatio >= 0.96 && candRatio <= 1.04) {
        this.candidateHits++;
        if (this.candidateHits >= requiredSlowHits) {
          this.lockedFrequency = rawFreq;
          this.detectedFrequency = rawFreq;
          this.candidateFrequency = 0.0;
          this.candidateHits = 0;
          this.hasSignalFlag = true;
          return;
        }
      } else {
        this.candidateFrequency = rawFreq;
        this.candidateHits = 1;
      }
    }

    this.detectedFrequency = this.lockedFrequency;
    this.hasSignalFlag = true;
  }
}
