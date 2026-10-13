---
section: live
title: Ableton Live Interface
---

The window is two pictures over three panels. The pictures show where the
vowel is and what it does to your sound; the panels are what you do to it.

- **Vowel**, on the left, is the vowel space: the five vowels, the path between
  them, and a dot where the vowel is now.
- **Envelope**, on the right, is your sound's colour as it comes in, and as it
  goes out.
- Under them, three panels: **Vowel** (which vowel, and whose voice sings
  it), **Vocal** (how far, and how) and **Level** (what comes out).

## Part 1 — what makes a vowel

### A buzz and a mouth

Sing "aaa", then "eee", on the same note. The note does not change — your
vocal folds buzz at the same pitch — but the sound does. What changes is the
shape of your mouth and throat. That space resonates: it lifts some
frequencies and lets others fall away, and it lifts different ones for every
vowel.

Those lifted regions are called **formants**. The first two, **F1** and
**F2**, are enough to tell the five vowels apart:

| vowel | F1 | F2 | the mouth |
|---|---|---|---|
| **A** | 768 Hz | 1333 Hz | open, tongue low |
| **E** | 476 Hz | 2089 Hz | half closed, tongue forward |
| **I** | 342 Hz | 2322 Hz | closed, tongue forward and high |
| **O** | 497 Hz | 910 Hz | half closed, lips rounded |
| **U** | 378 Hz | 997 Hz | closed, lips rounded, tongue back |

(The average adult male voice, from a classic study of American English
vowels, Hillenbrand et al. 1995. Three more formants sit above these; they
change little between vowels.)

### The Vowel picture

The left picture is how phoneticians draw this: **F2 across** (high on the
left), **F1 down** (high at the bottom). Drawn that way it is a map of the
mouth seen from the side — I at the top left (front and closed), A at the
bottom (open), U at the top right (back and closed).

The five letters are the vowels. The grey line through them is the path the
**Vowel** knob walks: A → E → I → O → U. The bright dot is where the vowel is
now. Turn Vowel and watch the dot travel.

> **Vowel is continuous.** Between two letters the formants move smoothly from
> one vowel to the next — 1.3 is "E>I 30 %", three tenths of the way from E to
> I. The knob holds briefly on each vowel as you drag past it, so the five are
> easy to land on. Type a letter into the readout to jump to it, or a letter
> and a number — `E 30` — for a place between.

### Size: whose voice

A bigger body has a longer throat, and a longer throat resonates lower: every
formant moves down together. That is most of the difference between a bass,
an alto and a child singing the same vowel. **Size** does exactly that: up to
+100 % moves every formant down by a fifth (×0.8, a deep voice), down to
−100 % up by a quarter (×1.25, a small, bright one). In the Vowel picture the
whole path slides along its diagonal.

### Resonance: how sharp

**Resonance**, in the Vocal panel, sets how narrow the formants are. Low, they
are broad hills: a soft, breathy, natural vowel. High, they are sharp peaks
that whistle and ring — the sound of a talkbox or a wah pedal pushed hard.
50 % is a natural voice.

## Part 2 — becoming the vowel

### Vocal: how far

The right picture, **Envelope**, shows your sound's **spectral envelope**: its
overall colour across the frequencies, from 50 Hz to 10 kHz, with the fine
detail — the individual harmonics — smoothed away. The grey line is your sound
as it comes in. The bright line is what Vocalize makes of it. The two marks
are the vowel's F1 and F2.

At **Vocal** 0 % the two lines are one: the sound passes untouched, bit for
bit. Turn Vocal up and watch the bright line let go of the grey one and take
the vowel's shape — peaks at the two marks — until at 100 % only the vowel's
shape is left.

What stays is everything else: the pitch of a synth note, the rhythm of a drum
loop, the noise of a cymbal. That is why the result sounds like *your* sound,
singing — and why Vocal is the knob to automate. A slow ramp from 0 to 100 %
over a bar is a sound slowly finding a voice.

