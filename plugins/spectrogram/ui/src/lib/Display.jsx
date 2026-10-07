// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The picture, its frequency scale, the time axis under it and the crosshair's
 * readout.
 *
 * THE SCALES ARE OUTSIDE THE WELL, not laid over the picture: over it a label
 * is legible against silence and invisible against a loud partial, and fixing
 * that needs a floating plate, which nothing in this system is.
 */
import { Index } from 'solid-js';
import { Spectrogram } from '@ultraviolet/ui';

/* One column per CSS pixel and one band per CSS pixel: see app.css. */
export const PICTURE_W = 606;
export const PICTURE_H = 256;

export function Display(props) {
  return (
    <>
      <div class="display">
        <div class="scale">
          {/* <Index>, keyed by position: the marks only move when the axis does,
            * and their DOM is reused rather than rebuilt. */}
          <Index each={props.freqMarks}>{(m) => (
            <div class="scale-mark t-label" style={{ top: `${m().y}px` }}>
              <span>{m().label}</span>
              <i class="scale-tick" />
            </div>
          )}</Index>
        </div>
        <div class="well">
          <Spectrogram
            width={PICTURE_W}
            height={PICTURE_H}
            cols={PICTURE_W}
            batch={props.batch}
            paused={props.paused}
            generation={props.generation}
            view={props.bars ? 'bars' : 'time'}
            clash={props.clash}
            onHover={props.onHover}
          />
        </div>
      </div>

      {/* Inset to line up with the canvas, not the window. */}
      <div class="under">
        <div class="time-axis" classList={{ bars: props.bars }}>
          <Index each={props.timeMarks}>{(m) => (
            <div class="time-mark t-hint" data-anchor={m().anchor ?? 'mid'}
                 classList={{ beat: !!m().beat }} style={{ left: `${m().x}px` }}>
              <i class="time-tick" />
              <span>{m().label}</span>
            </div>
          )}</Index>
        </div>

        {/* Keys in the label voice, values in t-value, which does not uppercase
          * "kHz" and "dB". */}
        <div class="crosshair-read">
          <span class="xh-key t-label">freq</span><span class="xh-val t-value">{props.freq}</span>
          <span class="xh-key t-label">{props.bars ? 'pos' : 'time'}</span>
          <span class="xh-val t-value">{props.time}</span>
          <span class="xh-key t-label">level</span><span class="xh-val t-value">{props.level}</span>
        </div>
      </div>
    </>
  );
}
