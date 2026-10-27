export interface TuningPreset {
  id: number;
  name: string;
  category: string;
  // For guitar: 8 elements: [0]=8th .. [7]=1st string
  // For bass: first 6 elements: [0]=5th(B0), [1]=4th(E1), [2]=3rd(A1), [3]=2nd(D2), [4]=1st(G2), [5]=high(C3)
  midiNotes: number[];
  centOffsets: number[];
  defaultFavorite: boolean;
}

const NOTE_NAMES = [
  'C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'
] as const;

export class TuningPresets {
  private static guitarPresets: TuningPreset[] = TuningPresets.buildGuitarPresets();
  private static bassPresets: TuningPreset[] = TuningPresets.buildBassPresets();

  static getPresets(isBass: boolean): TuningPreset[] {
    return isBass ? this.bassPresets : this.guitarPresets;
  }

  static getAllPresets(): TuningPreset[] {
    return this.getPresets(false);
  }

  static midiToNoteName(midiNote: number): string {
    const idx = ((midiNote % 12) + 12) % 12;
    return NOTE_NAMES[idx];
  }

  static getActiveStringNotes(
    p: TuningPreset,
    isBass: boolean,
    numStrings: number
  ): { midi: number[]; offsets: number[]; count: number } {
    const outMidi: number[] = [];
    const outOffsets: number[] = [];
    let outCount = 0;

    if (!isBass) {
      outCount = Math.max(6, Math.min(8, numStrings));
      const startIdx = 8 - outCount;
      for (let i = 0; i < outCount; ++i) {
        outMidi.push(p.midiNotes[startIdx + i]);
        outOffsets.push(p.centOffsets[startIdx + i]);
      }
    } else {
      outCount = Math.max(4, Math.min(6, numStrings));
      const startIdx = outCount === 4 ? 1 : 0;
      for (let i = 0; i < outCount; ++i) {
        outMidi.push(p.midiNotes[startIdx + i]);
        outOffsets.push(p.centOffsets[startIdx + i]);
      }
    }

    return { midi: outMidi, offsets: outOffsets, count: outCount };
  }

  static getSweetenerOffsetForMidi(
    isBass: boolean,
    presetIdx: number,
    numStrings: number,
    detectedMidi: number
  ): number {
    const all = this.getPresets(isBass);
    if (presetIdx < 0 || presetIdx >= all.length) {
      return 0.0;
    }

    const { midi: activeMidi, offsets: activeOffsets, count } = this.getActiveStringNotes(
      all[presetIdx],
      isBass,
      numStrings
    );

    let bestString = -1;
    let minMidiDist = 999;

    for (let i = 0; i < count; ++i) {
      const targetMidi = activeMidi[i];
      if (((targetMidi % 12) + 12) % 12 === ((detectedMidi % 12) + 12) % 12) {
        const dist = Math.abs(detectedMidi - targetMidi);
        if (dist < minMidiDist) {
          minMidiDist = dist;
          bestString = i;
        }
      }
    }

    if (bestString >= 0) {
      return activeOffsets[bestString];
    }

    return 0.0;
  }

  private static buildGuitarPresets(): TuningPreset[] {
    const list: TuningPreset[] = [];
    let idCounter = 0;

    const baseStd = [30, 35, 40, 45, 50, 55, 59, 64];
    const zeroOffsets = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0];

    const gtrSweetener = [-2.3, -2.1, -2.3, 0.0, 0.0, -0.4, -2.1, -2.3];
    const evhSweetener = [0.0, 0.0, 0.0, 0.0, -1.0, -3.0, -9.0, -9.0];
    const jtSweetener = [-14.0, -13.0, -12.0, -10.0, -8.0, -4.0, -6.0, -3.0];
    const bfeSweetener = [-2.0, -2.0, -2.0, -2.0, -2.0, -2.0, +1.0, 0.0];

    const addShifted = (
      name: string,
      category: string,
      baseNotes: number[],
      shiftSemitones: number,
      offsets: number[],
      defaultFavorite: boolean
    ) => {
      list.push({
        id: idCounter++,
        name,
        category,
        midiNotes: baseNotes.map((n) => n + shiftSemitones),
        centOffsets: [...offsets],
        defaultFavorite,
      });
    };

    addShifted('Standard', 'STANDARD & LOWERED', baseStd, 0, zeroOffsets, true);
    addShifted('- 1/2 step down', 'STANDARD & LOWERED', baseStd, -1, zeroOffsets, true);
    addShifted('- 1 step down', 'STANDARD & LOWERED', baseStd, -2, zeroOffsets, true);
    addShifted('- 1 1/2 steps down', 'STANDARD & LOWERED', baseStd, -3, zeroOffsets, false);
    addShifted('- 2 steps down', 'STANDARD & LOWERED', baseStd, -4, zeroOffsets, false);
    addShifted('- 2 1/2 steps down', 'STANDARD & LOWERED', baseStd, -5, zeroOffsets, false);

    const baseDropD = [28, 33, 38, 45, 50, 55, 59, 64];
    addShifted('Drop D', 'DROP TUNINGS', baseDropD, 0, zeroOffsets, true);
    addShifted('Drop C#', 'DROP TUNINGS', baseDropD, -1, zeroOffsets, false);
    addShifted('Drop C', 'DROP TUNINGS', baseDropD, -2, zeroOffsets, true);

