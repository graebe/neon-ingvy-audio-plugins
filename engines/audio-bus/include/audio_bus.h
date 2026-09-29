/*
 * audio-bus -- the C ABI.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * A SHARED-MEMORY AUDIO BUS between plugins in one host. A sender claims one of
 * sixteen numbered slots and publishes stereo float audio into it; any number
 * of receivers, in this process or another, open the same slot and read it.
 *
 * Nobody blocks anybody. The sender never waits. A receiver that falls behind
 * is TOLD how much it missed rather than handed a buffer spliced together from
 * two different moments -- which would look exactly like audio and be exactly
 * wrong. See `dropped` on abus_reader_read.
 *
 * THE THREAD RULES ARE PART OF THE ABI:
 *
 *   abus_writer_claim / release            the main thread
 *   abus_writer_set_label / sample_rate    the main thread
 *   abus_writer_push                       the audio thread, and only it
 *   abus_reader_open / close               the main thread
 *   abus_reader_read                       one thread, the same one each time
 *   abus_probe                             the main thread
 *
 * push and read may overlap across any number of processes -- that is the whole
 * point. TWO SENDERS ON ONE SLOT IS THE ONE THING THAT CANNOT HAPPEN, and it is
 * prevented rather than left undefined: the second abus_writer_claim returns
 * ABUS_ERR_TAKEN and the caller publishes nothing.
 *
 * `abus_writer_push` allocates nothing, takes no lock and makes no system call.
 * `abus_writer_claim` does all three, which is why it is not allowed anywhere
 * near the audio thread.
 *
 * This header is written by hand rather than generated, and it is the contract:
 * if it and crates/bus-capi/src/lib.rs disagree, they disagree silently.
 * tests/abus_roundtrip.c and tests/abus_ipc.c compile against THIS file and
 * link the real library, which is what keeps them honest.
 */
#ifndef AUDIO_BUS_H
#define AUDIO_BUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AbusWriter abus_writer_t;
typedef struct AbusReader abus_reader_t;

#define ABUS_OK                0
#define ABUS_ERR_BAD_SLOT     -1  /* outside 1..abus_max_slot()            */
#define ABUS_ERR_UNAVAILABLE  -2  /* the segment could not be made or mapped */
#define ABUS_ERR_TAKEN        -3  /* another LIVE sender holds this slot    */

/* Bytes in the buffer abus_probe fills, NUL included. */
#define ABUS_LABEL_CAP 32

/* Highest valid slot number. Slots are 1-based: slot 0 is not a bus, it is a
 * mistake, and it is reported as one. */
uint32_t abus_max_slot(void);

/* Always 2, always interleaved. A mono source is duplicated by the SENDER, so
 * a receiver never has to ask how many channels arrived. */
uint32_t abus_channels(void);

/*
 * THE SENDING END.
 *
 * On ABUS_OK, *out holds a handle to release later. On anything else *out is
 * untouched and the caller has no bus -- which is a normal state, not an
 * error to swallow: a second Listen-In on a taken slot must SAY SO rather than
 * quietly publish nothing.
 */
int  abus_writer_claim(uint32_t slot, uint32_t sample_rate, abus_writer_t** out);
void abus_writer_release(abus_writer_t* w);

/*
 * Publish one block: `frames * abus_channels()` interleaved floats.
 *
 * A NULL handle is a no-op, deliberately. ProcessBlock calls this
 * unconditionally, and "the slot was taken" is then silence rather than a
 * branch at the call site.
 */
void abus_writer_push(abus_writer_t* w, const float* interleaved, uint32_t frames);

/* The host's rate changed. Readers are resynced: samples either side of a rate
 * change are not the same signal, and splicing them would draw a transient
 * that never happened. */
void abus_writer_set_sample_rate(abus_writer_t* w, uint32_t sample_rate);

/* NUL-terminated UTF-8. Anything past ABUS_LABEL_CAP-1 bytes is dropped. */
void abus_writer_set_label(abus_writer_t* w, const char* text);

/*
 * THE RECEIVING END. Any number of these, in any number of processes, with no
 * coordination between them -- each keeps its own place in the stream.
 *
 * Opening succeeds only for a slot that EXISTS. A receiver never brings a bus
 * into existence by looking at one.
 */
int  abus_reader_open(uint32_t slot, abus_reader_t** out);
void abus_reader_close(abus_reader_t* r);

/*
 * Copy out up to max_frames. Returns the frames actually delivered, which is
 * commonly 0: a reader polling faster than the audio arrives is the normal
 * case, not a failure.
 *
 * `dropped` receives frames that existed and were lost before this read could
 * reach them; `resynced` is non-zero when the sender restarted (claimed the
 * slot, or changed rate) and the stream is not continuous with what came
 * before. Either may be NULL -- though a caller that never looks at `dropped`
 * is a caller that cannot tell a gap from silence.
 */
uint32_t abus_reader_read(abus_reader_t* r,
                          float*         out,
                          uint32_t       max_frames,
                          uint64_t*      dropped,
                          int32_t*       resynced);

/*
 * Describe a slot without opening it -- what a receiver builds its source list
 * from. Returns 1 if the slot exists, 0 if nobody has ever used it.
 *
 * `live` distinguishes a bus with a sender on it from one whose sender has
 * gone; `label` receives at most label_cap bytes including the NUL. Any of the
 * three out-parameters may be NULL.
 *
 * PROBING CREATES NOTHING. Walking all sixteen slots to fill a dropdown leaves
 * the machine exactly as it found it.
 */
int abus_probe(uint32_t  slot,
               int32_t*  live,
               uint32_t* sample_rate,
               char*     label,
               uint32_t  label_cap);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_BUS_H */
