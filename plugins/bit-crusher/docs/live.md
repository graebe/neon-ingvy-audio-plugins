---
section: live
title: Ableton Live Interface
---

A bit crusher makes a continuous signal discrete twice over: in **amplitude**
(it keeps fewer levels) and in **time** (it keeps them less often). Those are
the plugin's two halves, **Amplitude Crush** and **Time Crush**, and the
window is built around them: four pictures over four panels, each picture
over the panel that shapes it.

- **Amplitude Crush**, on the left, is the staircase: every input level
  across, and the level it becomes up. The grey diagonal is the untouched
  signal. While something plays, the staircase dims to a map and your signal
  lights it up as dots: the stairs it is on right now.
- **Time Crush**, in the middle, is the hold: your signal in grey, the held
  level as a dim line — flat between takes — and each take lit as a dot.
- **Signal**, third, is the sound itself, the last eleven milliseconds of
  it: the input in grey, the crushed output in front.
- **Filter**, on the right, is the low-pass after the crusher: how loud each
  frequency comes out, from 20 Hz to 20 kHz. The grey line is 0 dB, the
  signal untouched; the curve lights up while the Filter is on. Drag the
  dot on it to set Cutoff and Peak.
- Every picture has rulers: level in full scale up the left (and across, in
  Amplitude Crush), time in milliseconds across the bottom. The two
  discretizations also put their grid on the axes as small ticks — a tick at
  every step of the staircase, both ways, and at every take of the hold — so
  you can count the steps and the takes.
- Under them, four panels: **Amplitude Crush** (how finely a level is kept),
  **Time Crush** (how often a level is taken), **Level** (how hard it is
  driven in, and what comes out) and **Filter** (what the crushed sound keeps
  of its top end).

### Refresh and Pause

The three signal pictures take a new look at your signal once a second by
default, all three at the same moment, so they can be read rather than chased.
**Refresh**, at the bottom of the Level panel, sets how often: **1/s**, **4/s**,
**10/s** or **Live** (every frame the screen draws). It is saved with your set,
and Live does not offer it for automation: it changes the pictures, never the
sound. **Pause**, beside it, holds all three pictures still while the audio
goes on; press it again to let them go. The staircase itself still follows the
knobs while paused — it is your settings, not your signal.

## Part 1 — Amplitude Crush: keeping fewer levels

### What a bit is

Digital audio stores each moment of a wave as a number. With 16 bits there
are 65 536 possible numbers, so a wave has 65 536 levels to land on and the
steps between them are far too small to hear. Take bits away and the steps
get fewer and bigger: 2 to the power of Bits of them from bottom to top — 256
at 8 bits, 16 at 4 bits, 4 at 2 bits.

> **Steps and levels.** The crusher keeps a level at zero, so silence stays
> silent, and both ends of the range: one more level than steps. At 8 bits
> that is **257 levels**, at 2 bits 5 (−1, −½, 0, +½, +1), at 1 bit 3 (−1, 0,
> +1). With **Mid-Rise** (below) there is no level at zero and exactly 2 to
> the power of Bits levels: 256, 4, 2 — what a real converter of that many
> bits has. The Amplitude Crush picture's caption counts them for you:
> `257 LEVELS`.

This is **amplitude discretization**. Set **Bits** to 4 and watch the
Amplitude Crush picture. The grey diagonal behind is the untouched signal:
output equals input. The bright line in front is what the crusher does to it:
a staircase. Every input between two steps comes out as the nearest step.
While audio plays, the staircase dims and the samples of your signal light it
up as dots, each where its level in and its level out put it — all on the
stairs. The dots always belong to the staircase drawn: turn a knob and they
disappear until the signal has passed through the new settings, a moment
later (or, paused, until you let the pictures go). The Signal picture shows
the same thing in time:
the grey wave that came in becomes a stack of flat stairs.

> **The Amplitude Crush picture is always the whole range**, −1 to 1, so you
> see the staircase exactly as coarse as it is. At a few bits the steps are
> plain, and the axes tick each one. At 8 bits there are 256 of them, each
> narrower than a pixel, and the staircase looks like the diagonal — which is
> also roughly how it sounds: fine enough that you hear a faint grain, not
> stairs. The caption still counts the levels (`257 LEVELS`), and the axes
> stop ticking steps once they are too close to tell apart. The grey diagonal
> runs corner to corner: both axes are the level going into the crusher, on
> the same scale.

