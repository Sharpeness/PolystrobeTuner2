import React, { useEffect, useRef, useState } from 'react';

// Helper for linear color interpolation
function lerpColor(
  c1: [number, number, number],
  c2: [number, number, number],
  t: number
): [number, number, number] {
  return [
    Math.round(c1[0] + (c2[0] - c1[0]) * t),
    Math.round(c1[1] + (c2[1] - c1[1]) * t),
    Math.round(c1[2] + (c2[2] - c1[2]) * t),
  ];
}

function rgba(c: [number, number, number], a: number = 1): string {
  return `rgba(${c[0]}, ${c[1]}, ${c[2]}, ${Math.max(0, Math.min(1, a))})`;
}

// ---------------------------------------------------------------------------
// 1. 3D Display Bezel Canvas (exact port of drawDisplay3DBezel in PluginEditor.cpp)
// ---------------------------------------------------------------------------
export const Display3DBezel: React.FC<{ width?: number; height?: number }> = ({
  width = 448,
  height = 260,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = width * dpr;
    canvas.height = height * dpr;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.scale(dpr, dpr);

    // Fill pedal enclosure background
    ctx.fillStyle = 'rgb(236, 238, 241)';
    ctx.fillRect(0, 0, width, height);

    const area = { x: 12, y: 12, w: 270, h: 236 };
    const r = 14.0;

    const strokeRoundRect = (
      x: number,
      y: number,
      w: number,
      h: number,
      rad: number,
      lineWidth: number,
      strokeStyle: string | CanvasGradient
    ) => {
      ctx.beginPath();
      ctx.roundRect(x, y, w, h, rad);
      ctx.lineWidth = lineWidth;
      ctx.strokeStyle = strokeStyle;
      ctx.stroke();
    };

    const fillRoundRect = (
      x: number,
      y: number,
      w: number,
      h: number,
      rad: number,
      fillStyle: string | CanvasGradient
    ) => {
      ctx.beginPath();
      ctx.roundRect(x, y, w, h, rad);
      ctx.fillStyle = fillStyle;
      ctx.fill();
    };

    for (let i = 1; i <= 3; ++i) {
      const expand = i * 1.2;
      const ty = i * 1.1;
      strokeRoundRect(
        area.x - expand,
        area.y - expand + ty,
        area.w + expand * 2,
        area.h + expand * 2,
        r + expand,
        1.6,
        `rgba(0, 0, 0, ${0.07 * (4 - i)})`
      );
    }

    strokeRoundRect(
      area.x - 5,
      area.y - 5,
      area.w + 10,
      area.h + 10,
      r + 5,
      1.2,
      'rgb(130, 136, 148)'
    );

    const frameGrad = ctx.createLinearGradient(
      area.x,
      area.y,
      area.x + area.w,
      area.y + area.h
    );
    frameGrad.addColorStop(0.0, 'rgb(255, 255, 255)');
    frameGrad.addColorStop(0.35, 'rgb(220, 225, 235)');
    frameGrad.addColorStop(0.7, 'rgb(160, 165, 175)');
    frameGrad.addColorStop(1.0, 'rgb(110, 115, 125)');

    strokeRoundRect(
      area.x - 3,
      area.y - 3,
      area.w + 6,
      area.h + 6,
      r + 3,
      4.0,
      frameGrad
    );

    strokeRoundRect(
      area.x - 0.6,
      area.y - 0.6,
      area.w + 1.2,
      area.h + 1.2,
      r + 0.6,
      1.6,
      'rgb(12, 14, 18)'
    );

    fillRoundRect(area.x, area.y, area.w, area.h, r, 'rgb(23, 26, 37)');

    const highlightGrad = ctx.createLinearGradient(
      area.x + area.w * 0.5,
      area.y,
      area.x + area.w * 0.5,
      area.y + 30
    );
    highlightGrad.addColorStop(0, 'rgba(255, 255, 255, 0.07)');
    highlightGrad.addColorStop(1, 'rgba(255, 255, 255, 0.0)');
    fillRoundRect(area.x + 2, area.y + 1.5, area.w - 4, 30, r - 2, highlightGrad);
  }, [width, height]);

  return (
    <canvas
      ref={canvasRef}
      style={{ width, height }}
      className="pointer-events-none absolute inset-0"
    />
  );
};

// ---------------------------------------------------------------------------
// 2. StrobeArcComponent (exact port of Source/ui/StrobeArcComponent.h)
// ---------------------------------------------------------------------------
export interface PolyColumnState {
  cents: number;
  active: boolean;
}

export interface StrobeArcProps {
  cents: number;
  active: boolean;
  fastThresh: number;
  slowThresh: number;
  slowAbove: boolean;
  isPolyMode?: boolean;
  polyColumns?: PolyColumnState[];
  numStrings?: number;
  polyTolerance?: number;
  width?: number;
  height?: number;
}

function getPolyLayout(numStrings: number): { startCol: number; colWidth: number } {
  switch (numStrings) {
    case 4:
      return { startCol: 0, colWidth: 5 };
    case 5:
      return { startCol: 2, colWidth: 3 };
    case 6:
      return { startCol: 0, colWidth: 3 };
    case 7:
      return { startCol: 1, colWidth: 2 };
    case 8:
      return { startCol: 0, colWidth: 2 };
    default:
      return { startCol: 0, colWidth: 3 };
  }
}

function computeRowTargetBrightness(
  cents: number,
  polyTolerance: number,
  prevInTune: boolean
): { inTune: boolean; rows: [number, number, number, number, number] } {
  const outRows: [number, number, number, number, number] = [0, 0, 0, 0, 0];
  const absC = Math.abs(cents);
  const halfTol = Math.max(0.5, polyTolerance * 0.55);
  const exitLimit = halfTol + Math.max(0.35, halfTol * 0.25);

  const inTune = prevInTune ? absC <= exitLimit : absC <= halfTol;
  if (inTune) {
    outRows[2] = 1.0;
    return { inTune, rows: outRows };
  }

  const over = Math.max(0.0, absC - halfTol);
  const midRow = cents >= 0.0 ? 1 : 3;
  const outerRow = cents >= 0.0 ? 0 : 4;
  const halfPi = Math.PI * 0.5;

  const spanGreenToMid = Math.max(4.2, polyTolerance * 1.65);
  const spanMidHold = Math.max(1.5, polyTolerance * 0.5);
  const spanMidToOuter = Math.max(9.0, polyTolerance * 2.8);

  if (over <= spanGreenToMid) {
    const t = Math.max(0.0, Math.min(1.0, over / spanGreenToMid));
    outRows[2] = Math.cos(t * halfPi);
    outRows[midRow] = Math.sin(t * halfPi);
  } else if (over <= spanGreenToMid + spanMidHold) {
    outRows[midRow] = 1.0;
  } else if (over <= spanGreenToMid + spanMidHold + spanMidToOuter) {
    const u = Math.max(
      0.0,
      Math.min(1.0, (over - spanGreenToMid - spanMidHold) / spanMidToOuter)
    );
    outRows[midRow] = Math.cos(u * halfPi);
    outRows[outerRow] = Math.sin(u * halfPi);
  } else {
    outRows[outerRow] = 1.0;
  }

  return { inTune, rows: outRows };
}