> **The level stays.** Taking a sound's colour away and putting a vowel's on
> changes how loud it is; Vocalize measures that and corrects it, so turning
> Vocal changes the colour and not the volume.

### Excite: something to shape

A vowel can only lift what is there. A pure sine — a sub bass, a flute, a
whistle — has one frequency and nothing at F2 for the vowel to lift, so it
barely changes. **Excite** gives it something: a soft saturation that adds
harmonics before anything else happens. It works the same on quiet and loud
notes, and keeps the level. Turn it up on a sine and hear the vowel appear.
On a rich sound — a saw, a voice, a drum loop — leave it low.

### Mode: two ways to the same vowel

Vocalize has two engines, and **Mode** picks one. Both put on the same vowel;
they differ in how.

- **Time** (the default) measures your sound's colour every five milliseconds
  with **linear prediction** — the technique speech codecs use — and filters it
  out; five resonators, one per formant, put the vowel in. It has **no
  latency**: right for playing live and for tight drums. Its sound is
  resonant and punchy, like a talkbox.
- **Spectral** cuts the sound into short overlapping frames, measures the
  colour of each (with a **cepstrum**), and swaps it for the vowel's exactly,
  frequency by frequency. It is smoother and more sung, closest to a real
  choir — and it is one frame late, about **21 ms**. Live compensates for that
  automatically, so the track stays in time with the others.

The Envelope picture's caption says which engine is drawing, and Spectral's
latency. Switching Mode dips the sound for 20 milliseconds while one engine
hands over to the other — it never clicks — and Live is told the new latency.
Mode cannot be automated: a latency that changed mid-song would move the track
against the others.

## Part 3 — your own voice

### Why every voice is different

The table in Part 1 is an average: many men, many vowels, one number each.
Your vowels are yours. The size of your throat moves all of them together, and
the way you shape each one — how far you open for A, how far forward your
tongue goes for I — moves each on its own. That is a large part of why your
voice sounds like you. Vocalize can learn it.

### Capture: learning a voice from a recording

Record yourself singing the five vowels, one after the other, a second or two
each, on a comfortable note: **aaa — eee — iii — ooo — uuu**. A low or middle
note works best. Then either:

- **drag the recording onto the plugin window** — from Live's browser, an
  audio clip or the Finder; the vowel chart lights up its outline while you
  drag — or
- click **Capture…** in the Vowel panel and choose the file.

WAV and AIFF work, and on a Mac anything the system can play (m4a, mp3, caf).
Vocalize reads the first two minutes, finds the moments where you sing a
steady vowel — breaths, consonants and silence are skipped — and measures your
formants there. It takes a moment; the button says **Capturing…** meanwhile.

**The order tells it which vowel is which.** If it hears exactly five sung
stretches, it takes them as A, E, I, O and U in that order, however your
vowels happen to sound — an Italian E, a Swedish U, your own way of singing
them. If there are more or fewer stretches, or the order clearly does not fit
(you sang U first, say), it decides by sound instead, comparing each moment
with the textbook vowels.

When it is done, the vowel chart moves to **your** vowels and its caption
names the recording. The letters of the vowels it heard are drawn bright —
**the brighter, the surer**: a vowel held steady is bright, one that wandered
is fainter, a hint to sing it again. A vowel you did not sing is filled in
from the textbook, scaled to the size of your voice, and drawn muted.
Capture again with a better take at any time.

If it finds nothing it can use, the chart says why: **NO SINGING IN** a file
that holds only silence, noise or spoken consonants; **NO VOWELS IN** one
that is sung but nowhere near a vowel; **CANNOT READ** a file that is not a
recording it can open. The voice you had before stays.

> **What is kept is your voice, not your recording.** The capture is saved
> with the Live set as fifty numbers — five formants for each of five vowels —
> so the set stays small and the recording can be moved or deleted.

### Voice: how much of you

