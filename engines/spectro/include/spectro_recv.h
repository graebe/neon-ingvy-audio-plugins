// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * spectro-recv -- the C ABI for the listen-in receiver.
 *
 * spectro_core.h analyses ONE source: the track the plugin sits on. This is the
 * other half -- a receiver that also reads Listen-In buses, so a bass and a pad
 * can be looked at in one picture, and the places they are fighting can be
 * marked.
 *
 * It ships in the SAME static library as spectro_core.h (libspectro_capi.a), so
 * linking it costs nothing extra; see cmake/NiPlugin.cmake.
 *
 * THE THREAD RULES ARE PART OF THE ABI, and they are NOT the analyzer's:
 *
 *   srecv_new / free / start             one thread, nothing else in flight
 *   srecv_set_sources / set_clash        the main thread -- both allocate
 *   srecv_slots                          the main thread
 *   srecv_push_own                       the audio thread, and only it
 *   srecv_pump                           the message thread, and only it
 *   srecv_take_columns / clash / frame   the message thread, and only it
 *
 * THE TRANSFORMS RUN ON THE RECEIVER'S OWN THREAD once srecv_start has started
 * it: a worker, below the UI's priority, that wakes every few milliseconds,
 * drains the plugin's own audio and every bus, and queues finished columns for
 * srecv_take_columns. srecv_free stops and joins it. Without it, srecv_pump
 * does the same work on the caller's thread.
 *
 * A bus reader cannot be drained from the audio thread -- opening one allocates
 * and mmaps, and `abus_reader_read` is documented "one thread, the same one
 * each time". So every analyzer is fed in one place and the plugin's OWN audio
 * reaches it through a ring: ProcessBlock does nothing but copy its mono sum
 * into it. Every source is given the same number of frames, so column k of each
 * is the same moment -- which is what makes a per-cell clash mean anything
 * rather than being a coincidence.
 *
 * The handle is used from two threads at once -- push_own on the audio thread,
 * everything else on the message thread -- and is built for it: inside, the
 * audio thread's feed and the message thread's receiver are separate, and each
 * call touches only its own. Two threads calling push_own at once is undefined.
 *
 * THE MESSAGE SIDE IS SERIALISED, NOT MERELY TRUSTED. Its half sits behind a
 * lock, so a second thread calling in while the first is inside waits its turn:
 * nothing is corrupted and nothing deadlocks. That is a floor, not the design.
 * srecv_set_sources can hold the lock for a worker tick and a pump, and every
 * message-thread call behind it waits too -- so a plugin still calls these from
 * its main thread, and a host thread that wants a change (a state load) records
 * it for the main thread to apply. The audio thread's push_own takes no lock
 * and never waits on any of this.
 *
 * `srecv_push_own` allocates nothing, takes no lock and makes no system call.
 * `srecv_set_sources` does all three, which is why it is not allowed anywhere
 * near the audio thread.
 *
 * This header is written by hand and it is the contract: if it and
 * crates/spectro-capi/src/lib.rs disagree, they disagree silently.
 */
#ifndef SPECTRO_RECV_H
#define SPECTRO_RECV_H

#include "spectro_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Srecv srecv_t;

/* Channel 0 is always the track the plugin is inserted on. */
#define SRECV_OWN 0

/*
 * Sources one receiver draws at once, the own channel included.
 *
 * Each one past the first is a whole analysis chain -- up to a 16384-point
 * transform about 47 times a second. Four is also about where a picture stops
 * being readable, so the cost and the legibility run out together.
 *
 * SRECV_MAX_SOURCES is the same number for an array bound; srecv_api.c checks
 * the two agree.
 */
#define SRECV_MAX_SOURCES 4
int srecv_max_sources(void);

/*
 * Allocate a receiver. The own channel is configured exactly as
 * spectro_configure would, and every bus opened later inherits it -- which is
 * what lets their columns be compared at all.
 */
srecv_t* srecv_new(float sample_rate, int fft_size, int hop, int bands,
                   float f_min, float f_max, float db_floor, float db_ceil);
