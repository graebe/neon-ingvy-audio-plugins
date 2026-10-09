// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Every number and figure in the paper's section 4, measured here:
 *
 *   cargo run --release -p ni-paper-subsynth --example figures
 *
 * writes the SVGs and results.md into ../figures. The SVGs are static
 * (the paper is Markdown), carry their own light and dark colours, and every
 * value they plot is also in results.md as a table.
 */

use std::fmt::Write as _;
use std::fs;

use ni_paper_subsynth::filter::{ladder::Ladder, svf::Svf};
use ni_paper_subsynth::measure::{alias, note_hz, power_spectrum, render};
use ni_paper_subsynth::osc::{dpw::DpwSaw, naive::NaiveSaw, polyblep::PolyBlepSaw, wavetable::*, Osc};
use ni_paper_subsynth::oversample::{Down, Oversampled};
use ni_paper_subsynth::shaper::{Adaa, HardClip, Shape, Tanh};

const FS: f64 = 48_000.0;
const N: usize = 1 << 17;
const SKIP: usize = 4096;
const AUDIBLE: f64 = 20_000.0;
const OUT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../figures");

/* ----------------------------------------------------------- the sources */

/// The tables section 4 compares: one level per octave and three, at 2048
/// samples a frame, and one level per octave at 8192.
struct Tables {
    octave: Wavetable,
    third: Wavetable,
    octave_8k: Wavetable,
}

/// The oscillators section 4 compares, by name.
fn oscillator(name: &str, f0: f32, tables: &Tables) -> Vec<f32> {
    let fs = FS as f32;
    let table = |name: &str, ceiling: f32| -> Vec<f32> {
        let t = if name.contains("8k") {
            &tables.octave_8k
        } else if name.contains("1/oct") {
            &tables.octave
        } else {
            &tables.third
        };
        render({ let mut o = WavetableOsc::new(t, f0, fs, ceiling); move || o.next() }, SKIP, N)
    };
    match name {
        "Naive" => render({ let mut o = NaiveSaw::new(f0, fs); move || o.next() }, SKIP, N),
        "PolyBLEP" => render({ let mut o = PolyBlepSaw::new(f0, fs); move || o.next() }, SKIP, N),
        "DPW" => render({ let mut o = DpwSaw::new(f0, fs); move || o.next() }, SKIP, N),
        n if n.ends_with("strict") => table(n, fs / 2.0),
        n if n.ends_with("relaxed") => table(n, fs - AUDIBLE as f32),
        "PolyBLEP, 2x" => render(
            {
                let mut o = PolyBlepSaw::new(f0, 2.0 * fs);
                let mut down = Down::default();
                move || down.tick([o.next(), o.next()])
            },
            SKIP,
            N,
        ),
        _ => unreachable!("{name}"),
    }
}

/// A sine at `f0` through a shaper, with `drive`.
fn shaped(name: &str, f0: f64, drive: f32) -> Vec<f32> {
    let w = 2.0 * core::f64::consts::PI * f0 / FS;
    let mut n = 0usize;
    let mut sine = move || {
        n += 1;
        drive * (w * n as f64).sin() as f32
    };
    match name {
        "tanh" => render(move || Tanh::f(sine()), SKIP, N),
        "tanh, ADAA" => render({ let mut s = Adaa::<Tanh>::default(); move || s.tick(sine()) }, SKIP, N),
        "tanh, 2x" => render({ let mut s = Oversampled::new(Tanh::f); move || s.tick(sine()) }, SKIP, N),
        "tanh, ADAA + 2x" => render(
            {
                let mut a = Adaa::<Tanh>::default();
                let mut s = Oversampled::new(move |x| a.tick(x));
                move || s.tick(sine())
            },
            SKIP,
            N,
        ),
        "hard clip" => render(move || HardClip::f(sine()), SKIP, N),
        "hard clip, ADAA" => render({ let mut s = Adaa::<HardClip>::default(); move || s.tick(sine()) }, SKIP, N),
        "hard clip, 2x" => render({ let mut s = Oversampled::new(HardClip::f); move || s.tick(sine()) }, SKIP, N),
        "hard clip, ADAA + 2x" => render(
            {
                let mut a = Adaa::<HardClip>::default();
                let mut s = Oversampled::new(move |x| a.tick(x));
                move || s.tick(sine())
            },
            SKIP,
            N,
        ),
        _ => unreachable!("{name}"),
    }
}

