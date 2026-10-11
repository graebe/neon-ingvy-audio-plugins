---
section: live
title: Ableton Live Interface
---

The window is two pictures over three panels. The pictures show what the
crusher does; the panels are what you do to it.

- **Steps**, on the left, is the staircase: every input level across, and
  the level it becomes up. The grey diagonal is the untouched signal.
- **Signal**, on the right, is the sound itself, the last eleven
  milliseconds of it: the input in grey, the crushed output in front.
- Under them, three panels: **Steps** (how finely a level is kept), **Hold**
  (how often a level is taken) and **Level** (how hard it is driven in, and
  what comes out).

## Part 1 — keeping fewer levels

### What a bit is

Digital audio stores each moment of a wave as a number. With 16 bits there
are 65 536 possible numbers, so a wave has 65 536 levels to land on and the
steps between them are far too small to hear. Take bits away and the levels
get fewer and the steps bigger: with 8 bits there are 256, with 4 bits 16,
with 2 bits only 4.

Set **Bits** to 4 and watch the Steps picture. The grey diagonal behind is
the untouched signal: output equals input. The bright line in front is what
the crusher does to it: a staircase. Every input between two steps comes out
as the nearest step. The Signal picture shows the same thing in time: the
grey wave that came in becomes a stack of flat stairs.

> **The Steps picture zooms in.** At 8 bits there are 256 steps from bottom
> to top, each far narrower than a pixel, so drawn whole they would look like
> a plain diagonal. The picture shows the part around silence where about 16
> steps fit, and its caption says by how much: `STEPS 8.0 BITS ×16` is a
> sixteenth of full scale. From 4 bits down every step fits, and the zoom
> disappears. Drive and Shape move the zoom too, because they change how
> wide the steps are.

> **Bits is continuous.** 4.5 bits is a real setting, between 16 and 32
> levels, and the top step is simply shorter. So a slow sweep of Bits — or an
> automation lane on it — glides down the staircase rather than clicking from
> one count to the next.

What you hear is **quantisation error**: the difference between the wave and
its stairs. At high bit depths it is a faint hiss; as the bits drop it grows
into a gritty, distorted edge that follows the signal — loud where the music
is loud, gone where it is silent.

### Drive: where the signal sits on the stairs

A quiet signal only ever reaches the bottom few steps, so it sounds more
crushed than a loud one. **Drive** pushes it up the staircase before it is
crushed — and past the top step, it clips. Turn Drive up and the Steps picture
tilts steeper: the same input reaches further. Mix and Output bring the level
back down afterwards.

### Shape: steps where they matter

Even steps waste most of their resolution on loud signals. **Shape** bends the
grid toward µ-law — the curve telephones use — so the steps are fine near
silence and coarse near full scale. At 100 % a quiet tail keeps its detail at
the same Bits that turns it to silence at 0 %. Watch the Steps picture: the
stairs bunch up around the middle.

### Dither: noise instead of grit

**Dither** adds a little noise — up to one step, triangular — before the
staircase. It sounds backwards, but it works: the noise moves the signal
across the step edges at random, so on average the output follows the input
even below a single step. The gritty distortion becomes an even hiss, and a
fade-out fades out instead of stopping dead. Leave it at 0 for the classic
crunch; turn it up for a cleaner, tape-like low-fi.

### Mid-Rise: no step at zero

Normally there is a step exactly at zero, so silence stays silent (this is
called **mid-tread**). Switch on **Mid-Rise** and the grid moves half a step:
there is no zero any more, and the quietest signal flips between two steps
either side of it. At 1 bit this turns any sound into a square wave — pure
sign. Silence becomes a half-step offset, which the plugin's DC blocker takes
out over a fraction of a second, so the output never sits off-centre.

### Anti-Alias: the crusher, drawn exactly

A digital crusher works on your project's sample grid: it can only take a
level, or jump to a new one, on one of the 44 100 or 48 000 sample ticks a
second. Its staircase has sharp corners, and sharp corners mean overtones far
above what the sample rate can hold; those fold back down as **aliasing** —
inharmonic tones that do not belong to the note. Worse, the grid itself is in
the sound: 12 kHz at 44.1 kHz holds for three samples and four by turns, which
a 48 kHz project does not do, so the same settings sound different in two
projects.

**Anti-Alias** draws the crusher as if it ran in continuous time and you
listened through your project's sample rate. Each level is taken at the exact
instant the Rate says, read between the samples; each jump is drawn as a
smooth, band-limited step at that instant. What a hold *should* add — the
mirror images of the tone around every multiple of the Rate, the sound of the
effect — is all there; what your project's grid added is gone, and nothing
folds back past its Nyquist. The same settings sound the same at 44.1, 48 or
96 kHz.

Off is the raw digital crusher, grid and all — harsher, and the classic. On
is the "ideal converter at Rate". The top octave is kept: within a decibel to
15 kHz at 44.1 kHz.

