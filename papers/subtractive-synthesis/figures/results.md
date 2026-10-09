<!-- Written by `cargo run --release -p ni-paper-subsynth --example figures`. Do not edit. -->

### Table 1 -- sawtooth signal-to-alias ratio, dB (fs = 48 kHz)

Each cell: whole band to Nyquist / audible band to 20 kHz. Higher is cleaner.

| Oscillator | MIDI 48 (131 Hz) | MIDI 72 (523 Hz) | MIDI 84 (1047 Hz) | MIDI 96 (2093 Hz) | MIDI 108 (4186 Hz) |
|---|---:|---:|---:|---:|---:|
| Naive | 24.8 / 25.9 | 18.7 / 19.8 | 15.6 / 16.7 | 12.5 / 13.6 | 9.1 / 10.1 |
| PolyBLEP | 40.9 / 46.2 | 34.5 / 39.9 | 31.1 / 36.4 | 28.4 / 33.9 | 23.7 / 29.1 |
| DPW | 34.9 / 38.2 | 28.7 / 32.1 | 25.4 / 28.8 | 22.6 / 26.0 | 18.4 / 21.7 |
| PolyBLEP, 2x | 50.2 / 67.1 | 42.3 / 61.2 | 37.3 / 57.9 | 43.9 / 55.3 | 38.1 / 53.3 |
| Table 1/oct, strict | 72.7 / 73.1 | 90.6 / 91.4 | 99.3 / 99.9 | 107.8 / 108.8 | 115.8 / 116.3 |
| Table 1/oct, relaxed | 72.7 / 73.1 | 90.6 / 91.4 | 99.3 / 99.9 | 107.8 / 108.8 | 115.8 / 116.3 |
| Table 3/oct, strict | 70.4 / 70.6 | 87.7 / 88.1 | 96.5 / 96.9 | 105.1 / 105.6 | 113.3 / 113.5 |
| Table 3/oct, relaxed | 35.1 / 67.6 | 28.7 / 85.1 | 24.9 / 93.9 | 23.5 / 103.1 | 17.2 / 111.2 |
| Table 1/oct 8k, strict | 96.8 / 97.7 | 114.6 / 115.7 | 122.8 / 123.4 | 129.1 / 130.7 | 133.6 / 134.4 |

### Table 2 -- highest harmonic kept by the wavetable, Hz

Strict: ceiling 24 kHz. Relaxed: ceiling 28 kHz, so nothing folds below 20 kHz.

| MIDI note | f0 (Hz) | 1/oct, strict | 1/oct, relaxed | 3/oct, strict | 3/oct, relaxed | below Nyquist |
|---:|---:|---:|---:|---:|---:|---:|
| 60 | 261.6 | 16744 | 16744 | 20930 | 26424 | 23808 |
| 62 | 293.7 | 18795 | 18795 | 23493 | 23493 | 23787 |
| 64 | 329.6 | 21096 | 21096 | 21096 | 26370 | 23733 |
| 66 | 370.0 | 23680 | 23680 | 23680 | 23680 | 23680 |
| 68 | 415.3 | 13290 | 26580 | 20765 | 26580 | 23672 |
| 70 | 466.2 | 14917 | 14917 | 23308 | 23308 | 23774 |
| 72 | 523.3 | 16744 | 16744 | 20930 | 26163 | 23546 |

### Table 3 -- waveshaper signal-to-alias ratio, dB, audible band (fs = 48 kHz)

| Shaper | 1047 Hz, drive 4 | 2093 Hz, drive 4 | 1047 Hz, drive 16 |
|---|---:|---:|---:|
| tanh | 91.7 | 51.7 | 33.0 |
| tanh, ADAA | 97.8 | 60.0 | 40.5 |
| tanh, 2x | 139.8 | 124.7 | 72.2 |
| tanh, ADAA + 2x | 139.8 | 134.6 | 86.5 |
| hard clip | 41.8 | 33.5 | 28.7 |
| hard clip, ADAA | 50.6 | 44.2 | 36.4 |
| hard clip, 2x | 56.7 | 48.0 | 43.4 |
| hard clip, ADAA + 2x | 75.3 | 68.2 | 59.2 |