/// |H| in dB on a log grid, from 8192 samples of the impulse response.
fn response(mut f: impl FnMut(f32) -> f32) -> Vec<(f64, f64)> {
    let n = 8192;
    let h: Vec<f32> = (0..n).map(|i| f(if i == 0 { 1.0 } else { 0.0 })).collect();
    /* No window: the response has decayed long before the end. */
    let mut planner = realfft_plan(n);
    let spec = planner(&h);
    (0..=200)
        .map(|i| {
            let hz = 20.0 * 1000f64.powf(i as f64 / 200.0); // 20 Hz .. 20 kHz
            let k = (hz / FS * n as f64).round() as usize;
            (hz, 10.0 * spec[k].max(1e-30).log10())
        })
        .collect()
}

/// An unwindowed power spectrum (the windowed one is for measuring, this is for drawing a response).
fn realfft_plan(n: usize) -> impl FnMut(&[f32]) -> Vec<f64> {
    move |x: &[f32]| {
        assert_eq!(x.len(), n);
        let mut planner = realfft::RealFftPlanner::<f64>::new();
        let fft = planner.plan_fft_forward(n);
        let mut buf: Vec<f64> = x.iter().map(|&v| v as f64).collect();
        let mut out = fft.make_output_vec();
        fft.process(&mut buf, &mut out).unwrap();
        out.iter().map(|c| c.norm_sqr()).collect()
    }
}

/// A spectrum for drawing: dB relative to its peak, the loudest bin per column.
fn spectrum_db(x: &[f32], columns: usize) -> Vec<(f64, f64)> {
    let p = power_spectrum(x);
    let peak = p.iter().cloned().fold(0.0, f64::max);
    let per = p.len() / columns;
    (0..columns)
        .map(|c| {
            let m = p[c * per..(c + 1) * per].iter().cloned().fold(0.0, f64::max);
            let hz = (c as f64 + 0.5) * per as f64 * FS / x.len() as f64;
            (hz, (10.0 * (m / peak).max(1e-20).log10()).max(-160.0))
        })
        .collect()
}

/* --------------------------------------------------------------- drawing */

struct Series {
    name: String,
    slot: usize, // categorical slot, 1..=3
    points: Vec<(f64, f64)>,
}

struct Panel {
    title: String,
    series: Vec<Series>,
}

struct Axes {
    x: (f64, f64),
    y: (f64, f64),
    log_x: bool,
    x_ticks: Vec<(f64, String)>,
    y_ticks: Vec<f64>,
    x_label: &'static str,
    y_label: &'static str,
}