export const StrobeArcCanvas: React.FC<StrobeArcProps> = ({
  cents,
  active,
  fastThresh,
  slowThresh,
  slowAbove,
  isPolyMode = false,
  polyColumns = [],
  numStrings = 6,
  polyTolerance = 2.0,
  width = 262,
  height = 115,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const stateRef = useRef({
    topAngle: 0.0,
    bottomAngle: 0.0,
    currentAlpha: 0.0,
    polyColAlpha: [0, 0, 0, 0, 0, 0, 0, 0] as number[],
    polyColCents: [0, 0, 0, 0, 0, 0, 0, 0] as number[],
    polyColInTune: [false, false, false, false, false, false, false, false] as boolean[],
    polyRowBrightness: Array.from({ length: 8 }, () => [0, 0, 0, 0, 0]) as number[][],
    cachedPoints: [] as { x: number; y: number }[],
  });

  // Precompute LED socket positions
  useEffect(() => {
    const numCols = 23;
    const fW = width;
    const fH = height;
    const centerX = fW * 0.5;
    const centerY = fH * 2.8 + 5.0;

    const startAngle = Math.PI * 1.368;
    const endAngle = Math.PI * 1.632;
    const angleStep = (endAngle - startAngle) / (numCols - 1);

    const baseR = fH * 2.8 - 10.0;
    const radii = [baseR, baseR - 10.0, baseR - 20.0, baseR - 30.0, baseR - 40.0];

    const pts: { x: number; y: number }[] = [];
    for (let row = 0; row < 5; ++row) {
      const r = radii[row];
      for (let i = 0; i < numCols; ++i) {
        const a = startAngle + i * angleStep;
        pts.push({
          x: centerX + Math.cos(a) * r,
          y: centerY + Math.sin(a) * r,
        });
      }
    }
    stateRef.current.cachedPoints = pts;
  }, [width, height]);

  useEffect(() => {
    let animId = 0;
    const numCols = 23;
    const dotR = 3.9;

    const render = () => {
      const st = stateRef.current;

      if (isPolyMode) {
        st.currentAlpha *= 0.8;
        if (st.currentAlpha < 0.003) st.currentAlpha = 0.0;

        const clampedStrings = Math.max(4, Math.min(8, numStrings));
        for (let s = 0; s < 8; ++s) {
          const strActive = s < clampedStrings && s < polyColumns.length ? polyColumns[s].active : false;
          const targetA = strActive ? 1.0 : 0.0;
          const prevA = st.polyColAlpha[s];
          if (st.polyColAlpha[s] < targetA) {
            st.polyColAlpha[s] += (targetA - st.polyColAlpha[s]) * 0.24;
          } else {
            st.polyColAlpha[s] += (targetA - st.polyColAlpha[s]) * 0.065;
          }
          if (st.polyColAlpha[s] < 0.003 && targetA === 0.0) {
            st.polyColAlpha[s] = 0.0;
          }

          let targetRows: [number, number, number, number, number] = [0, 0, 0, 0, 0];
          if (strActive || st.polyColAlpha[s] > 0.003) {
            if (strActive) {
              const targetCents = polyColumns[s].cents;
              if (prevA < 0.02) st.polyColCents[s] = targetCents;
              else st.polyColCents[s] += (targetCents - st.polyColCents[s]) * 0.30;
            }
            const res = computeRowTargetBrightness(
              st.polyColCents[s],
              polyTolerance,
              st.polyColInTune[s]
            );
            st.polyColInTune[s] = res.inTune;
            targetRows = res.rows;
          } else {
            st.polyColInTune[s] = false;
          }

          for (let r = 0; r < 5; ++r) {
            const desired = targetRows[r] * st.polyColAlpha[s];
            if (st.polyRowBrightness[s][r] < desired) {
              st.polyRowBrightness[s][r] += (desired - st.polyRowBrightness[s][r]) * 0.28;
            } else {
              st.polyRowBrightness[s][r] += (desired - st.polyRowBrightness[s][r]) * 0.15;
            }
            if (st.polyRowBrightness[s][r] < 0.003 && desired < 0.003) {
              st.polyRowBrightness[s][r] = 0.0;
            }
          }
        }
      } else {
        for (let s = 0; s < 8; ++s) {
          st.polyColAlpha[s] *= 0.82;
          if (st.polyColAlpha[s] < 0.003) st.polyColAlpha[s] = 0.0;
          for (let r = 0; r < 5; ++r) {
            st.polyRowBrightness[s][r] *= 0.82;
            if (st.polyRowBrightness[s][r] < 0.003) st.polyRowBrightness[s][r] = 0.0;
          }
        }

        const targetAlpha = active ? 1.0 : 0.0;
        if (st.currentAlpha < targetAlpha) {
          st.currentAlpha += (targetAlpha - st.currentAlpha) * 0.18;
        } else {
          st.currentAlpha += (targetAlpha - st.currentAlpha) * 0.055;
        }
        if (st.currentAlpha < 0.003 && targetAlpha === 0.0) {
          st.currentAlpha = 0.0;
        }

        if (st.currentAlpha > 0.005) {
          const dt = 1.0 / 60.0;
          const absCents = Math.abs(cents);
          const sign = cents > 0.0 ? 1.0 : -1.0;

          const slowSpeed = absCents >= slowThresh ? absCents * 0.2 : 0.0;
          let fastSpeed = 0.0;
          if (absCents >= fastThresh) {
            if (absCents <= 15.0) {
              fastSpeed = absCents * 1.2;
            } else {
              fastSpeed = 15.0 * 1.2 + (absCents - 15.0) * 0.35;
            }
          }

          if (slowAbove) {
            st.topAngle -= sign * slowSpeed * dt;
            st.bottomAngle -= sign * fastSpeed * dt;
          } else {
            st.topAngle -= sign * fastSpeed * dt;
            st.bottomAngle -= sign * slowSpeed * dt;
          }
        }
      }

      const canvas = canvasRef.current;
      if (canvas) {
        const dpr = window.devicePixelRatio || 1;
        if (canvas.width !== width * dpr || canvas.height !== height * dpr) {
          canvas.width = width * dpr;
          canvas.height = height * dpr;
        }
        const ctx = canvas.getContext('2d');
        if (ctx) {
          ctx.save();
          ctx.scale(dpr, dpr);
          ctx.clearRect(0, 0, width, height);

          // Draw 115 unlit sockets
          const pts = st.cachedPoints;
          for (let idx = 0; idx < pts.length; ++idx) {
            const { x: px, y: py } = pts[idx];
            const grad = ctx.createRadialGradient(
              px,
              py - dotR * 0.3,
              0,
              px,
              py - dotR * 0.3,
              dotR * 1.3
            );
            grad.addColorStop(0, 'rgb(28, 30, 36)');
            grad.addColorStop(1, 'rgb(14, 15, 18)');
            ctx.beginPath();
            ctx.arc(px, py, dotR, 0, Math.PI * 2);
            ctx.fillStyle = grad;
            ctx.fill();
          }

          const drawLitLed = (
            px: number,
            py: number,
            baseCol: [number, number, number],
            brightness: number
          ) => {
            const glow = ctx.createRadialGradient(px, py, 0, px, py, dotR * 1.6);
            glow.addColorStop(0, rgba(baseCol, brightness * 0.38));
            glow.addColorStop(1, rgba(baseCol, 0));
            ctx.beginPath();
            ctx.arc(px, py, dotR * 1.6, 0, Math.PI * 2);
            ctx.fillStyle = glow;
            ctx.fill();

            ctx.beginPath();
            ctx.arc(px, py, dotR, 0, Math.PI * 2);
            ctx.fillStyle = rgba(baseCol, brightness);
            ctx.fill();

            ctx.beginPath();
            ctx.arc(px, py, dotR * 0.45, 0, Math.PI * 2);
            ctx.fillStyle = `rgba(255, 255, 255, ${brightness * 0.7})`;
            ctx.fill();
          };

          if (isPolyMode && pts.length > 0) {
            const polyGreen: [number, number, number] = [65, 255, 30];
            const polyRed: [number, number, number] = [255, 25, 15];
            const clampedStrings = Math.max(4, Math.min(8, numStrings));
            const { startCol, colWidth } = getPolyLayout(clampedStrings);
            const stride = colWidth + 1;

            for (let s = 0; s < clampedStrings; ++s) {
              if (st.polyColAlpha[s] <= 0.005) continue;

              const col0 = startCol + s * stride;
              for (let r = 0; r < 5; ++r) {
                const rowBright = st.polyRowBrightness[s][r];
                if (rowBright <= 0.015) continue;
                const col = r === 2 ? polyGreen : polyRed;
                const rowOffset = r * numCols;
                for (let k = 0; k < colWidth; ++k) {
                  const gridCol = col0 + k;
                  if (gridCol >= 0 && gridCol < numCols) {
                    const pt = pts[rowOffset + gridCol];
                    drawLitLed(pt.x, pt.y, col, rowBright);
                  }
                }
              }
            }
          } else if (st.currentAlpha >= 0.005 && pts.length > 0) {
            const outerGreen: [number, number, number] = [100, 255, 65];
            const innerCyan: [number, number, number] = [70, 215, 255];

            const absCents = Math.abs(cents);
            const fastGapLen = absCents >= 16.0 ? (absCents >= 35.0 ? 3 : 2) : 1;
            const fastSegLen = absCents >= 16.0 ? (absCents >= 35.0 ? 4 : 5) : 6;

            const drawActiveRow = (
              rowIdx: number,
              offsetA: number,
              baseCol: [number, number, number],
              segmentLen: number,
              gapLen: number
            ) => {
              const period = segmentLen + gapLen;
              const rowOffset = rowIdx * numCols;

              for (let i = 0; i < numCols; ++i) {
                const shiftedIdx = i + offsetA * 3.5;
                let modIdx = shiftedIdx % period;
                if (modIdx < 0.0) modIdx += period;

                if (modIdx < segmentLen) {
                  const normPos = modIdx / (segmentLen - 1.0);
                  let brightness = Math.sin(normPos * Math.PI);
                  brightness =
                    Math.max(0.35, Math.min(1.0, Math.pow(Math.max(0, brightness), 0.7))) *
                    st.currentAlpha;

                  if (brightness > 0.03) {
                    const { x: px, y: py } = pts[rowOffset + i];
                    drawLitLed(px, py, baseCol, brightness);
                  }
                }
              }
            };

            const topSeg = slowAbove ? 6 : fastSegLen;
            const topGap = slowAbove ? 1 : fastGapLen;
            for (let row = 0; row < 3; ++row) {
              drawActiveRow(row, st.topAngle, outerGreen, topSeg, topGap);
            }

            const btmSeg = slowAbove ? fastSegLen : 6;
            const btmGap = slowAbove ? fastGapLen : 1;
            for (let row = 3; row < 5; ++row) {
              drawActiveRow(row, st.bottomAngle, innerCyan, btmSeg, btmGap);
            }
          }

          ctx.restore();
        }
      }

      animId = requestAnimationFrame(render);
    };

    animId = requestAnimationFrame(render);
    return () => cancelAnimationFrame(animId);
  }, [
    cents,
    active,
    fastThresh,
    slowThresh,
    slowAbove,
    isPolyMode,
    polyColumns,
    numStrings,
    polyTolerance,
    width,
    height,
  ]);

  return <canvas ref={canvasRef} style={{ width, height }} className="pointer-events-none" />;
};

// ---------------------------------------------------------------------------
// 3. NoteMatrixComponent (exact port of Source/ui/NoteMatrixComponent.h)
// ---------------------------------------------------------------------------
const NOTE_PATTERNS: Record<string, number[]> = {
  A: [0b0110, 0b1001, 0b1111, 0b1001, 0b1001],
  B: [0b1110, 0b1001, 0b1110, 0b1001, 0b1110],
  C: [0b0111, 0b1000, 0b1000, 0b1000, 0b0111],
  D: [0b1110, 0b1001, 0b1001, 0b1001, 0b1110],
  E: [0b1111, 0b1000, 0b1110, 0b1000, 0b1111],
  F: [0b1111, 0b1000, 0b1110, 0b1000, 0b1000],
  G: [0b0111, 0b1000, 0b1011, 0b1001, 0b0111],
};

const DIGIT_MASKS: number[][] = [
  [0b111, 0b101, 0b101, 0b101, 0b111], // 0
  [0b010, 0b110, 0b010, 0b010, 0b111], // 1
  [0b111, 0b001, 0b111, 0b100, 0b111], // 2
  [0b111, 0b001, 0b111, 0b001, 0b111], // 3
  [0b101, 0b101, 0b111, 0b001, 0b001], // 4
  [0b111, 0b100, 0b111, 0b001, 0b111], // 5
  [0b111, 0b100, 0b111, 0b101, 0b111], // 6
  [0b111, 0b001, 0b010, 0b010, 0b010], // 7
  [0b111, 0b101, 0b111, 0b101, 0b111], // 8
  [0b111, 0b101, 0b111, 0b001, 0b111], // 9
];

const SHARP_MASK = [0b01010, 0b11111, 0b01010, 0b11111, 0b01010];

export interface NoteMatrixProps {
  noteName: string;
  isSharp: boolean;
  octaveNum: number;
  active: boolean;
  cents: number;
  inTuneTolerance: number;
  showOctave: boolean;
  width?: number;
  height?: number;
}

export const NoteMatrixCanvas: React.FC<NoteMatrixProps> = ({
  noteName,
  isSharp,
  octaveNum,
  active,
  cents,
  inTuneTolerance,
  showOctave,
  width = 200,
  height = 100,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const stateRef = useRef({
    currentNote: ' ',
    sharpActive: false,
    currentOctave: 4,
    currentAlpha: 0.0,
    targetInTune: false,
    colourMorph: 0.0,
  });

  useEffect(() => {
    let animId = 0;

    const render = () => {
      const st = stateRef.current;
      const targetAlpha = active ? 1.0 : 0.0;
      const halfTol = inTuneTolerance * 0.5;
      const absCents = Math.abs(cents);

      if (st.targetInTune) {
        st.targetInTune = absCents <= halfTol * 1.15;
      } else {
        st.targetInTune = absCents <= halfTol;
      }

      if (active) {
        st.currentNote = noteName;
        st.sharpActive = isSharp;
        st.currentOctave = octaveNum;
      } else if (st.currentAlpha < 0.02) {
        st.currentNote = ' ';
        st.sharpActive = false;
      }

      if (st.currentAlpha < targetAlpha) {
        st.currentAlpha += (targetAlpha - st.currentAlpha) * 0.18;
      } else {
        st.currentAlpha += (targetAlpha - st.currentAlpha) * 0.055;
      }
      if (st.currentAlpha < 0.002 && targetAlpha === 0.0) {
        st.currentAlpha = 0.0;
      }

      const targetMorph = st.targetInTune && active ? 1.0 : 0.0;
      st.colourMorph += (targetMorph - st.colourMorph) * 0.22;
      if (Math.abs(st.colourMorph - targetMorph) < 0.002) {
        st.colourMorph = targetMorph;
      }

      const canvas = canvasRef.current;
      if (canvas) {
        const dpr = window.devicePixelRatio || 1;
        if (canvas.width !== width * dpr || canvas.height !== height * dpr) {
          canvas.width = width * dpr;
          canvas.height = height * dpr;
        }
        const ctx = canvas.getContext('2d');
        if (ctx) {
          ctx.save();
          ctx.scale(dpr, dpr);
          ctx.clearRect(0, 0, width, height);

          const bloomMargin = 16.0;
          const drawH = height - bloomMargin * 2;
          const drawY = bloomMargin;

          const stepY = drawH / 5.0;
          const stepX = stepY * 0.95;
          const ledRadius = stepY * 0.38;

          const noteStartX = width * 0.5 - 1.5 * stepX;
          const noteStartY = drawY + stepY * 0.5;

          const subRadius = ledRadius * 0.48;
          const subStep = stepY * 0.5;
          const slantFactor = 0.1 * stepX;

          const rightBlockX = noteStartX + 4.3 * stepX;
          const sharpStartY = noteStartY - 1.2 * stepY;
          const octStartY = noteStartY + 3.2 * stepY;

          const pattern = NOTE_PATTERNS[st.currentNote] || null;
          const octPattern =
            st.currentOctave >= 0 && st.currentOctave <= 9
              ? DIGIT_MASKS[st.currentOctave]
              : null;

          const drawWideBloom = (cx: number, cy: number, r: number, a: number, morph: number) => {
            const baseBloom = lerpColor([255, 30, 25], [60, 220, 45], morph);
            const bloomR = r * 3.2;
            const bloomAlpha = (0.4 + (0.34 - 0.4) * morph) * a;
            const grad = ctx.createRadialGradient(cx, cy, 0, cx, cy, bloomR);
            grad.addColorStop(0, rgba(baseBloom, bloomAlpha));
            grad.addColorStop(1, rgba(baseBloom, 0));
            ctx.beginPath();
            ctx.arc(cx, cy, bloomR, 0, Math.PI * 2);
            ctx.fillStyle = grad;
            ctx.fill();
          };

          const drawTightHalo = (cx: number, cy: number, r: number, a: number, morph: number) => {
            const baseNeon = lerpColor([255, 45, 35], [85, 230, 55], morph);
            const haloR = r * 1.85;
            const haloAlpha = (0.55 + (0.46 - 0.55) * morph) * a;
            const grad = ctx.createRadialGradient(cx, cy, 0, cx, cy, haloR);
            grad.addColorStop(0, rgba(baseNeon, haloAlpha));
            grad.addColorStop(1, rgba(baseNeon, 0));
            ctx.beginPath();
            ctx.arc(cx, cy, haloR, 0, Math.PI * 2);
            ctx.fillStyle = grad;
            ctx.fill();
          };

          const drawLedBody = (
            cx: number,
            cy: number,
            r: number,
            isActive: boolean,
            a: number,
            morph: number
          ) => {
            const offGrad = ctx.createRadialGradient(cx, cy - r * 0.3, 0, cx, cy - r * 0.3, r * 1.3);
            offGrad.addColorStop(0, 'rgb(28, 30, 36)');
            offGrad.addColorStop(1, 'rgb(14, 15, 18)');
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, Math.PI * 2);
            ctx.fillStyle = offGrad;
            ctx.fill();

            ctx.lineWidth = 0.6;
            ctx.strokeStyle = 'rgba(40, 44, 52, 0.45)';
            ctx.stroke();

            if (isActive && a > 0.01) {
              const curCenter = lerpColor([255, 110, 95], [71, 216, 6], morph);
              const curEdge = lerpColor([225, 20, 20], [69, 154, 32], morph);

              const coreGrad = ctx.createRadialGradient(
                cx,
                cy - r * 0.2,
                0,
                cx,
                cy - r * 0.2,
                r * 1.35
              );
              coreGrad.addColorStop(0, rgba(curCenter, a));
              coreGrad.addColorStop(1, rgba(curEdge, 0.9 * a));
              ctx.beginPath();
              ctx.arc(cx, cy, r * 1.12, 0, Math.PI * 2);
              ctx.fillStyle = coreGrad;
              ctx.fill();

              const hotAlpha = (0.95 + (0.75 - 0.95) * morph) * a;
              const hotGrad = ctx.createRadialGradient(
                cx,
                cy,
                0,
                cx,
                cy,
                r * (0.9 + 0.1 * morph)
              );
              hotGrad.addColorStop(0, `rgba(255, 255, 240, ${hotAlpha})`);
              hotGrad.addColorStop(1, 'rgba(255, 255, 240, 0)');
              ctx.beginPath();
              ctx.arc(cx, cy, r * 0.7, 0, Math.PI * 2);
              ctx.fillStyle = hotGrad;
              ctx.fill();
            }
          };

          // Stage 1 & 2: Wide Bloom & Tight Halo
          if (st.currentAlpha > 0.02) {
            const drawGlowPass = (
              fn: (cx: number, cy: number, r: number, a: number, morph: number) => void
            ) => {
              for (let row = 0; row < 5; ++row) {
                const rowBits = pattern ? pattern[row] : 0;
                for (let col = 0; col < 4; ++col) {
                  if ((rowBits & (1 << (3 - col))) !== 0) {
                    fn(
                      noteStartX + col * stepX,
                      noteStartY + row * stepY,
                      ledRadius,
                      st.currentAlpha,
                      st.colourMorph
                    );
                  }
                }
              }

              if (st.sharpActive) {
                for (let row = 0; row < 5; ++row) {
                  const slantOffset = (4 - row) * slantFactor;
                  for (let col = 0; col < 5; ++col) {
                    if ((SHARP_MASK[row] & (1 << (4 - col))) !== 0) {
                      fn(
                        rightBlockX + col * subStep + slantOffset,
                        sharpStartY + row * subStep,
                        subRadius,
                        st.currentAlpha,
                        st.colourMorph
                      );
                    }
                  }
                }
              }

              if (showOctave && st.currentNote !== ' ' && octPattern) {
                for (let row = 0; row < 5; ++row) {
                  for (let col = 0; col < 3; ++col) {
                    if ((octPattern[row] & (1 << (2 - col))) !== 0) {
                      fn(
                        rightBlockX + 0.9 * subStep + col * subStep,
                        octStartY + row * subStep,
                        subRadius,
                        st.currentAlpha,
                        st.colourMorph
                      );
                    }
                  }
                }
              }
            };

            drawGlowPass(drawWideBloom);
            drawGlowPass(drawTightHalo);
          }

          // Stage 3: LED Bodies
          for (let row = 0; row < 5; ++row) {
            const rowBits = pattern ? pattern[row] : 0;
            for (let col = 0; col < 4; ++col) {
              const isLit = (rowBits & (1 << (3 - col))) !== 0;
              drawLedBody(
                noteStartX + col * stepX,
                noteStartY + row * stepY,
                ledRadius,
                isLit,
                st.currentAlpha,
                st.colourMorph
              );
            }
          }

          for (let row = 0; row < 5; ++row) {
            const slantOffset = (4 - row) * slantFactor;
            for (let col = 0; col < 5; ++col) {
              const isLit = st.sharpActive && (SHARP_MASK[row] & (1 << (4 - col))) !== 0;
              drawLedBody(
                rightBlockX + col * subStep + slantOffset,
                sharpStartY + row * subStep,
                subRadius,
                isLit,
                st.currentAlpha,
                st.colourMorph
              );
            }
          }

          if (showOctave) {
            for (let row = 0; row < 5; ++row) {
              for (let col = 0; col < 3; ++col) {
                const isLit =
                  st.currentNote !== ' ' &&
                  octPattern !== null &&
                  (octPattern[row] & (1 << (2 - col))) !== 0;
                drawLedBody(
                  rightBlockX + 0.9 * subStep + col * subStep,
                  octStartY + row * subStep,
                  subRadius,
                  isLit,
                  st.currentAlpha,
                  st.colourMorph
                );
              }
            }
          }

          ctx.restore();
        }
      }

      animId = requestAnimationFrame(render);
    };

    animId = requestAnimationFrame(render);
    return () => cancelAnimationFrame(animId);
  }, [noteName, isSharp, octaveNum, active, cents, inTuneTolerance, showOctave, width, height]);

  return <canvas ref={canvasRef} style={{ width, height }} className="pointer-events-none" />;
};

// ---------------------------------------------------------------------------
// 4. SteppedKnobComponent (exact port of Source/ui/SteppedKnobComponent.h)
// ---------------------------------------------------------------------------
export interface SteppedKnobProps {
  title: string;
  minVal: number;
  maxVal: number;
  defaultVal: number;
  value: number;
  stepVal?: number;
  onChange: (val: number) => void;
  width?: number;
  height?: number;
}

export const SteppedKnob: React.FC<SteppedKnobProps> = ({
  title,
  minVal,
  maxVal,
  defaultVal,
  value,
  stepVal = 1,
  onChange,
  width = 50,
  height = 68,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const dragRef = useRef<{ active: boolean; startX: number; startY: number; startVal: number }>({
    active: false,
    startX: 0,
    startY: 0,
    startVal: value,
  });

  const clampAndSnap = (raw: number) => {
    const snapped = minVal + Math.round((raw - minVal) / stepVal) * stepVal;
    return Math.max(minVal, Math.min(maxVal, snapped));
  };

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = width * dpr;
    canvas.height = height * dpr;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.save();
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, width, height);

    // 1. Title
    ctx.fillStyle = 'rgb(90, 95, 105)';
    ctx.font = 'bold 11px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(title, width * 0.5, 7);

    const cx = width * 0.5;
    const cy = 34.0;
    const knobR = 14.0;

    // 2. End ticks (-135 deg and +135 deg)
    const minAngle = -Math.PI * 0.75;
    const maxAngle = Math.PI * 0.75;

    ctx.strokeStyle = 'rgb(80, 85, 95)';
    ctx.lineWidth = 1.3;
    for (const tickA of [minAngle, maxAngle]) {
      const r1 = knobR + 2.0;
      const r2 = knobR + 5.2;
      ctx.beginPath();
      ctx.moveTo(cx + Math.sin(tickA) * r1, cy - Math.cos(tickA) * r1);
      ctx.lineTo(cx + Math.sin(tickA) * r2, cy - Math.cos(tickA) * r2);
      ctx.stroke();
    }

    // 3. Drop shadow
    ctx.beginPath();
    ctx.arc(cx, cy + 2.0, knobR, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(0, 0, 0, 0.22)';
    ctx.fill();

    // 4. Outer metallic bevel
    const bevelGrad = ctx.createLinearGradient(
      cx - knobR * 0.7,
      cy - knobR * 0.7,
      cx + knobR * 0.7,
      cy + knobR * 0.7
    );
    bevelGrad.addColorStop(0.0, 'rgb(242, 245, 248)');
    bevelGrad.addColorStop(0.45, 'rgb(175, 180, 188)');
    bevelGrad.addColorStop(1.0, 'rgb(95, 100, 110)');
    ctx.beginPath();
    ctx.arc(cx, cy, knobR, 0, Math.PI * 2);
    ctx.fillStyle = bevelGrad;
    ctx.fill();
    ctx.lineWidth = 0.8;
    ctx.strokeStyle = 'rgb(110, 115, 125)';
    ctx.stroke();

    // 5. Top brushed face
    const capR = knobR - 2.0;
    const capGrad = ctx.createLinearGradient(
      cx - capR * 0.5,
      cy - capR * 0.6,
      cx + capR * 0.5,
      cy + capR * 0.6
    );
    capGrad.addColorStop(0.0, 'rgb(228, 231, 236)');
    capGrad.addColorStop(0.5, 'rgb(196, 200, 207)');
    capGrad.addColorStop(1.0, 'rgb(162, 167, 175)');
    ctx.beginPath();
    ctx.arc(cx, cy, capR, 0, Math.PI * 2);
    ctx.fillStyle = capGrad;
    ctx.fill();

    ctx.beginPath();
    ctx.arc(cx, cy, capR - 0.5, 0, Math.PI * 2);
    ctx.lineWidth = 0.8;
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.65)';
    ctx.stroke();

    // 6. Indicator dot
    const norm = maxVal > minVal ? (value - minVal) / (maxVal - minVal) : 0.0;
    const angle = minAngle + norm * (maxAngle - minAngle);
    const dotDist = capR - 3.1;
    const dx = cx + Math.sin(angle) * dotDist;
    const dy = cy - Math.cos(angle) * dotDist;
    const dotR = 1.5;

    ctx.beginPath();
    ctx.arc(dx, dy + 0.6, dotR, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(255, 255, 255, 0.6)';
    ctx.fill();

    ctx.beginPath();
    ctx.arc(dx, dy, dotR, 0, Math.PI * 2);
    ctx.fillStyle = 'rgb(40, 44, 52)';
    ctx.fill();

    // 7. Value label under knob
    ctx.fillStyle = 'rgb(45, 50, 58)';
    ctx.font = 'bold 11.5px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
    ctx.fillText(String(value), width * 0.5, 59);

    ctx.restore();
  }, [title, minVal, maxVal, value, width, height]);

  useEffect(() => {
    const handleMove = (e: MouseEvent) => {
      if (!dragRef.current.active) return;
      const delta = e.clientX - dragRef.current.startX - (e.clientY - dragRef.current.startY);
      const steps = Math.round(delta / 9.0);
      const next = clampAndSnap(dragRef.current.startVal + steps * stepVal);
      onChange(next);
    };
    const handleUp = () => {
      dragRef.current.active = false;
    };
    window.addEventListener('mousemove', handleMove);
    window.addEventListener('mouseup', handleUp);
    return () => {
      window.removeEventListener('mousemove', handleMove);
      window.removeEventListener('mouseup', handleUp);
    };
  });

  return (
    <canvas
      ref={canvasRef}
      style={{ width, height }}
      className="cursor-ns-resize"
      onMouseDown={(e) => {
        dragRef.current = {
          active: true,
          startX: e.clientX,
          startY: e.clientY,
          startVal: value,
        };
      }}
      onDoubleClick={() => onChange(defaultVal)}
      onWheel={(e) => {
        e.preventDefault();
        if (e.deltaY < 0) onChange(clampAndSnap(value + stepVal));
        else if (e.deltaY > 0) onChange(clampAndSnap(value - stepVal));
      }}
    />
  );
};

