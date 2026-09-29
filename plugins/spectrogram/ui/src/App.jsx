/*
 * Spectrogram — the editor.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Three jobs, and nothing else: decode the columns the plugin pushes, hand them
 * to the canvas, and draw the frequency scale beside it.
 *
 * WHAT THIS FILE IS NOT ALLOWED TO DO IS DECIDE ANYTHING ABOUT THE ANALYSIS. The
 * band count, the log mapping and the dB floor all live in the Rust analyzer, and
 * the scale below is drawn from the centre frequencies IT sends. A second copy of
 * the mapping here would look right for as long as nobody changed the first one.
 */
import { createSignal, onMount, onCleanup } from 'solid-js';
import { onMessage, sendMessage } from '@ultraviolet/ui';
import { MSG } from './lib/msg.js';
import { decodeColumns, decodeAxis, marksFor, RANGES } from './lib/columns.js';
import Spectrogram from './lib/Spectrogram.jsx';
import { Hint, Button, Select } from '@ultraviolet/ui';

/* Mirrored by PLUG_WIDTH in config.h and by `main` in app.css. */
const DESIGN_W = 720;
/*
 * One column per CSS pixel and one band per CSS pixel: see the arithmetic at the
 * top of app.css. The well around this is two pixels larger in each direction,
 * which is the fix for the frame eating the bottom bands.
 */
const PICTURE_W = 606;
const PICTURE_H = 256;
/* ~47 columns a second, held constant across sample rates by the engine's hop
 * rule -- so this one number turns the width into a span of time. */
const COLUMNS_PER_S = 47;

export default function App() {
  const [batch, setBatch] = createSignal(null);
  const [axis, setAxis] = createSignal(null);
  const [scale, setScale] = createSignal(1);
  const [paused, setPaused] = createSignal(false);
  const [range, setRange] = createSignal(0);
  /* Bumped on every range change: the picture's history is about other
   * frequencies now, and the canvas clears on it. */
  const [generation, setGeneration] = createSignal(0);
  /* Columns stop arriving when the host stops calling OnIdle -- or when nothing
   * is loaded on the track at all. Saying so beats a frozen picture that looks
   * like a crash. */
  const [live, setLive] = createSignal(false);
  let lastSeen = 0;

  /*
   * THE PAGE IS SCALED, NOT LAID OUT FLUIDLY, and it is the Trance Gate's
   * approach for the Trance Gate's reason: the numbers in app.css ARE the design,
   * and a fluid layout that happens to look close is a different drawing. A host
   * that gives the WebView a viewport narrower than 720 -- Live's own scaling
   * does -- gets the whole window scaled to fit.
   */
  const fit = () => {
    const el = document.querySelector('main');
    if (!el) return 1;
    const k = Math.max(0.1, (window.innerWidth || DESIGN_W) / DESIGN_W);
    el.style.transformOrigin = 'top left';
    el.style.transform = `scale(${k})`;
    setScale(k);
    return k;
  };

  onMount(() => {
    fit();
    window.addEventListener('resize', fit);

    const off = onMessage((tag, text) => {
      if (tag === MSG.cols) {
        /*
         * NOT GATED ON `paused`. The freeze is a repaint gate in the canvas;
         * the columns still arrive and are still written, which is what makes
         * unpausing show a current picture instead of resuming from a seam.
         */
        const decoded = decodeColumns(text);
        if (decoded) {
          lastSeen = performance.now();
          setLive(true);
          setBatch(decoded);
        }
      } else if (tag === MSG.axis) {
        setAxis(decodeAxis(text));
      }
    });

    /*
     * Half a second of nothing is a stall, not a gap: at the analyzer's defaults
     * a column arrives every 21 ms.
     *
     * It needs no exemption for pause any more. Pause stops the drawing, not the
     * sending, so columns keep arriving and this keeps meaning what it says.
     */
    const watchdog = setInterval(() => {
      if (live() && performance.now() - lastSeen > 500) setLive(false);
    }, 250);

    /* LAST, AND IT HAS TO BE FROM HERE. The plugin sends the axis from OnUIOpen
     * as well, but that fires before this deferred module has evaluated and
     * lands on an undefined global -- see MSG.ready. */
    sendMessage(MSG.ready);

    onCleanup(() => {
      off();
      clearInterval(watchdog);
      window.removeEventListener('resize', fit);
    });
  });

  /*
   * PAUSE NEVER LEAVES THE EDITOR. The plugin is not told, because there is
   * nothing for it to do: the analysis runs, the columns arrive, the history
   * fills. Only the repaint stops.
   */
  const togglePause = () => setPaused((p) => !p);

  /*
   * The zoom. The plugin re-bands the analysis and sends the new scale back
   * rather than the editor assuming its request was honoured -- f_max is
   * clamped to Nyquist, so in a 32 kHz session "High" really is 2 k to 16 k.
   *
   * Unpauses: a frozen picture of a range you have just left is a lie, and
   * clicking a dropdown is not how anyone expects to be shown stale data.
   */
  const chooseRange = (i) => {
    const r = RANGES[i];
    if (!r) return;
    setRange(i);
    setPaused(false);
    setGeneration((g) => g + 1);
    sendMessage(MSG.range, `${r.lo}:${r.hi}`);
  };

  /* "200 Hz", not "0.2 kHz": the unit follows the number rather than the other
   * end of the range. The system's rule is that a value carries its unit, and
   * a zoomed view whose top is 200 Hz should say so in hertz. */
  const asHz = (v) => (v < 1000 ? `${Math.round(v)} Hz` : `${(v / 1000).toFixed(1)} kHz`);

  const hint = () => {
    const hz = axis();
    const range = hz && hz.length > 1
      ? `${asHz(hz[0])} – ${asHz(hz[hz.length - 1])}`
      : 'waiting for the plugin';
    return [
      ['log', range],
      ['floor', '−96 dB'],
      ['history', `${Math.round(PICTURE_W / COLUMNS_PER_S)} s`],
    ];
  };

  return (
    <main>
      <div class="title-row">
        <span class="t-title">Spectrogram</span>
        <div class="title-actions">
          {/* The window's single amber mark, and only when it means something. */}
          <span class="t-label stale">{live() ? '' : 'no signal'}</span>
          {/* No label beside it: the option names the band and the hint bar
              below already prints the numbers. */}
          <Select options={RANGES.map((r) => r.name)} value={range()} onChange={chooseRange} />
          <Button on={paused()} onClick={togglePause}>Pause</Button>
        </div>
      </div>

      <div class="display">
        <div class="scale">
          {marksFor(axis(), PICTURE_H).map((m) => (
            <div class="scale-mark t-label" style={{ top: `${m.y}px` }}>
              <span>{m.label}</span>
              <i class="scale-tick" />
            </div>
          ))}
        </div>
        <div class="well">
          <Spectrogram
            width={PICTURE_W}
            height={PICTURE_H}
            cols={PICTURE_W}
            batch={batch()}
            scale={scale()}
            paused={paused()}
            generation={generation()}
          />
        </div>
      </div>

      <Hint clauses={hint()} />
    </main>
  );
}