const STYLE: &str = r#"<style>
svg{--surface:#fcfcfb;--text:#0b0b0b;--text2:#52514e;--muted:#898781;--grid:#e1e0d9;--axis:#c3c2b7;--s1:#2a78d6;--s2:#eb6834;--s3:#1baf7a}
@media (prefers-color-scheme:dark){svg{--surface:#1a1a19;--text:#ffffff;--text2:#c3c2b7;--muted:#898781;--grid:#2c2c2a;--axis:#383835;--s1:#3987e5;--s2:#d95926;--s3:#199e70}}
text{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;fill:var(--text2);font-size:12px}
.t{fill:var(--text);font-size:13px;font-weight:600}.m{fill:var(--muted);font-size:11px}
.g{stroke:var(--grid);stroke-width:1}.a{stroke:var(--axis);stroke-width:1}
.l{fill:none;stroke-width:2;stroke-linejoin:round;stroke-linecap:round}
.s1{stroke:var(--s1)}.s2{stroke:var(--s2)}.s3{stroke:var(--s3)}
.f1{fill:var(--s1)}.f2{fill:var(--s2)}.f3{fill:var(--s3)}
</style>"#;

fn chart(file: &str, title: &str, panels: &[Panel], ax: &Axes) {
    let (w, ph) = (760.0, 220.0);
    let (left, right, top, gap) = (64.0, 150.0, 44.0, 48.0);
    let h = top + panels.len() as f64 * (ph + gap) + 10.0;
    let pw = w - left - right;
    let mut s = String::new();
    let _ = write!(s, r#"<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" role="img" aria-label="{title}">{STYLE}"#);
    let _ = write!(s, r#"<rect width="{w}" height="{h}" fill="var(--surface)"/><text class="t" x="{left}" y="22">{title}</text>"#);
    let fx = |x: f64| {
        let t = if ax.log_x { (x.ln() - ax.x.0.ln()) / (ax.x.1.ln() - ax.x.0.ln()) } else { (x - ax.x.0) / (ax.x.1 - ax.x.0) };
        left + t.clamp(0.0, 1.0) * pw
    };
    for (pi, panel) in panels.iter().enumerate() {
        let y0 = top + pi as f64 * (ph + gap) + 18.0;
        let fy = |y: f64| y0 + (1.0 - ((y - ax.y.0) / (ax.y.1 - ax.y.0)).clamp(0.0, 1.0)) * ph;
        let _ = write!(s, r#"<text x="{left}" y="{}">{}</text>"#, y0 - 8.0, panel.title);
        for &t in &ax.y_ticks {
            let y = fy(t);
            let _ = write!(s, r#"<line class="g" x1="{left}" x2="{}" y1="{y:.1}" y2="{y:.1}"/><text class="m" x="{}" y="{:.1}" text-anchor="end">{t}</text>"#, left + pw, left - 6.0, y + 4.0);
        }
        for (t, label) in &ax.x_ticks {
            let x = fx(*t);
            let _ = write!(s, r#"<line class="g" x1="{x:.1}" x2="{x:.1}" y1="{y0}" y2="{}"/><text class="m" x="{x:.1}" y="{}" text-anchor="middle">{label}</text>"#, y0 + ph, y0 + ph + 15.0);
        }
        let _ = write!(s, r#"<line class="a" x1="{left}" x2="{}" y1="{}" y2="{}"/>"#, left + pw, y0 + ph, y0 + ph);
        let _ = write!(s, r#"<text class="m" x="14" y="{}" transform="rotate(-90 14 {})" text-anchor="middle">{}</text>"#, y0 + ph / 2.0, y0 + ph / 2.0, ax.y_label);
        /* The legend, in the right margin, in series order: a line key, then
         * the name in text ink. One series needs none: the panel's title names it. */
        for (si, ser) in panel.series.iter().enumerate().filter(|_| panel.series.len() > 1) {
            let ly = y0 + 10.0 + si as f64 * 20.0;
            let lx = left + pw + 16.0;
            let _ = write!(s, r#"<line class="l s{}" x1="{lx}" x2="{}" y1="{ly}" y2="{ly}"/><text x="{}" y="{}">{}</text>"#, ser.slot, lx + 18.0, lx + 24.0, ly + 4.0, ser.name);
        }
        for ser in &panel.series {
            let mut d = String::new();
            for (i, &(x, y)) in ser.points.iter().enumerate() {
                let _ = write!(d, "{}{:.1},{:.1}", if i == 0 { "M" } else { "L" }, fx(x), fy(y));
            }
            let _ = write!(s, r#"<path class="l s{}" d="{d}"/>"#, ser.slot);
        }
    }
    let _ = write!(s, r#"<text class="m" x="{}" y="{}" text-anchor="middle">{}</text></svg>"#, left + pw / 2.0, h - 4.0, ax.x_label);
    fs::write(format!("{OUT}/{file}"), s).unwrap();
}

fn khz_ticks(max: f64, step: f64) -> Vec<(f64, String)> {
    (0..=(max / step) as usize).map(|i| (i as f64 * step, format!("{}k", i as f64 * step / 1000.0))).collect()
}

/* ------------------------------------------------------------------ main */

fn main() {
    fs::create_dir_all(OUT).unwrap();
    let table = Tables {
        octave: Wavetable::new(&[saw_frame(2048)], 1),
        third: Wavetable::new(&[saw_frame(2048)], 3),
        octave_8k: Wavetable::new(&[saw_frame(8192)], 1),
    };
    let mut md = String::from("<!-- Written by `cargo run --release -p ni-paper-subsynth --example figures`. Do not edit. -->\n\n");

    /* ---- Table 1: the oscillators. */
    let oscs = [
        "Naive", "PolyBLEP", "DPW", "PolyBLEP, 2x",
        "Table 1/oct, strict", "Table 1/oct, relaxed", "Table 3/oct, strict", "Table 3/oct, relaxed",
        "Table 1/oct 8k, strict",
    ];
    let notes = [48, 72, 84, 96, 108];
    let _ = writeln!(md, "### Table 1 -- sawtooth signal-to-alias ratio, dB (fs = 48 kHz)\n");
    let _ = writeln!(md, "Each cell: whole band to Nyquist / audible band to 20 kHz. Higher is cleaner.\n");
    let _ = write!(md, "| Oscillator |");
    for n in notes {
        let _ = write!(md, " MIDI {n} ({:.0} Hz) |", note_hz(n));
    }
    let _ = writeln!(md, "\n|---|{}", "---:|".repeat(notes.len()));
    for o in oscs {
        let _ = write!(md, "| {o} |");
        for n in notes {
            let f0 = note_hz(n);
            let x = oscillator(o, f0 as f32, &table);
            let full = alias(&x, f0, FS, FS / 2.0);
            let aud = alias(&x, f0, FS, AUDIBLE);
            let _ = write!(md, " {:.1} / {:.1} |", full.asr_db, aud.asr_db);
        }
        let _ = writeln!(md);
    }

    /* ---- Table 2: brightness the strict wavetable gives up. */
    let _ = writeln!(md, "\n### Table 2 -- highest harmonic kept by the wavetable, Hz\n");
    let _ = writeln!(md, "Strict: ceiling 24 kHz. Relaxed: ceiling 28 kHz, so nothing folds below 20 kHz.\n");
    let _ = writeln!(md, "| MIDI note | f0 (Hz) | 1/oct, strict | 1/oct, relaxed | 3/oct, strict | 3/oct, relaxed | below Nyquist |\n|---:|---:|---:|---:|---:|---:|---:|");
    for n in [60, 62, 64, 66, 68, 70, 72] {
        let f0 = note_hz(n);
        let top = |t: &Wavetable, ceiling: f64| t.harmonics(t.level_for(f0 as f32, ceiling as f32)) as f64 * f0;
        let ideal = (FS / 2.0 / f0).floor() * f0;
        let _ = writeln!(
            md,
            "| {n} | {f0:.1} | {:.0} | {:.0} | {:.0} | {:.0} | {ideal:.0} |",
            top(&table.octave, FS / 2.0),
            top(&table.octave, FS - AUDIBLE),
            top(&table.third, FS / 2.0),
            top(&table.third, FS - AUDIBLE)
        );
    }

    /* ---- Figure 1: one note's spectrum, naive against PolyBLEP. */
    let f0 = note_hz(96);
    let panels: Vec<Panel> = [("Naive", 2), ("PolyBLEP", 1)]
        .iter()
        .map(|&(name, slot)| Panel {
            title: format!("{name} sawtooth, C7 ({f0:.0} Hz)"),
            series: vec![Series { name: name.into(), slot, points: spectrum_db(&oscillator(name, f0 as f32, &table), 480) }],
        })
        .collect();
    chart(
        "fig1-spectrum-naive-polyblep.svg",
        "Figure 1. Aliasing is everything between the harmonics",
        &panels,
        &Axes {
            x: (0.0, FS / 2.0),
            y: (-140.0, 0.0),
            log_x: false,
            x_ticks: khz_ticks(24_000.0, 4_000.0),
            y_ticks: vec![-140.0, -120.0, -100.0, -80.0, -60.0, -40.0, -20.0, 0.0],
            x_label: "frequency (Hz)",
            y_label: "level (dB re peak)",
        },
    );

    /* ---- Figure 2: ASR across the keyboard, audible band. */
    let keys: Vec<i32> = (36..=108).step_by(3).collect();
    let curve = |name: &str| -> Vec<(f64, f64)> {
        keys.iter().map(|&n| (n as f64, alias(&oscillator(name, note_hz(n) as f32, &table), note_hz(n), FS, AUDIBLE).asr_db)).collect()
    };
    let panels = vec![
        Panel {
            title: "Corrected at the sample rate".into(),
            series: vec![
                Series { name: "Naive".into(), slot: 2, points: curve("Naive") },
                Series { name: "PolyBLEP".into(), slot: 1, points: curve("PolyBLEP") },
                Series { name: "DPW".into(), slot: 3, points: curve("DPW") },
            ],
        },
        Panel {
            title: "Oversampled, and band-limited by table".into(),
            series: vec![
                Series { name: "PolyBLEP, 2x".into(), slot: 1, points: curve("PolyBLEP, 2x") },
                Series { name: "Table 1/oct, strict".into(), slot: 2, points: curve("Table 1/oct, strict") },
                Series { name: "Table 3/oct, relaxed".into(), slot: 3, points: curve("Table 3/oct, relaxed") },
            ],
        },
    ];
    chart(
        "fig2-asr-keyboard.svg",
        "Figure 2. Signal-to-alias ratio below 20 kHz, across the keyboard",
        &panels,
        &Axes {
            x: (36.0, 108.0),
            y: (0.0, 140.0),
            log_x: false,
            x_ticks: [36, 48, 60, 72, 84, 96, 108].iter().map(|&n| (n as f64, format!("C{}", n / 12 - 1))).collect(),
            y_ticks: vec![0.0, 20.0, 40.0, 60.0, 80.0, 100.0, 120.0, 140.0],
            x_label: "note",
            y_label: "ASR (dB)",
        },
    );

    /* ---- Figure 3: the two filters' responses. */
    let fs = FS as f32;
    let svf = |q: f32| response({ let mut f = Svf::default(); f.set(1_000.0, q, fs); move |x| f.tick(x).lp });
    let ladder = |r: f32| response({ let mut f = Ladder::default(); f.set(1_000.0, r, fs); move |x| f.tick(x) });
    let panels = vec![
        Panel {
            title: "State-variable low-pass, fc = 1 kHz".into(),
            series: vec![
                Series { name: "Q 0.707".into(), slot: 1, points: svf(0.707) },
                Series { name: "Q 2".into(), slot: 2, points: svf(2.0) },
                Series { name: "Q 8".into(), slot: 3, points: svf(8.0) },
            ],
        },
        Panel {
            title: "Ladder, fc = 1 kHz".into(),
            series: vec![
                Series { name: "resonance 0".into(), slot: 1, points: ladder(0.0) },
                Series { name: "resonance 0.5".into(), slot: 2, points: ladder(0.5) },
                Series { name: "resonance 0.9".into(), slot: 3, points: ladder(0.9) },
            ],
        },
    ];
    chart(
        "fig3-filters.svg",
        "Figure 3. Magnitude responses of the two TPT filters",
        &panels,
        &Axes {
            x: (20.0, 20_000.0),
            y: (-96.0, 24.0),
            log_x: true,
            x_ticks: [20.0, 100.0, 1_000.0, 10_000.0, 20_000.0].iter().map(|&f| (f, if f >= 1000.0 { format!("{}k", f / 1000.0) } else { format!("{f}") })).collect(),
            y_ticks: vec![-96.0, -72.0, -48.0, -24.0, 0.0, 24.0],
            x_label: "frequency (Hz)",
            y_label: "gain (dB)",
        },
    );

    /* ---- Table 3 and Figure 4: the waveshapers. */
    let shapers = [
        "tanh", "tanh, ADAA", "tanh, 2x", "tanh, ADAA + 2x",
        "hard clip", "hard clip, ADAA", "hard clip, 2x", "hard clip, ADAA + 2x",
    ];
    let tones = [(note_hz(84), 4.0f32), (note_hz(96), 4.0), (note_hz(84), 16.0)];
    let _ = writeln!(md, "\n### Table 3 -- waveshaper signal-to-alias ratio, dB, audible band (fs = 48 kHz)\n");
    let _ = write!(md, "| Shaper |");
    for (f, d) in tones {
        let _ = write!(md, " {f:.0} Hz, drive {d} |");
    }
    let _ = writeln!(md, "\n|---|{}", "---:|".repeat(tones.len()));
    for s in shapers {
        let _ = write!(md, "| {s} |");
        for (f, d) in tones {
            let _ = write!(md, " {:.1} |", alias(&shaped(s, f, d), f, FS, AUDIBLE).asr_db);
        }
        let _ = writeln!(md);
    }
    let f = note_hz(96);
    let panels: Vec<Panel> = [("hard clip", 2), ("hard clip, ADAA", 1), ("hard clip, ADAA + 2x", 3)]
        .iter()
        .map(|&(name, slot)| Panel {
            title: format!("{name}, sine at {f:.0} Hz, drive 4"),
            series: vec![Series { name: name.into(), slot, points: spectrum_db(&shaped(name, f, 4.0), 480) }],
        })
        .collect();
    chart(
        "fig4-shaper-spectra.svg",
        "Figure 4. A hard clipper, plain, with ADAA, and with ADAA at 2x",
        &panels,
        &Axes {
            x: (0.0, FS / 2.0),
            y: (-140.0, 0.0),
            log_x: false,
            x_ticks: khz_ticks(24_000.0, 4_000.0),
            y_ticks: vec![-140.0, -120.0, -100.0, -80.0, -60.0, -40.0, -20.0, 0.0],
            x_label: "frequency (Hz)",
            y_label: "level (dB re peak)",
        },
    );

    fs::write(format!("{OUT}/results.md"), md).unwrap();
    println!("{}", fs::read_to_string(format!("{OUT}/results.md")).unwrap());
}
