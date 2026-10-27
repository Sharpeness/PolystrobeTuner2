export interface TunerResult {
  note: string;
  isSharp: boolean;
  midiNote: number;
  cents: number;
  isValid: boolean;
}

const NOTE_NAMES = [
  'C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'
] as const;

export class TunerMath {
  static frequencyToNote(
    freq: number,
    a4Ref: number = 440.0,
    sweetenerCentsOffset: number = 0.0,
    capoSemitones: number = 0
  ): TunerResult {
    const res: TunerResult = {
      note: ' ',
      isSharp: false,
      midiNote: 69,
      cents: 0.0,
      isValid: false,
    };

    if (freq < 20.0 || freq > 4500.0) {
      return res;
    }

    // Real acoustic MIDI number (A4 = 440 Hz = 69)
    const semitone = 12.0 * (Math.log(freq / a4Ref) / Math.log(2.0)) + 69.0;
    const nearestMidi = Math.round(semitone);

    // Deviation strictly from real note frequency minus sweetener offset
    res.cents = (semitone - nearestMidi) * 100.0 - sweetenerCentsOffset;

    // Visual note shift on screen: with Capo = -2, note D (50) is displayed as E (50 - (-2) = 52)
    res.midiNote = nearestMidi - capoSemitones;
    res.isValid = true;

    const noteIndex = ((res.midiNote % 12) + 12) % 12;
    const name = NOTE_NAMES[noteIndex];

    res.note = name[0];
    res.isSharp = name.length > 1 && name[1] === '#';

    return res;
  }
}