// ---------------------------------------------------------------------------
// 5. GuitarBassSwitchComponent (exact port of Source/ui/GuitarBassSwitchComponent.h)
// ---------------------------------------------------------------------------
export const GuitarBassSwitch: React.FC<{
  isBass: boolean;
  onChange: (bass: boolean) => void;
  width?: number;
  height?: number;
}> = ({ isBass, onChange, width = 132, height = 22 }) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = width * dpr;
    canvas.height = height * dpr;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.save();
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, width, height);

    const midY = height * 0.5;
    const trackW = 36.0;
    const trackH = 14.0;
    const trackX = width * 0.5 - trackW * 0.5 + 2.0;
    const trackY = midY - trackH * 0.5;

    ctx.font = 'bold 11.5px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
    ctx.textBaseline = 'middle';

    ctx.fillStyle = !isBass ? 'rgb(50, 55, 65)' : 'rgb(125, 130, 140)';
    ctx.textAlign = 'right';
    ctx.fillText('Guitar', trackX - 5.0, midY);

    ctx.fillStyle = isBass ? 'rgb(50, 55, 65)' : 'rgb(125, 130, 140)';
    ctx.textAlign = 'left';
    ctx.fillText('Bass', trackX + trackW + 6.0, midY);

    // Outer 3D metallic bezel
    const obX = trackX - 1.8;
    const obY = trackY - 1.8;
    const obW = trackW + 3.6;
    const obH = trackH + 3.6;
    const bezelGrad = ctx.createLinearGradient(obX, obY, obX, obY + obH);
    bezelGrad.addColorStop(0, 'rgb(130, 135, 145)');
    bezelGrad.addColorStop(1, 'rgb(248, 250, 252)');
    ctx.beginPath();
    ctx.roundRect(obX, obY, obW, obH, obH * 0.5);
    ctx.fillStyle = bezelGrad;
    ctx.fill();
    ctx.lineWidth = 0.8;
    ctx.strokeStyle = 'rgb(105, 110, 120)';
    ctx.stroke();

    // Inner recessed slot
    const slotGrad = ctx.createLinearGradient(trackX, trackY, trackX, trackY + trackH);
    slotGrad.addColorStop(0, 'rgb(145, 150, 160)');
    slotGrad.addColorStop(1, 'rgb(215, 220, 228)');
    ctx.beginPath();
    ctx.roundRect(trackX, trackY, trackW, trackH, trackH * 0.5);
    ctx.fillStyle = slotGrad;
    ctx.fill();
    ctx.lineWidth = 1.0;
    ctx.strokeStyle = 'rgba(75, 80, 90, 0.6)';
    ctx.stroke();

    // 3D metallic thumb pin
    const thumbR = (trackH - 2.6) * 0.5;
    const leftX = trackX + thumbR + 1.6;
    const rightX = trackX + trackW - thumbR - 1.6;
    const thumbX = isBass ? rightX : leftX;
    const thumbY = midY;

    ctx.beginPath();
    ctx.arc(thumbX + (isBass ? -1.2 : 1.2), thumbY + 0.8, thumbR, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(0, 0, 0, 0.28)';
    ctx.fill();

    const thumbGrad = ctx.createLinearGradient(
      thumbX - thumbR * 0.6,
      thumbY - thumbR * 0.6,
      thumbX + thumbR * 0.7,
      thumbY + thumbR * 0.7
    );
    thumbGrad.addColorStop(0, 'rgb(245, 248, 252)');
    thumbGrad.addColorStop(0.5, 'rgb(175, 182, 192)');
    thumbGrad.addColorStop(1, 'rgb(95, 102, 115)');
    ctx.beginPath();
    ctx.arc(thumbX, thumbY, thumbR, 0, Math.PI * 2);
    ctx.fillStyle = thumbGrad;
    ctx.fill();

    const capR = thumbR * 0.62;
    const capGrad = ctx.createLinearGradient(
      thumbX - capR,
      thumbY - capR,
      thumbX + capR,
      thumbY + capR
    );
    capGrad.addColorStop(0, 'rgb(140, 146, 158)');
    capGrad.addColorStop(1, 'rgb(235, 238, 244)');
    ctx.beginPath();
    ctx.arc(thumbX, thumbY, capR, 0, Math.PI * 2);
    ctx.fillStyle = capGrad;
    ctx.fill();

    ctx.beginPath();
    ctx.arc(thumbX, thumbY, thumbR, 0, Math.PI * 2);
    ctx.lineWidth = 0.7;
    ctx.strokeStyle = 'rgb(70, 75, 85)';
    ctx.stroke();

    ctx.restore();
  }, [isBass, width, height]);

  return (
    <canvas
      ref={canvasRef}
      style={{ width, height }}
      className="cursor-pointer"
      onClick={(e) => {
        const rect = e.currentTarget.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const midX = width * 0.5;
        if (x < midX - 18) onChange(false);
        else if (x > midX + 22) onChange(true);
        else onChange(!isBass);
      }}
    />
  );
};