    addShifted('GTR Sweetener (E)', 'PETERSON GTR SWEETENER', baseStd, 0, gtrSweetener, true);
    addShifted('GTR Sweetener (D#)', 'PETERSON GTR SWEETENER', baseStd, -1, gtrSweetener, false);
    addShifted('GTR Sweetener (D)', 'PETERSON GTR SWEETENER', baseStd, -2, gtrSweetener, false);
    addShifted('GTR Sweetener (C#)', 'PETERSON GTR SWEETENER', baseStd, -3, gtrSweetener, false);
    addShifted('GTR Sweetener (C)', 'PETERSON GTR SWEETENER', baseStd, -4, gtrSweetener, false);
    addShifted('GTR Sweetener (B)', 'PETERSON GTR SWEETENER', baseStd, -5, gtrSweetener, false);

    addShifted('EVH Tuning (E)', 'EDDIE VAN HALEN (EVH)', baseStd, 0, evhSweetener, true);
    addShifted('EVH Tuning (D#)', 'EDDIE VAN HALEN (EVH)', baseStd, -1, evhSweetener, false);
    addShifted('EVH Tuning (D)', 'EDDIE VAN HALEN (EVH)', baseStd, -2, evhSweetener, false);
    addShifted('EVH Tuning (C#)', 'EDDIE VAN HALEN (EVH)', baseStd, -3, evhSweetener, false);
    addShifted('EVH Tuning (C)', 'EDDIE VAN HALEN (EVH)', baseStd, -4, evhSweetener, false);
    addShifted('EVH Tuning (B)', 'EDDIE VAN HALEN (EVH)', baseStd, -5, evhSweetener, false);

    addShifted('James Taylor / ACU (E)', 'JAMES TAYLOR / ACU', baseStd, 0, jtSweetener, true);
    addShifted('James Taylor / ACU (D#)', 'JAMES TAYLOR / ACU', baseStd, -1, jtSweetener, false);
    addShifted('James Taylor / ACU (D)', 'JAMES TAYLOR / ACU', baseStd, -2, jtSweetener, false);

    addShifted('Buzz Feiten (E)', 'BUZZ FEITEN (BFE)', baseStd, 0, bfeSweetener, false);
    addShifted('Buzz Feiten (D#)', 'BUZZ FEITEN (BFE)', baseStd, -1, bfeSweetener, false);
    addShifted('Buzz Feiten (D)', 'BUZZ FEITEN (BFE)', baseStd, -2, bfeSweetener, false);

    return list;
  }

  private static buildBassPresets(): TuningPreset[] {
    const list: TuningPreset[] = [];
    let idCounter = 0;

    // Base bass tuning: B0(23), E1(28), A1(33), D2(38), G2(43), C3(48)
    const baseBass = [23, 28, 33, 38, 43, 48, 0, 0];
    const baseBassDrop = [21, 26, 33, 38, 43, 48, 0, 0];
    const zeroOffsets = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0];

    const bassSweetener = [-2.0, -1.5, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0];
    const bfeBass = [-2.0, -2.0, -1.0, -1.0, 0.0, 0.0, 0.0, 0.0];

    const addBass = (
      name: string,
      category: string,
      baseNotes: number[],
      shiftSemitones: number,
      offsets: number[],
      defaultFavorite: boolean
    ) => {
      list.push({
        id: idCounter++,
        name,
        category,
        midiNotes: baseNotes.map((n) => n + shiftSemitones),
        centOffsets: [...offsets],
        defaultFavorite,
      });
    };

    addBass('Bass Standard', 'BASS STANDARD & LOWERED', baseBass, 0, zeroOffsets, true);
    addBass('Bass - 1/2 step down', 'BASS STANDARD & LOWERED', baseBass, -1, zeroOffsets, true);
    addBass('Bass - 1 step down', 'BASS STANDARD & LOWERED', baseBass, -2, zeroOffsets, true);
    addBass('Bass - 1 1/2 steps', 'BASS STANDARD & LOWERED', baseBass, -3, zeroOffsets, false);
    addBass('Bass - 2 steps down', 'BASS STANDARD & LOWERED', baseBass, -4, zeroOffsets, false);

    addBass('Bass Drop D / A', 'BASS DROP TUNINGS', baseBassDrop, 0, zeroOffsets, true);
    addBass('Bass Drop C# / G#', 'BASS DROP TUNINGS', baseBassDrop, -1, zeroOffsets, false);
    addBass('Bass Drop C / G', 'BASS DROP TUNINGS', baseBassDrop, -2, zeroOffsets, true);

    addBass('Bass Sweetener (E)', 'PETERSON BASS SWEETENER', baseBass, 0, bassSweetener, true);
    addBass('Bass Sweetener (D#)', 'PETERSON BASS SWEETENER', baseBass, -1, bassSweetener, false);
    addBass('Bass Sweetener (D)', 'PETERSON BASS SWEETENER', baseBass, -2, bassSweetener, false);

    addBass('Bass Buzz Feiten (E)', 'BASS BUZZ FEITEN', baseBass, 0, bfeBass, false);
    addBass('Bass Buzz Feiten (D#)', 'BASS BUZZ FEITEN', baseBass, -1, bfeBass, false);
    addBass('Bass Buzz Feiten (D)', 'BASS BUZZ FEITEN', baseBass, -2, bfeBass, false);

    return list;
  }
}