/* Stops and joins the analysis thread first, if one was started. */
void srecv_free(srecv_t* r);

/*
 * Start the analysis thread. Returns 1 if it is running (also when it already
 * was), 0 if it could not be created -- srecv_pump then goes on doing the work
 * on the caller's thread. Main thread.
 */
int srecv_start(srecv_t* r);

/* Channels drawable now: 1 (the own channel) plus each open bus. */
int srecv_channels(const srecv_t* r);

/* The bus slot behind a channel, or 0 for the own channel. */
int srecv_slot_of(const srecv_t* r, int ch);

/*
 * Non-zero when a channel's sender runs at another sample rate.
 *
 * SUCH A SOURCE IS NOT DRAWN, and that is deliberate: a different rate picks a
 * different window (8192 at 48 kHz, 16384 at 96) and therefore a different
 * group delay, so the two pictures would be offset from each other by an amount
 * nobody can see and nobody asked for. Saying so beats drawing it.
 */
int srecv_rate_mismatch(const srecv_t* r, int ch);

/*
 * Choose which buses to listen to -- `slots[0..n]`, 1-based, in the order they
 * should appear. Anything past srecv_max_sources()-1, any duplicate, and any
 * slot outside 1..abus_max_slot() is dropped from the request.
 *
 * Slots ALREADY OPEN ARE KEPT rather than reopened: a reopened reader starts at
 * the live edge, which would put a seam in a picture that had no reason for one.
 *
 * Once srecv_start has started the analysis thread this WAITS for it to adopt
 * the change -- at most a tick and one pump. A thread that is not running is
 * never waited for. Concurrent callers are served one after another, each
 * waiting for its own change: safe, but it blocks every other message-side
 * call meanwhile, which is why it belongs to the main thread.
 *
 * Main thread. Allocates, takes the message side's lock, and waits.
 */
void srecv_set_sources(srecv_t* r, const unsigned int* slots, int n);

/*
 * Feed `n` mono samples from this plugin's own track. AUDIO THREAD ONLY.
 * Copies into a ring and returns.
 */
void srecv_push_own(srecv_t* r, const float* mono, int n);

/*
 * Move audio into every analyzer, in step, on the calling thread; returns the
 * frames each source was given, commonly 0 -- and always 0 once srecv_start
 * has handed the work to the receiver's thread. MESSAGE THREAD ONLY.
 *
 * A source whose sample rate differs from the receiver's is read and
 * discarded, never analysed; the verdict is taken again whenever its sender
 * restarts. A bus that has delivered nothing for about a second is re-opened
 * if its sender quit and came back under the same slot.
 */
int srecv_pump(srecv_t* r);

/*
 * Columns every drawn channel has ready; take this many from each and column
 * k is the same moment in all of them. Columns become visible a whole pump at
 * a time -- the analysis thread feeds one analyzer after another, and a drain
 * that saw one channel's new column before the next channel had it would pair
 * them one column apart for good. Left out: a channel refused for its sample
 * rate, and a bus that has not drawn its first column yet. Message thread
 * only.
 */
int srecv_ready(const srecv_t* r);

/*
 * Drain one channel's finished columns -- as of the last finished pump --
 * spectro_bands() bytes each, oldest first. `out` must hold max_cols * bands
 * bytes; pass srecv_ready() as max_cols to keep the channels in step. Message
 * thread only.
 */
int srecv_take_columns(srecv_t* r, int ch, unsigned char* out, int max_cols);

/*
 * ONE EDITOR TICK'S PICTURE, in one call: every channel drained by the same
 * number of columns (srecv_ready, at most max_cols), the `view` channels added
 * in power into `sum_out`, and -- when cmp_a and cmp_b are both >= 0 and differ
 * -- their clash into `clash_out`. Returns the columns written to `sum_out`, 0
 * when there is nothing to send; *clash_cols gets the clash's, 0 when there is
 * none. A channel that drew nothing this tick is left out of the sum rather
 * than adding silence.
 *
 * A null `sum_out` drains and drops -- a closed editor keeps the rings from
 * filling with a picture nobody will see. Both outputs hold max_cols *
 * srecv_bands() bytes. Message thread only; allocates on its first call.
 */