// ---------------------------------------------------------------------------
// 6. FootswitchComponent (exact port of Source/ui/FootswitchComponent.h)
// ---------------------------------------------------------------------------
export const Footswitch: React.FC<{
  muted: boolean;
  onToggle: (muted: boolean) => void;
  width?: number;
  height?: number;
}> = ({ muted, onToggle, width = 62, height = 78 }) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [isMouseDown, setIsMouseDown] = useState(false);
  const glowRef = useRef(muted ? 1.0 : 0.0);

  useEffect(() => {
    let animId = 0;

    const render = () => {
      const target = muted ? 1.0 : 0.0;
      if (Math.abs(glowRef.current - target) > 0.005) {
        glowRef.current += (target - glowRef.current) * 0.22;
      } else {
        glowRef.current = target;
      }

      const canvas = canvasRef.current;
      if (canvas) {
        const dpr = window.devicePixelRatio || 1;
        if (canvas.width !== width * dpr || canvas.height !== height * dpr) {
          canvas.width = width * dpr;
          canvas.height = height * dpr;
        }
        const ctx = canvas.getContext('2d');
        if (ctx) {
          ctx.save();
          ctx.scale(dpr, dpr);
          ctx.clearRect(0, 0, width, height);

          const midX = width * 0.5;
          const midY = 42.0;
          const outerRadius = 19.5;
          const glowAlpha = glowRef.current;

          // 1. "mute" label
          ctx.fillStyle = 'rgb(35, 38, 45)';
          ctx.font = 'bold 11.5px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          ctx.fillText('mute', midX, 8.5);

          // 2. Ruby halo
          if (glowAlpha > 0.01) {
            const passes = 11;
            const maxSpread = 5.0;
            for (let i = passes; i >= 1; --i) {
              const t = i / passes;
              const r = outerRadius + t * maxSpread;
              const profile = 0.5 * (1.0 + Math.cos(t * Math.PI));
              const ringCol = lerpColor([245, 10, 20], [175, 0, 10], t);
              const alpha = profile * 0.12 * glowAlpha;

              ctx.beginPath();
              ctx.arc(midX, midY, r, 0, Math.PI * 2);
              ctx.lineWidth = 1.1;
              ctx.strokeStyle = rgba(ringCol, alpha);
              ctx.stroke();
            }

            for (let i = 0; i < 3; ++i) {
              const r = outerRadius + 0.4 + i * 0.45;
              const coreAlpha = (0.8 - i * 0.2) * glowAlpha;
              ctx.beginPath();
              ctx.arc(midX, midY, r, 0, Math.PI * 2);
              ctx.lineWidth = 1.0;
              ctx.strokeStyle = `rgba(255, 30, 25, ${coreAlpha})`;
              ctx.stroke();
            }
          }

          // 3. Washer shadow
          ctx.beginPath();
          ctx.arc(midX, midY + 1.8, outerRadius, 0, Math.PI * 2);
          ctx.fillStyle = 'rgba(0, 0, 0, 0.35)';
          ctx.fill();

          // 4. Massive chrome rim
          const rimGrad = ctx.createLinearGradient(
            midX - outerRadius * 0.7,
            midY - outerRadius * 0.7,
            midX + outerRadius * 0.7,
            midY + outerRadius * 0.7
          );
          rimGrad.addColorStop(0.0, 'rgb(245, 247, 250)');
          rimGrad.addColorStop(0.25, 'rgb(185, 190, 200)');
          rimGrad.addColorStop(0.65, 'rgb(95, 100, 110)');
          rimGrad.addColorStop(0.85, 'rgb(210, 215, 225)');
          rimGrad.addColorStop(1.0, 'rgb(55, 60, 70)');
          ctx.beginPath();
          ctx.arc(midX, midY, outerRadius, 0, Math.PI * 2);
          ctx.fillStyle = rimGrad;
          ctx.fill();

          // Dark groove
          const grooveR = outerRadius - 4.4;
          ctx.beginPath();
          ctx.arc(midX, midY, grooveR, 0, Math.PI * 2);
          ctx.fillStyle = 'rgb(14, 16, 20)';
          ctx.fill();

          // 5. Inner plunger
          const plungerR = grooveR - 1.2;
          const pushOffset = isMouseDown ? 0.8 : 0.0;
          const plungerGrad = ctx.createLinearGradient(
            midX - plungerR * 0.35,
            midY - plungerR * 0.45 + pushOffset,
            midX + plungerR * 0.5,
            midY + plungerR * 0.6 + pushOffset
          );
          plungerGrad.addColorStop(0.0, 'rgb(235, 238, 243)');
          plungerGrad.addColorStop(0.7, 'rgb(175, 180, 190)');
          plungerGrad.addColorStop(1.0, 'rgb(155, 160, 170)');
          ctx.beginPath();
          ctx.arc(midX, midY + pushOffset, plungerR, 0, Math.PI * 2);
          ctx.fillStyle = plungerGrad;
          ctx.fill();

          // Specular highlight
          ctx.beginPath();
          ctx.ellipse(
            midX,
            midY + pushOffset - 0.4,
            plungerR - 1.0,
            plungerR - 1.4,
            0,
            0,
            Math.PI * 2
          );
          ctx.lineWidth = 1.0;
          ctx.strokeStyle = 'rgba(255, 255, 255, 0.75)';
          ctx.stroke();

          ctx.restore();
        }
      }

      animId = requestAnimationFrame(render);
    };

    animId = requestAnimationFrame(render);
    return () => cancelAnimationFrame(animId);
  }, [muted, isMouseDown, width, height]);

  return (
    <canvas
      ref={canvasRef}
      style={{ width, height }}
      className="cursor-pointer"
      onMouseDown={() => setIsMouseDown(true)}
      onMouseUp={() => {
        setIsMouseDown(false);
        onToggle(!muted);
      }}
      onMouseLeave={() => setIsMouseDown(false)}
    />
  );
};