> **Bits is continuous.** 4.5 bits is a real setting, between 16 and 32
> steps, and the top step is simply shorter. So a slow sweep of Bits — or an
> automation lane on it — glides down the staircase rather than clicking from
> one count to the next.

What you hear is **quantisation error**: the difference between the wave and
its stairs. At high bit depths it is a faint hiss; as the bits drop it grows
into a gritty, distorted edge that follows the signal — loud where the music
is loud, gone where it is silent.

### Drive: where the signal sits on the stairs

A quiet signal only ever reaches the bottom few steps, so it sounds more
crushed than a loud one. **Drive** pushes it up the staircase before it is
crushed — and past the top step, it clips. The Amplitude Crush picture is
drawn on the level going *into* the crusher, after Drive, so the staircase
stays on the diagonal and Drive moves your signal instead: turn it up and the
dots spread further up the stairs, and the loudest ones gather on the top and
bottom steps — the clip. Mix and Output bring the level back down afterwards.

### Shape: steps where they matter

Even steps waste most of their resolution on loud signals. **Shape** bends the
grid toward µ-law — the curve telephones use — so the steps are fine near
silence and coarse near full scale. At 100 % a quiet tail keeps its detail at
the same Bits that turns it to silence at 0 %. Watch the Amplitude Crush picture:
the stairs bunch up around the middle.

### Dither: noise instead of grit

**Dither** adds a little noise — up to one step, triangular — before the
staircase. It sounds backwards, but it works: the noise moves the signal
across the step edges at random, so on average the output follows the input
even below a single step. The gritty distortion becomes an even hiss, and a
fade-out fades out instead of stopping dead. Leave it at 0 for the classic
crunch; turn it up for a cleaner, tape-like low-fi. In the Amplitude Crush
picture, Dither is the dots leaving the stairs: each sample lands a step above
or below where the staircase would put it, at random.

### Mid-Rise: no step at zero

Normally there is a step exactly at zero, so silence stays silent (this is
called **mid-tread**). Switch on **Mid-Rise** and the grid moves half a step:
there is no zero any more, and the quietest signal flips between two steps
either side of it. It also has exactly 2 to the power of Bits levels, one
fewer than mid-tread (the caption goes from `257 LEVELS` to `256 LEVELS`). At
1 bit this turns any sound into a square wave — pure sign, two levels. Silence
becomes a half-step offset, which the plugin's DC blocker takes out over a
fraction of a second, so the output never sits off-centre.

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

## Part 2 — Time Crush: keeping them less often

### Rate: the sample-and-hold

The second half of crushing is **time discretization**. **Rate** sets how many
times a second the crusher takes the signal's level; between takes, it *holds*
the last one. The Time Crush picture shows exactly that: your signal in grey,
the held level as a dim line, and each take lit as a dot. With Anti-Alias on,
the takes are drawn at their exact instants, between samples, as you hear
them. The grey is what the hold takes: your signal after Drive and, with Pre
on, after the pre-filter. It shows sixteen
holds — under a millisecond at a high Rate, tens of milliseconds at a low one,
and as many as fit in the last forty milliseconds or so at the very lowest —
and its caption names the Rate (`TIME CRUSH 12.0 KHZ 16 HOLDS`). It shows the
hold on its own, before the amplitude steps. The Signal picture shows both
together as flat tops: at 4 kHz each level is held for twelve samples at
48 kHz.

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
way and only the Amplitude Crush panel is heard.

### Jitter: an unsteady clock

**Jitter** lets each hold last a little longer or shorter, at random. The
steps in time become irregular: noisier, rougher, less like a clean tone. In
the Time Crush picture the dots space out unevenly and the treads change
length. Each
channel's clock wanders on its own, so jitter also spreads a mono sound across
the stereo field. At 0 the two channels hold in lockstep.

### Pre and Post: the two filters

Two switches in the Time Crush panel put a low-pass filter on either side of
the hold:

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

## Part 3 — Filter: taming the top

