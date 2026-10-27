import React, { useEffect, useRef, useState } from 'react';
import { TuningPresets } from '../dsp/TuningPresets';

// ---------------------------------------------------------------------------
// Star SVG helper matching PresetSelectorComponent::drawStarIcon
// ---------------------------------------------------------------------------
export const StarIcon: React.FC<{
  filled: boolean;
  hovered?: boolean;
  size?: number;
}> = ({ filled, hovered = false, size = 12 }) => {
  const cx = size / 2;
  const cy = size / 2;
  const outerR = size * 0.44;
  const innerR = outerR * 0.44;

  const pts: string[] = [];
  for (let i = 0; i < 10; ++i) {
    const r = i % 2 === 0 ? outerR : innerR;
    const a = i * Math.PI * 0.2 - Math.PI * 0.5;
    pts.push(`${cx + Math.cos(a) * r},${cy + Math.sin(a) * r}`);
  }

  return (
    <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`} className="shrink-0">
      <polygon
        points={pts.join(' ')}
        fill={
          filled
            ? hovered
              ? 'rgb(255, 225, 70)'
              : 'rgb(245, 195, 35)'
            : 'none'
        }
        stroke={
          filled
            ? 'rgb(185, 135, 10)'
            : hovered
              ? 'rgb(255, 255, 255)'
              : 'rgb(130, 138, 152)'
        }
        strokeWidth={filled ? 0.8 : 1.2}
      />
    </svg>
  );
};

// ---------------------------------------------------------------------------
// 1. PresetSelectorComponent (Source/ui/PresetSelectorComponent.h)
// ---------------------------------------------------------------------------
export interface PresetSelectorProps {
  isBass: boolean;
  presetIdx: number;
  numStrings: number;
  capoSemitones: number;
  favoritesMask: number;
  onSelectPreset: (idx: number) => void;
  onStarClicked: () => void;
}

export const PresetSelector: React.FC<PresetSelectorProps> = ({
  isBass,
  presetIdx,
  numStrings,
  capoSemitones,
  favoritesMask,
  onSelectPreset,
  onStarClicked,
}) => {
  const [boxHovered, setBoxHovered] = useState(false);
  const [starHovered, setStarHovered] = useState(false);
  const [menuOpen, setMenuOpen] = useState(false);
  const containerRef = useRef<HTMLDivElement | null>(null);

  const presets = TuningPresets.getPresets(isBass);
  const safeIdx = Math.max(0, Math.min(presets.length - 1, presetIdx));
  const preset = presets[safeIdx];
  const { midi: activeMidi, count } = TuningPresets.getActiveStringNotes(
    preset,
    isBass,
    numStrings
  );

  useEffect(() => {
    if (!menuOpen) return;
    const handleClickOutside = (e: MouseEvent) => {
      if (containerRef.current && !containerRef.current.contains(e.target as Node)) {
        setMenuOpen(false);
      }
    };
    window.addEventListener('mousedown', handleClickOutside);
    return () => window.removeEventListener('mousedown', handleClickOutside);
  }, [menuOpen]);

  return (
    <div ref={containerRef} className="relative w-full h-full select-none">
      <div
        onMouseEnter={() => setBoxHovered(true)}
        onMouseLeave={() => {
          setBoxHovered(false);
          setStarHovered(false);
        }}
        onClick={() => setMenuOpen((o) => !o)}
        style={{
          backgroundColor: boxHovered ? 'rgb(248, 250, 253)' : 'rgb(242, 244, 248)',
          borderColor: 'rgb(150, 168, 195)',
        }}
        className="w-full h-full rounded-[4.5px] border px-[4px] py-[2px] flex flex-col justify-between cursor-pointer"
      >
        {/* Row 1: Preset Name + Star */}
        <div className="flex items-center justify-between h-[15px] pl-[2px]">
          <span
            style={{ color: 'rgb(25, 28, 34)' }}
            className="text-[10.5px] font-bold truncate leading-none pr-1"
          >
            {preset.name}
          </span>
          <div
            onMouseEnter={(e) => {
              e.stopPropagation();
              setStarHovered(true);
            }}
            onMouseLeave={() => setStarHovered(false)}
            onClick={(e) => {
              e.stopPropagation();
              setMenuOpen(false);
              onStarClicked();
            }}
            className="w-[18px] h-[15px] flex items-center justify-center -mr-[1px]"
            title="Browse All Presets"
          >
            <StarIcon filled={true} hovered={starHovered} size={11} />
          </div>
        </div>

        {/* Row 2: String Note Badges */}
        <div
          className="flex items-center w-full"
          style={{ gap: count >= 8 ? '1.5px' : '2.2px' }}
        >
          {Array.from({ length: count }).map((_, i) => {
            const noteStr = TuningPresets.midiToNoteName(activeMidi[i] + capoSemitones);
            return (
              <div
                key={i}
                style={{
                  backgroundColor: 'rgb(62, 92, 138)',
                  borderColor: 'rgb(42, 66, 104)',
                }}
                className="flex-1 h-[14.5px] rounded-[2.8px] border-[0.7px] flex items-center justify-center text-white font-bold text-[10.4px] leading-none"
              >
                {noteStr}
              </div>
            );
          })}
        </div>
      </div>

      {/* Favorites Dropdown Popup */}
      {menuOpen && (
        <div
          style={{
            backgroundColor: 'rgb(25, 29, 39)',
            borderColor: 'rgb(65, 75, 95)',
          }}
          className="absolute right-0 top-[39px] z-50 w-[235px] rounded-[5px] border shadow-xl py-1 text-[11px]"
        >
          <div className="max-h-[180px] overflow-y-auto">
            {presets.map((p, idx) => {
              const isFav = (favoritesMask & (1 << idx)) !== 0;
              if (!isFav) return null;
              const notesInfo = TuningPresets.getActiveStringNotes(p, isBass, numStrings);
              const preview = notesInfo.midi
                .slice(0, notesInfo.count)
                .map((m) => TuningPresets.midiToNoteName(m + capoSemitones))
                .join('-');
              const active = idx === safeIdx;
              return (
                <button
                  key={p.id}
                  onClick={() => {
                    onSelectPreset(idx);
                    setMenuOpen(false);
                  }}
                  className={`w-full text-left px-2.5 py-1 flex items-center justify-between gap-2 transition-colors ${
                    active
                      ? 'bg-[#2a3e5c] text-white font-bold'
                      : 'text-[#d2d7e1] hover:bg-[#202632]'
                  }`}
                >
                  <span className="truncate">{p.name}</span>
                  <span className="text-[10px] text-[#8cb4ff] font-mono shrink-0">
                    [{preview}]
                  </span>
                </button>
              );
            })}
          </div>
          <div className="my-1 border-t border-[#323744]" />
          <button
            onClick={() => {
              setMenuOpen(false);
              onStarClicked();
            }}
            className="w-full text-left px-2.5 py-1 text-[#64d7ff] hover:bg-[#202632] font-semibold flex items-center gap-1.5"
          >
            <span>Browse All Presets (★)...</span>
          </button>
        </div>
      )}
    </div>
  );
};

// ---------------------------------------------------------------------------
// 2. RefPitchComponent (Source/ui/RefPitchComponent.h)
// ---------------------------------------------------------------------------
export const RefPitchBox: React.FC<{
  value: number;
  onChange: (val: number) => void;
}> = ({ value, onChange }) => {
  const [editing, setEditing] = useState(false);
  const [draft, setDraft] = useState(value.toFixed(1));
  const dragRef = useRef<{ active: boolean; startY: number; startVal: number }>({
    active: false,
    startY: 0,
    startVal: value,
  });

  const clampPitch = (v: number) => Math.max(400.0, Math.min(480.0, Math.round(v * 10) / 10));

  useEffect(() => {
    setDraft(value.toFixed(1));
  }, [value]);

  useEffect(() => {
    const onMove = (e: MouseEvent) => {
      if (!dragRef.current.active) return;
      const diffY = dragRef.current.startY - e.clientY;
      const steps = Math.round(diffY / 6.0);
      onChange(clampPitch(dragRef.current.startVal + steps * 0.1));
    };
    const onUp = () => {
      dragRef.current.active = false;
    };
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
    return () => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup', onUp);
    };
  });

  const commitEdit = () => {
    setEditing(false);
    const parsed = parseFloat(draft);
    if (!isNaN(parsed) && parsed >= 400.0 && parsed <= 480.0) {
      onChange(clampPitch(parsed));
    } else {
      setDraft(value.toFixed(1));
    }
  };

  return (
    <div
      style={{
        backgroundColor: 'rgb(23, 26, 37)',
        borderColor: 'rgb(50, 54, 64)',
      }}
      className="w-full h-full rounded-[4px] border flex items-center justify-between px-[7px] select-none"
      onWheel={(e) => {
        e.preventDefault();
        if (e.deltaY < 0) onChange(clampPitch(value + 0.1));
        else if (e.deltaY > 0) onChange(clampPitch(value - 0.1));
      }}
    >
      <span
        style={{ color: 'rgba(170, 170, 170, 0.85)' }}
        className="text-[11.5px] font-bold cursor-pointer"
        onDoubleClick={() => onChange(440.0)}
        title="Double-click to reset to 440.0 Hz"
      >
        REF. Hz
      </span>

      {editing ? (
        <input
          type="text"
          value={draft}
          autoFocus
          onChange={(e) => setDraft(e.target.value)}
          onBlur={commitEdit}
          onKeyDown={(e) => {
            if (e.key === 'Enter') commitEdit();
            if (e.key === 'Escape') {
              setEditing(false);
              setDraft(value.toFixed(1));
            }
          }}
          className="w-[38px] bg-transparent text-white text-[12px] outline-none text-left font-medium"
        />
      ) : (
        <span
          onClick={() => setEditing(true)}
          onMouseDown={(e) => {
            dragRef.current = { active: true, startY: e.clientY, startVal: value };
          }}
          className="text-white text-[12px] cursor-ns-resize pl-1 flex-1 text-left"
        >
          {value.toFixed(1)}
        </span>
      )}

      <div className="flex flex-col items-center justify-center w-[14px] h-full -mr-[3px]">
        <button
          onClick={() => onChange(clampPitch(value + 0.1))}
          className="h-1/2 flex items-center justify-center text-[#a0a5af] hover:text-white"
        >
          <svg width="9" height="6" viewBox="0 0 9 6">
            <path
              d="M1 5 L4.5 1 L8 5"
              fill="none"
              stroke="currentColor"
              strokeWidth="1.5"
              strokeLinecap="round"
            />
          </svg>
        </button>
        <button
          onClick={() => onChange(clampPitch(value - 0.1))}
          className="h-1/2 flex items-center justify-center text-[#a0a5af] hover:text-white"
        >
          <svg width="9" height="6" viewBox="0 0 9 6">
            <path
              d="M1 1 L4.5 5 L8 1"
              fill="none"
              stroke="currentColor"
              strokeWidth="1.5"
              strokeLinecap="round"
            />
          </svg>
        </button>
      </div>
    </div>
  );
};

// ---------------------------------------------------------------------------
// 3. SettingsOverlayComponent (Source/ui/SettingsOverlayComponent.h)
// ---------------------------------------------------------------------------
interface SpinRowProps {
  title: string;
  value: number;
  defaultVal: number;
  minVal: number;
  maxVal: number;
  stepVal: number;
  decimals: number;
  onChange: (val: number) => void;
}

const ValueSpinRow: React.FC<SpinRowProps> = ({
  title,
  value,
  defaultVal,
  minVal,
  maxVal,
  stepVal,
  decimals,
  onChange,
}) => {
  const [editing, setEditing] = useState(false);
  const [draft, setDraft] = useState(value.toFixed(decimals));

  useEffect(() => {
    setDraft(value.toFixed(decimals));
  }, [value, decimals]);

  const clampAndRound = (v: number) => {
    const rounded = Math.round(v / stepVal) * stepVal;
    return Math.max(minVal, Math.min(maxVal, Number(rounded.toFixed(decimals))));
  };

  const isModified = Math.abs(value - defaultVal) > stepVal * 0.1;

  const commit = () => {
    setEditing(false);
    const parsed = parseFloat(draft);
    if (!isNaN(parsed) && parsed >= minVal && parsed <= maxVal) {
      onChange(clampAndRound(parsed));
    } else {
      setDraft(value.toFixed(decimals));
    }
  };

  return (
    <div
      className="flex items-center justify-between h-[24px] select-none"
      onWheel={(e) => {
        e.preventDefault();
        if (e.deltaY < 0) onChange(clampAndRound(value + stepVal));
        else if (e.deltaY > 0) onChange(clampAndRound(value - stepVal));
      }}
    >
      <span
        onDoubleClick={() => onChange(defaultVal)}
        style={{ color: 'rgb(210, 215, 225)' }}
        className="text-[12px] font-bold cursor-pointer"
        title="Double-click to reset to default"
      >
        {title}
      </span>

      <div className="flex items-center gap-1.5">
        {editing ? (
          <input
            type="text"
            value={draft}
            autoFocus
            onChange={(e) => setDraft(e.target.value)}
            onBlur={commit}
            onKeyDown={(e) => {
              if (e.key === 'Enter') commit();
              if (e.key === 'Escape') {
                setEditing(false);
                setDraft(value.toFixed(decimals));
              }
            }}
            className="w-[46px] bg-[#252a36] text-white text-[12.5px] font-bold text-right px-1 rounded outline-none"
          />
        ) : (
          <span
            onClick={() => setEditing(true)}
            className="text-white text-[12.5px] font-bold text-right w-[46px] cursor-pointer"
          >
            {value.toFixed(decimals)}
          </span>
        )}

        <div className="flex flex-col items-center justify-center w-[15px]">
          <button
            onClick={() => onChange(clampAndRound(value + stepVal))}
            className="text-[#a0a5af] hover:text-white leading-none py-[1px]"
          >
            <svg width="9" height="6" viewBox="0 0 9 6">
              <path
                d="M1 5 L4.5 1 L8 5"
                fill="none"
                stroke="currentColor"
                strokeWidth="1.5"
                strokeLinecap="round"
              />
            </svg>
          </button>
          <button
            onClick={() => onChange(clampAndRound(value - stepVal))}
            className="text-[#a0a5af] hover:text-white leading-none py-[1px]"
          >
            <svg width="9" height="6" viewBox="0 0 9 6">
              <path
                d="M1 1 L4.5 5 L8 1"
                fill="none"
                stroke="currentColor"
                strokeWidth="1.5"
                strokeLinecap="round"
              />
            </svg>
          </button>
        </div>

        <div className="w-[15px] flex items-center justify-center">
          {isModified && (
            <button
              onClick={() => onChange(defaultVal)}
              className="text-[#a0a5af] hover:text-white"
              title="Reset to default"
            >
              <svg width="11" height="11" viewBox="0 0 12 12">
                <path
                  d="M2.5 6 A3.8 3.8 0 1 1 6 9.8"
                  fill="none"
                  stroke="currentColor"
                  strokeWidth="1.4"
                  strokeLinecap="round"
                />
                <path
                  d="M4.5 1.5 L2.2 3.8 L5.2 4.4"
                  fill="none"
                  stroke="currentColor"
                  strokeWidth="1.4"
                  strokeLinecap="round"
                  strokeLinejoin="round"
                />
              </svg>
            </button>
          )}
        </div>
      </div>
    </div>
  );
};

const PedalToggleRow: React.FC<{
  title: string;
  checked: boolean;
  onChange: (checked: boolean) => void;
}> = ({ title, checked, onChange }) => {
  return (
    <div
      onClick={() => onChange(!checked)}
      className="flex items-center justify-between h-[24px] pr-[18px] cursor-pointer group select-none"
    >
      <span style={{ color: 'rgb(210, 215, 225)' }} className="text-[12px] font-bold">
        {title}
      </span>
      <div
        style={{ backgroundColor: 'rgb(18, 20, 26)' }}
        className="w-[18px] h-[18px] rounded-[3.5px] border border-[#3c414e] group-hover:border-[#a0a5af] flex items-center justify-center"
      >
        {checked && (
          <svg width="13" height="13" viewBox="0 0 14 14">
            <path
              d="M2.5 7 L5.8 10.5 L11.5 3.5"
              fill="none"
              stroke="rgb(100, 255, 65)"
              strokeWidth="2.0"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
          </svg>
        )}
      </div>
    </div>
  );
};

export interface SettingsOverlayProps {
  fastStrobe: number;
  slowStrobe: number;
  inTune: number;
  polytune: number;
  slowAbove: boolean;
  showOctave: boolean;
  onChangeFastStrobe: (v: number) => void;
  onChangeSlowStrobe: (v: number) => void;
  onChangeInTune: (v: number) => void;
  onChangePolytune: (v: number) => void;
  onChangeSlowAbove: (v: boolean) => void;
  onChangeShowOctave: (v: boolean) => void;
  onClose: () => void;
}

export const SettingsOverlay: React.FC<SettingsOverlayProps> = ({
  fastStrobe,
  slowStrobe,
  inTune,
  polytune,
  slowAbove,
  showOctave,
  onChangeFastStrobe,
  onChangeSlowStrobe,
  onChangeInTune,
  onChangePolytune,
  onChangeSlowAbove,
  onChangeShowOctave,
  onClose,
}) => {
  return (
    <div className="w-full h-full px-[16px] pt-[8px] flex flex-col select-none">
      {/* Header */}
      <div className="flex items-center h-[24px] mb-[4px]">
        <button
          onClick={onClose}
          className="text-[12px] font-bold text-[#aab0ba] hover:text-white w-[48px] text-left cursor-pointer"
        >
          &lt; Back
        </button>
        <span className="text-[14px] font-bold text-white ml-[2px]">SETTINGS</span>
      </div>

      <div className="border-t border-[#323744] mb-[5px]" />

      <div
        style={{ color: 'rgb(100, 215, 255)' }}
        className="text-[11px] font-bold mb-[4px] pl-[2px]"
      >
        TUNING ACCURACY
      </div>

      <div className="flex flex-col gap-[3px] pl-[2px]">
        <ValueSpinRow
          title="Fast strobe, cent"
          value={fastStrobe}
          defaultVal={0.1}
          minVal={0.05}
          maxVal={5.0}
          stepVal={0.01}
          decimals={2}
          onChange={onChangeFastStrobe}
        />
        <ValueSpinRow
          title="Slow strobe, cent"
          value={slowStrobe}
          defaultVal={1.0}
          minVal={1.0}
          maxVal={10.0}
          stepVal={0.1}
          decimals={1}
          onChange={onChangeSlowStrobe}
        />
        <ValueSpinRow
          title="In tune, cent"
          value={inTune}
          defaultVal={2.0}
          minVal={1.0}
          maxVal={10.0}
          stepVal={0.1}
          decimals={1}
          onChange={onChangeInTune}
        />
        <ValueSpinRow
          title="Polytune, cent"
          value={polytune}
          defaultVal={2.0}
          minVal={1.0}
          maxVal={10.0}
          stepVal={0.1}
          decimals={1}
          onChange={onChangePolytune}
        />
        <div className="mt-[2px] flex flex-col gap-[3px]">
          <PedalToggleRow
            title="Slow strobe above"
            checked={slowAbove}
            onChange={onChangeSlowAbove}
          />
          <PedalToggleRow
            title="Show octave number"
            checked={showOctave}
            onChange={onChangeShowOctave}
          />
        </div>
      </div>
    </div>
  );
};

// ---------------------------------------------------------------------------
// 4. PresetBrowserOverlay (Source/ui/PresetBrowserOverlay.h)
// ---------------------------------------------------------------------------
export interface PresetBrowserOverlayProps {
  isBass: boolean;
  selectedPresetIdx: number;
  numStrings: number;
  capoSemitones: number;
  favoritesMask: number;
  onSelectPreset: (idx: number) => void;
  onFavoritesChanged: (mask: number) => void;
  onClose: () => void;
}

export const PresetBrowserOverlay: React.FC<PresetBrowserOverlayProps> = ({
  isBass,
  selectedPresetIdx,
  numStrings,
  capoSemitones,
  favoritesMask,
  onSelectPreset,
  onFavoritesChanged,
  onClose,
}) => {
  const presets = TuningPresets.getPresets(isBass);

  // Group presets with category headers in order
  const rows: Array<
    | { isHeader: true; categoryTitle: string; key: string }
    | { isHeader: false; presetIdx: number; key: string }
  > = [];

  let lastCat = '';
  presets.forEach((p, idx) => {
    if (p.category !== lastCat) {
      lastCat = p.category;
      rows.push({ isHeader: true, categoryTitle: lastCat, key: `cat-${lastCat}` });
    }
    rows.push({ isHeader: false, presetIdx: idx, key: `preset-${p.id}` });
  });

  const toggleStar = (idx: number) => {
    const bit = 1 << idx;
    // Prevent removing the very last favorite
    if ((favoritesMask & bit) !== 0 && (favoritesMask & ~bit) === 0) {
      return;
    }
    onFavoritesChanged(favoritesMask ^ bit);
  };

  return (
    <div className="w-full h-full flex flex-col pt-[4px] select-none">
      {/* Header */}
      <div className="flex items-center h-[24px] px-[8px] mb-[4px]">
        <button
          onClick={onClose}
          className="text-[12px] font-bold text-[#aab0ba] hover:text-white w-[50px] text-left cursor-pointer"
        >
          &lt; Back
        </button>
        <span className="text-[13.5px] font-bold text-white">TUNING PRESETS</span>
      </div>

      <div className="mx-[12px] border-t border-[#323744] mb-[2px]" />

      {/* Scrollable Preset List */}
      <div className="flex-1 overflow-y-auto px-[4px] pb-[6px] space-y-[1px]">
        {rows.map((item) => {
          if (item.isHeader) {
            return (
              <div
                key={item.key}
                style={{ color: 'rgb(100, 215, 255)' }}
                className="text-[10.5px] font-bold px-[4px] pt-[5px] pb-[1px]"
              >
                {item.categoryTitle}
              </div>
            );
          }

          const p = presets[item.presetIdx];
          const isSelected = item.presetIdx === selectedPresetIdx;
          const isFav = (favoritesMask & (1 << item.presetIdx)) !== 0;
          const { midi: activeMidi, count } = TuningPresets.getActiveStringNotes(
            p,
            isBass,
            numStrings
          );

          return (
            <div
              key={item.key}
              onClick={() => onSelectPreset(item.presetIdx)}
              style={{
                backgroundColor: isSelected ? 'rgb(42, 62, 92)' : undefined,
              }}
              className={`h-[20px] rounded-[4px] flex items-center justify-between px-[4px] cursor-pointer ${
                !isSelected ? 'hover:bg-[#202632]' : ''
              }`}
            >
              <div className="flex items-center gap-[5px] min-w-0 pr-1">
                <button
                  onClick={(e) => {
                    e.stopPropagation();
                    toggleStar(item.presetIdx);
                  }}
                  className="p-[2px] flex items-center justify-center"
                >
                  <StarIcon filled={isFav} size={11} />
                </button>
                <span
                  className={`text-[11px] truncate ${
                    isSelected ? 'text-white font-bold' : 'text-[#d2d7e1]'
                  }`}
                >
                  {p.name}
                </span>
              </div>

              <div className="flex items-center gap-[1.6px] shrink-0">
                {Array.from({ length: count }).map((_, s) => {
                  const nStr = TuningPresets.midiToNoteName(activeMidi[s] + capoSemitones);
                  return (
                    <div
                      key={s}
                      style={{ backgroundColor: 'rgb(55, 82, 125)' }}
                      className="w-[15.5px] h-[14.5px] rounded-[2.2px] flex items-center justify-center text-white text-[10px] font-bold leading-none"
                    >
                      {nStr}
                    </div>
                  );
                })}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};