// ---------------------------------------------------------------------------
// 7. SettingsButton (exact port of Source/ui/SettingsButton.h)
// ---------------------------------------------------------------------------
export const SettingsTabButton: React.FC<{
  onClick: () => void;
  width?: number;
  height?: number;
}> = ({ onClick, width = 22, height = 22 }) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [hovered, setHovered] = useState(false);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = width * dpr;
    canvas.height = height * dpr;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.save();
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, width, height);

    const r = 11.0;
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(width - r, 0);
    ctx.arc(width - r, r, r, -Math.PI * 0.5, 0);
    ctx.lineTo(width, height - r);
    ctx.arc(width - r, height - r, r, 0, Math.PI * 0.5);
    ctx.lineTo(0, height);
    ctx.closePath();

    ctx.fillStyle = hovered ? 'rgb(248, 249, 251)' : 'rgb(236, 238, 242)';
    ctx.fill();
    ctx.lineWidth = 1.2;
    ctx.strokeStyle = 'rgb(172, 177, 185)';
    ctx.stroke();

    const cx = width * 0.5;
    const cy = height * 0.5;
    const rOuter = 6.8;
    const rInner = 4.8;
    const holeR = 2.4;
    const numTeeth = 8;
    const angleStep = (Math.PI * 2) / numTeeth;

    ctx.beginPath();
    for (let i = 0; i < numTeeth; ++i) {
      const a0 = i * angleStep;
      const a1 = a0 + angleStep * 0.35;
      const a2 = a0 + angleStep * 0.55;
      const a3 = a0 + angleStep * 0.9;

      if (i === 0) ctx.moveTo(cx + Math.cos(a0) * rInner, cy + Math.sin(a0) * rInner);
      else ctx.lineTo(cx + Math.cos(a0) * rInner, cy + Math.sin(a0) * rInner);

      ctx.lineTo(cx + Math.cos(a1) * rOuter, cy + Math.sin(a1) * rOuter);
      ctx.lineTo(cx + Math.cos(a2) * rOuter, cy + Math.sin(a2) * rOuter);
      ctx.lineTo(cx + Math.cos(a3) * rInner, cy + Math.sin(a3) * rInner);
    }
    ctx.closePath();

    ctx.moveTo(cx + holeR, cy);
    ctx.arc(cx, cy, holeR, 0, Math.PI * 2, true);

    ctx.fillStyle = hovered ? 'rgb(90, 95, 105)' : 'rgb(140, 145, 155)';
    ctx.fill('evenodd');

    ctx.restore();
  }, [hovered, width, height]);

  return (
    <canvas
      ref={canvasRef}
      style={{ width, height }}
      className="cursor-pointer"
      onMouseEnter={() => setHovered(true)}
      onMouseLeave={() => setHovered(false)}
      onClick={onClick}
    />
  );
};