A crusher's steps are made of high frequencies: every jump of the staircase,
every edge of the hold, is a burst of them. Pre and Post filter at the Rate,
because they belong to the hold. The **Filter** panel is a low-pass of your
own, anywhere you like, on the crushed sound before it meets the dry one at
Mix — so a parallel crunch can be dark while the dry signal stays bright.

It is **off by default**: switch **Filter** on under its knobs. The Filter
picture lights up, and what it draws is what you hear.

**Drag the dot.** The white dot on the curve sits at the cutoff. Drag it
sideways to move **Cutoff**, up and down to set **Peak** — both at once, as
one move in Live's undo and automation, the same as turning the two knobs.
Hold **Shift** for fine control; **double-click** the dot to put both back to
their defaults. It works while the Filter is off too, so you can shape the
curve before you switch it on. With the dot selected, the arrow keys move it:
left and right for Cutoff, up and down for Peak.

### Cutoff: where it starts

**Cutoff**, 20 Hz to 20 kHz, is where the filter starts to cut: with Peak at
0 the sound is 3 dB down there, and falls away above it. Turn it down to
bury the grit under a warm, muffled tone; sweep it with automation for the
classic filter rise. Type `800` or `1.2k` into its readout to set it exactly.

### Slope: how steeply

**Slope** sets how fast the sound falls away above the cutoff: **12 dB**,
**24 dB** or **48 dB** per octave. 12 dB is gentle — the steps soften but
stay audible; 48 dB is a wall — above the cutoff, they are gone. In the
picture the curve's right-hand side tilts steeper with each. The slope is
the same wherever the cutoff sits, right up to 20 kHz: the filter is built to
keep the shape of its analog original near the top of the range, where
digital filters usually fall away ever faster. Switching it
while playing crossfades rather than clicks.

### Peak: a resonant bump

**Peak** lifts the sound right at the cutoff, up to **+24 dB**: at 0 the
filter is flat up to where it bends, at +12 dB it rings, at +24 dB it sings
the cutoff's note. The picture shows the bump growing, and its height is
exactly what you set. It never runs away into a self-oscillating whistle, so
it is safe to automate. Higher Peak means more level at the cutoff, so watch
Output when you push it.

### Recipes with the filter

- **Lo-fi drums, warm:** Bits 6, Rate 11 kHz, Filter on at 4 kHz, 24 dB, Peak 0.
- **Acid crunch:** Bits 4, Drive +9 dB, Filter on at 800 Hz, 24 dB, Peak +15 dB;
  automate Cutoff from 300 Hz to 5 kHz.
- **Radio through a wall:** Bits 8, Rate 8 kHz, Filter on at 1.5 kHz, 48 dB.

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
| Amplitude Crush | **Bits** | 1 – 16 bits | 8.0 |
| | **Shape** | 0 – 100 % | 0 % |
| | **Dither** | 0 – 100 % | 0 % |
| | **Mid-Rise** | off / on | off |
| | **Anti-Alias** | off / on | off |
| Time Crush | **Rate** | 50 Hz – 96 kHz | 12 kHz |
| | **Jitter** | 0 – 100 % | 0 % |
| | **Octave** | −3 – +3 oct | 0 |
| | **Pre** (Pre-Filter in Live's list) | off / on | off |
| | **Post** (Post-Filter in Live's list) | off / on | off |
| Level | **Drive** | −24 – +24 dB | 0 dB |
| | **Mix** | 0 – 100 % | 100 % |
| | **Output** | −24 – +24 dB | 0 dB |
| | **Refresh** (not automatable) | Live, 10/s, 4/s, 1/s | 1/s |
| | **Pause** (not saved) | off / on | off |
| Filter | **Filter** | off / on | off |
| | **Cutoff** | 20 Hz – 20 kHz | 2.00 kHz |
| | **Peak** | 0 – +24 dB | 0 dB |
| | **Slope** | 12, 24, 48 dB/oct | 24 dB |

The **Signal** and **Time Crush** pictures zoom to the level of what is
playing, so a quiet track still fills them; their top right says by how much.
Double-click either to hold it at full scale.

When Live switches the device off and on again, or bypasses it, the crusher
fades out to the plain signal and back in from silence: nothing it held
before is heard again, and nothing clicks. Automation is followed smoothly
whatever Live's buffer size.