> **Latency.** Drawing a step smoothly needs a few samples before and after
> it, so the plugin runs 32 samples late (0.7 ms at 44.1 kHz) — always, with
> Anti-Alias on or off, so switching it never moves anything in time. Live
> compensates for it automatically.

## Part 2 — keeping them less often

### Rate: the sample-and-hold

The second half of crushing is in time. **Rate** sets how many times a second
the crusher takes the signal's level; between takes, it *holds* the last one.
The Signal picture shows it as flat tops: at 4 kHz each level is held for
twelve samples at 48 kHz.

Anything in the signal above **half the Rate** cannot be held faithfully and
folds back down as aliasing — at a low Rate, a hi-hat becomes a metallic
whine. That is the character of the effect, and it is why Rate is the most
dramatic control here. Rate does not have to divide your project's rate: any
value works, and the hold's clock never drifts.

> **Rate turns in equal ratios.** The knob spends as much of its turn between
> 500 Hz and 1 kHz as between 10 and 20 kHz, so every part of the range is
> reachable. Type a value straight into the readout: `8000`, `8k` and `8 kHz`
> are all 8 kHz.

At or above your project's sample rate, nothing is held: Rate is out of the
way and only the Steps panel is heard.

### Jitter: an unsteady clock

**Jitter** lets each hold last a little longer or shorter, at random. The
steps in time become irregular: noisier, rougher, less like a clean tone. Each
channel's clock wanders on its own, so jitter also spreads a mono sound across
the stereo field. At 0 the two channels hold in lockstep.

### Pre and Post: the two filters

Two switches in the Hold panel put a low-pass filter on either side of the
hold:

- **Pre** filters *before* the hold. Everything from half the Rate up — what
  would fold back — comes out at least 60 dB down, so the result is
  band-limited and smooth: the sound of a real, well-made old sampler. The
  filter is steep, so it starts a little below half the Rate (it is 3 dB down
  at about three quarters of it). It also tames the stereo width Jitter adds.
- **Post** filters *after* the hold, at half the Rate moved by **Octave**. It
  rounds off the hold's flat tops — the sound of the reconstruction filter on
  an old converter. Octave moves it from three octaves below (dark, muffled)
  to three octaves above (open, edgy).

Both off is the raw, aliasing crusher; both on is the polite one. With
Anti-Alias as well, it is the polite one at any sample rate.

## Recipes

| sound | settings |
|---|---|
| **8-bit console** | Bits 4–6, Rate 8–11 kHz, everything else off. Add Mid-Rise at 1–2 bits for square-wave leads. |
| **Telephone** | Bits 8, Shape 100 %, Rate 8 kHz, Pre and Post on, Octave 0. Then cut the lows with an EQ after it. |
| **12-bit sampler** | Bits 12, Rate 26–32 kHz, Pre and Post on, Octave +1, Dither 30 %. Warm, not broken. |
| **Broken radio** | Bits 3, Drive +12 dB, Rate 2–4 kHz, Jitter 60 %, Mix 70 %. |
| **Stutter sweep** | Automate Rate from 20 kHz down to 300 Hz over a bar; Anti-Alias on, so the sweep stays musical. |
| **Parallel crunch on drums** | Bits 5, Rate 12 kHz, Drive +6 dB, Mix 30 %: the transients keep their punch, the crunch sits under them. |

## Controls

Every knob reads in its own unit and takes what you type: click the readout,
type, Enter. **Double-click** a knob to reset it; hold **Shift** while dragging
for fine control. Drive, Octave and Output run both ways from zero, so their
arcs start at the top.

| panel | control | range | default |
|---|---|---|---|
| Steps | **Bits** | 1 – 16 bits | 8.0 |
| | **Shape** | 0 – 100 % | 0 % |
| | **Dither** | 0 – 100 % | 0 % |
| | **Mid-Rise** | off / on | off |
| | **Anti-Alias** | off / on | off |
| Hold | **Rate** | 50 Hz – 96 kHz | 12 kHz |
| | **Jitter** | 0 – 100 % | 0 % |
| | **Octave** | −3 – +3 oct | 0 |
| | **Pre** (Pre-Filter in Live's list) | off / on | off |
| | **Post** (Post-Filter in Live's list) | off / on | off |
| Level | **Drive** | −24 – +24 dB | 0 dB |
| | **Mix** | 0 – 100 % | 100 % |
| | **Output** | −24 – +24 dB | 0 dB |

The **Signal** picture zooms to the level of what is playing, so a quiet track
still fills it; its top right says by how much. Double-click it to hold it at
full scale. The **Motion** switch in the bar at the bottom stops the
background rippling on the beat; it is remembered on this computer.

When Live switches the device off and on again, or bypasses it, the crusher
fades out to the plain signal and back in from silence: nothing it held
before is heard again, and nothing clicks. Automation is followed smoothly
whatever Live's buffer size.