**Voice**, in the Vowel panel, morphs from the textbook voice (0 %) to the
captured one (100 %, the default). Between, every formant lies part of the way
from one to the other, so a slow turn is the synth's voice slowly becoming
yours. Everything else works on top of it: **Vowel** walks your five vowels,
**Size** makes your voice bigger or smaller, **Resonance** sharpens or softens
it. With nothing captured, Voice does nothing.

**Forget** lets go of the captured voice: the textbook voice sings again.
Changed your mind? The button now reads **Restore** and brings it back — until
you capture or restore another voice, or open another set.

> **What travels with a set.** A shared set carries the captured voice — a
> coarse description of how your vowels sound — and the recording's file name,
> not the audio. Rename the file before capturing if its name says more than
> you want to share.

> **A clean capture.** One voice, no reverb or music behind it, every vowel
> held steady. Singing high spreads a voice's harmonics so far apart that the
> measurement is drawn towards them; if a capture sounds off, sing the vowels
> an octave lower.

## Part 4 — the level

### Ceiling: nothing past full scale

Sharp formants ring, and a vowel can peak higher than the sound that went in.
**Ceiling**, in the Level panel and on by default, leaves everything below
−0.3 dBFS exactly as it is and bends anything louder smoothly towards full
scale, so nothing that leaves Vocalize clips. Switch it off if you would
rather catch the peaks with your own limiter.

### Bypass

Bypassing Vocalize lets the sound through — late by exactly the latency Live
was told, so it stays in time — and Vocalize keeps listening meanwhile. When
you switch it back on it takes up the sound as it is now, fading in over
10 ms; nothing from before the bypass is heard again.

## Recipes

| sound | settings |
|---|---|
| **Talking synth** | A saw pad, Mode Time, Vocal 100 %, Resonance 70 %. Automate Vowel A → O → I over two bars. |
| **Choir pad** | A wide string pad, Mode Spectral, Vocal 80 %, Vowel O → U, Size +30 %, Resonance 40 %. |
| **Wah, but a mouth** | A guitar, Mode Time, Vocal 100 %, Resonance 85 %, Vowel automated U → A on each chord. |
| **Singing bass** | A sine sub, Excite 70 %, Vocal 90 %, Vowel O, Size +60 %. Excite gives the vowel something to shape. |
| **Drums that say something** | A drum loop, Mode Time, Vocal 60 %, Vowel E, Mix 50 %: the hats and snare take the vowel's colour, the kick keeps its weight. |
| **Fade into a voice** | Any sound, automate Vocal from 0 to 100 % over eight bars: the transformation is the arrangement. |
| **Child's voice** | Mode Spectral, Size −80 %, Resonance 55 %, Vowel I or E. |
| **Your choir** | Capture your own A-E-I-O-U, then a pad in Mode Spectral, Vocal 90 %, Voice 100 %: the pad sings with your vowels. |
| **Becoming you** | After a capture, automate Voice from 0 to 100 % over a phrase while Vowel moves: the textbook voice turns into yours. |

## Controls

Every knob reads in its own unit and takes what you type: click the readout,
type, Enter. **Double-click** a knob to reset it; hold **Shift** while dragging
for fine control. Size and Output run both ways from zero, so their arcs start
at the top.

| panel | control | range | default |
|---|---|---|---|
| Vowel | **Vowel** | A – U (0 – 4) | A |
| | **Size** | −100 – +100 % | 0 % |
| | **Voice** | 0 – 100 % (the textbook voice to the captured one) | 100 % |
| | **Capture…**, **Forget** / **Restore** | learn a voice from a recording, let it go, bring it back | — |
| Vocal | **Vocal** | 0 – 100 % | 80 % |
| | **Resonance** | 0 – 100 % | 50 % |
| | **Excite** | 0 – 100 % | 0 % |
| | **Mode** | Time / Spectral (not automatable) | Time |
| Level | **Mix** | 0 – 100 % | 100 % |
| | **Output** | −24 – +24 dB | 0 dB |
| | **Ceiling** | off / on (a soft limit at −0.3 dBFS) | on |

When Live switches the device off and on again, Vocalize starts from silence:
nothing it held before is heard again.