int srecv_frame(srecv_t* r, const int* view, int n_view, int cmp_a, int cmp_b,
                unsigned char* sum_out, unsigned char* clash_out, int max_cols,
                int* clash_cols);

/* Bytes in one column of any channel: the band count they all share. */
int srecv_bands(const srecv_t* r);

/*
 * The band centre frequencies, ascending. ONE axis for every source, because
 * they share a configuration -- which is what lets their columns be compared
 * cell by cell in the first place.
 */
int srecv_band_hz(const srecv_t* r, float* out, int n);

/* The range every source is measured over. Safe while audio runs. */
void srecv_set_range(const srecv_t* r, float f_min, float f_max);

/*
 * What counts as a clash: a floor in dBFS that BOTH sources must clear, and a
 * balance window in dB past which the louder one is simply winning rather than
 * competing.
 *
 * The floor alone is not enough. A product of two spectra is a SUM in dB, so
 * 0 dB against -60 scores what -30 against -30 scores -- and only the second is
 * a clash. `min` answers "both present"; the window answers "and neither wins".
 */
void srecv_set_clash(srecv_t* r, float floor_db, float balance_db);

/*
 * Clash strength for `n_cols` columns of `a` against `b`, into `out`. All three
 * hold n_cols * spectro_bands() bytes.
 *
 * It takes columns the caller ALREADY drained rather than draining again:
 * srecv_take_columns is destructive, so a second drain would compare one
 * source's present against another's future.
 */
void srecv_clash(const srecv_t* r, const unsigned char* a, const unsigned char* b,
                 unsigned char* out, int n_cols);

/*
 * Add several channels' columns into one, in POWER. `srcs` is `n_src` pointers,
 * each to n_cols * spectro_bands() bytes; `out` likewise.
 *
 * IT CANNOT BE DONE IN BYTE SPACE, and that is the whole reason this exists. A
 * byte is linear in dB, so adding two bytes adds two DECIBELS -- which
 * multiplies two amplitudes, and would put two -20 dB sources at -40, quieter
 * than either. This inverts to power, adds, and re-encodes: +3 dB for two equal
 * uncorrelated sources, which is what a bass and a pad actually measure. Not +6
 * -- that is amplitude addition, and it assumes they are phase locked.
 *
 * Like srecv_clash it takes columns the caller ALREADY drained: taking them
 * again would add one source's present to another's future.
 */
void srecv_sum(const srecv_t* r, const unsigned char* const* srcs, int n_src,
               unsigned char* out, int n_cols);

/*
 * Non-zero when a channel is being zero-filled because its sender has gone
 * quiet -- a muted Listen-In, or one whose host stopped calling it.
 *
 * A bus that publishes nothing used to hold every OTHER source still, because
 * the pump waited for the slowest; the plugin's own picture stopped dead
 * because something else went quiet, and the editor went on saying it was live.
 * Now the own track sets the pace and a silent bus is filled with silence --
 * which is true, and keeps column k the same moment for every source. This is
 * how the editor knows to SAY so rather than letting a black stripe read as
 * "that track is silent".
 */
int srecv_starved(const srecv_t* r, int ch);

/*
 * The source list: every slot that exists, as "<slot>:<live>:<rate>:<label>",
 * one per line. Returns the bytes the text needs -- so a caller with too small
 * a buffer can ask again rather than guess. `out` is always NUL-terminated when
 * cap > 0.
 *
 * PROBING CREATES NOTHING. Walking all sixteen slots to fill a dropdown leaves
 * the machine exactly as it found it.
 */
int srecv_slots(unsigned char* out, int cap);

/*
 * Frames a channel lost before the receiver reached them. A diagnostic: it
 * should stay zero, and a jump means whatever pumps -- the receiver's thread,
 * or the srecv_pump caller -- stopped keeping up, not that the analysis broke.
 */
int srecv_dropped(const srecv_t* r, int ch);

#ifdef __cplusplus
}
#endif

#endif /* SPECTRO_RECV_H */
