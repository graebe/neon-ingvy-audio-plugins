/*
 * Spectrogram -- a rolling analyzer for Ableton Live, on iPlug2.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Audio passes through bit for bit; the plugin's whole output is a picture.
 *
 * THE ANALYSIS IS NOT HERE. It is the Rust crate in engines/spectro, reached
 * through its C ABI (spectro_core.h), and the division of labour is the one the
 * Trance Gate uses: the engine owns everything that is a decision about sound,
 * this file owns everything that is a decision about a host. So the FFT size,
 * the log frequency mapping and the dB scale are the crate's, and the wire
 * format, the message tags and the buffer lifetimes are this file's.
 *
 * WHAT CROSSES A THREAD, AND HOW. ProcessBlock pushes the mono sum into the
 * receiver's ring; the receiver's own thread transforms it and leaves finished
 * columns -- one byte per band -- in lock-free rings. OnIdle drains those and
 * hands the columns to the WebView as hex. The rules are in spectro_recv.h.
 */
#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "spectro_recv.h"
#include "shell_handoff.h"
#include "shell_state.h"
#include "ground_detect.h"  /* the ground's kick detector; editor builds only */
#include "Wire.h"
#include <atomic>
#include <string>
#include <vector>

const int kNumPresets = 1;

/* The state chunk's layout, after shell_state.h's header: parameters (none
 * yet), then sources, clash, view and comparison as strings. A chunk with no
 * header is this layout as every earlier build wrote it. */
constexpr int32_t kChunkVersion = 1;

/*
 * NO PARAMETERS, and that is a statement rather than an omission.
 *
 * There is nothing about this plugin for a host to automate yet: it has no
 * value that changes what comes out. When Range (the dB floor) and Speed arrive
 * they will be ordinary host parameters in this enum -- not a dummy to keep a
 * host happy, which is the usual reason a zero-parameter plugin grows one.
 */
enum EParams
{
  kNumParams = 0
};

using namespace iplug;

/* iplug::Plugin, spelled out: the CLAP target pulls in clap-helpers, which has
 * a `Plugin` template of its own, and `using namespace iplug` makes the
 * unqualified name resolve to the wrong one there. */
class Spectrogram final : public iplug::Plugin
{
public:
  Spectrogram(const InstanceInfo& info);
  ~Spectrogram();

  /*
   * THE MESSAGE TAGS, both directions. The Trance Gate reserves 0..kNumParams-1
   * for parameter display strings; there are no parameters here, but the
   * numbering is kept so the two plugins' tags mean the same things.
   */
  enum EMsgTags
  {
    kMsgCols = 64,      /* -> UI: "<cols>:<bands>:<hex>", oldest column first */
    kMsgAxis,           /* -> UI: the band centre frequencies, comma separated */
    /*
     * THE HOST'S CLOCK, EVERY TICK, whether or not a column went with it.
     *
     * "<ppq>:<bpm>:<num>:<denom>:<running>:<ppqPerCol>". The editor's bar view
     * places each column at the pixel its musical position implies, so it needs
     * the clock even on the ticks the analyzer had nothing finished -- and
     * especially then, because that is when the playhead is crossing a bar line
     * with no column to announce it.
     *
     * NOTHING HERE SAYS HOW WIDE THE WINDOW IS. How many bars the picture spans
     * is a layout decision and lives in the editor, which is why this plugin
     * has no bar count to be told and no message to be told it with.
     */
    kMsgSync,
    /*
     * -> UI: the source list, "<slot>:<live>:<rate>:<label>" a line.
     *
     * Sent on demand and on a slow timer rather than every tick: a Listen-In
     * appears when somebody inserts one, which is a human-speed event, and
     * probing sixteen slots fifty times a second to learn nothing would be a
     * syscall storm in aid of a dropdown.
     */
    kMsgSources,
    /*
     * -> UI: the clash mask, in the same "<ch>:<cols>:<bands>:<hex>" shape as a
     * column batch -- because that is exactly what it is. `ch` names the source
     * this channel was compared AGAINST the own one, so the editor can union
     * several or show one.
     */
    kMsgClashCols,
    /* -> UI: one kick, as a strength in 0..1. Sent only when the detector
     * fires, not every tick -- one message is one ring on the ground. */
    kMsgGround,

    kMsgRange = 96,     /* <- UI: "<f_min>:<f_max>" -- the zoom               */
    /*
     * <- UI: "<slot>,<slot>,..." -- which buses to open, in order. Empty means
     * the own channel alone.
     *
     * DERIVED IN THE EDITOR from what it is viewing and what it is comparing,
     * rather than being a control of its own. Having a third list to manage was
     * the thing that made "view" and "compare" feel like one tangled setting.
     */
    kMsgSelect,
    /*
     * <- UI: "<ch>,<ch>,..." -- which channels are ADDED into the picture.
     *
     * The plugin sums them, so the editor receives ONE stream and never routes
     * by channel. That is not a tidy-up: routing by channel is what made a
     * batch for an unselected source silently dropped, and a saved session come
     * back showing the wrong track with no way to say so.
     */
    kMsgView,
    /* <- UI: "<a>:<b>:<on>" -- the two channels the clash is measured between,
     * and whether to send it. Independent of what is being viewed. */
    kMsgCompare,
    /* <- UI: "<floor_db>:<balance_db>" -- what counts as a clash. */
    kMsgClash,

    /*
     * "I AM LISTENING", and it is not optional. OnUIOpen fires from
     * didFinishNavigation, but the editor is a <script type="module"> and module
     * scripts are DEFERRED -- they evaluate after the document is done, so
     * anything pushed from OnUIOpen lands before globalThis.SAMFD exists and is
     * dropped on the floor. The Trance Gate lost twelve parameter values to
     * this and the symptom wore four different hats.
     *
     * Here the loss would be the frequency scale: the picture would roll
     * correctly with no numbers beside it.
     */
    kMsgReady = 102     /* <- UI: mounted -- send me the axis */
  };

  /* iPlug2's WebView transport formats through
   * WDL_String::SetFormatted(mMaxJSStringLength, ...), which TRUNCATES rather
   * than fails, and base64 costs a third on top. Raised from the 8192 default
   * in the constructor and asserted against at the one call that can approach
   * it. */
  static constexpr int kMaxJSString = 65536;

  /*
   * The most columns one OnIdle tick will send. At the analyzer's defaults a
   * 60 Hz tick has NONE OR ONE waiting -- the picture scrolls at ~47 columns a
   * second -- so this is not a throttle. It is the bound that makes the payload
   * provably fit: 32 columns x 256 bands x 2 hex characters is 16 KB, ~22 KB
   * once base64 has inflated it, against the 64 KB cap above.
   *
   * It was 64 when a column was 128 bands. The bands doubled, so this halved:
   * the product is what the cap constrains, and holding one of them constant
   * while the other grows is how a payload quietly starts being truncated.
   */
  static constexpr int kMaxColsPerTick = 32;

  /*
   * THE BUDGET, CHECKED BY THE COMPILER.
   *
   * The three asserts that used to be the only enforcement of this are gone
   * under -DNDEBUG, and -DNDEBUG is what the documented Release build sets --
   * so in the configuration that actually ships, nothing held this at all.
   * The failure it guards is WDL_String::SetFormatted truncating rather than
   * failing: a short payload decodes to a column of garbage that reads as a
   * real transient.
   *
   * Two hex characters a byte, base64's extra third, and the frame's 32.
   */
  static_assert(spectro::wire::framed_size(kMaxColsPerTick * SPECTRO_BANDS * 2)
                    < kMaxJSString,
                "a full OnIdle tick no longer fits the WebView's string cap -- "
                "kMaxColsPerTick and SPECTRO_BANDS are what constrain it");

  /* The receiver's lifecycle, then -- with an editor -- every column the
   * analyzer has finished since the last tick. iPlug2's idle timer belongs to
   * the API wrapper, so this runs with or without a window. */
  void OnIdle() override;

#ifdef WEBVIEW_EDITOR_DELEGATE
  /* One message per onset, from OnIdle. */
  void SendGround();
  void OnUIOpen() override;
  /* Switches the ground's detector off; see the definition. */
  void CloseWindow() override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
#endif

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
#endif

  /*
   * The chosen sources and the clash settings, so a session reopens looking at
   * what it was looking at. Read back DEFENSIVELY -- a chunk written by a
   * future version may hold more than this one knows how to want, and this
   * plugin's chunk was empty until now, so v1 sets will be unserialised by v2
   * code. Listen-In's arrangement, for Listen-In's reason.
   */
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

private:
  /* Main thread only: replaces the receiver when OnReset has asked for one. */
  void ServiceReceiver();
  /* Hand the receiver the current selection. Main thread: it allocates. */
  void ApplySources();
#ifdef WEBVIEW_EDITOR_DELEGATE
  /* The columns, the transport and the source list, from OnIdle. */
  void SendPicture();
  /* The frequency scale, sent on kMsgReady. The UI never computes it: the log
   * mapping lives in the analyzer and a second copy would drift. */
  void SendAxis();
  /* The transport, once a tick. Cheap by construction: a hundred-odd bytes
   * against the 64 KB the column payload is budgeted out of. */
  void SendSync();
  /* The buses that exist, for the editor's picker. Probing creates nothing. */
  void SendSources();
#endif

  /*
   * THE RECEIVER, NOT A BARE ANALYZER, and that is the whole shape of this
   * plugin now. It holds the own channel AND every bus being listened to, and
   * it feeds them all the same number of frames so their columns can be
   * compared cell by cell. The transforms run on its own thread, started with
   * srecv_start; see spectro_recv.h.
   */
  srecv_t* mRecv = nullptr;

  /*
   * AND IT IS THE MAIN THREAD'S, LENT TO THE AUDIO THREAD.
   *
   * mRecv is the main thread's handle: built, configured, drained and freed
   * there -- and freeing it joins its analysis thread. ProcessBlock reaches the same object only through mRecvLend, which
   * frees a replaced receiver once the audio thread has let go of it. OnReset
   * -- which some AU hosts call off the main thread -- only records the rate
   * and asks for a rebuild; OnIdle does it. Until then the audio thread feeds
   * nothing, rather than feed a receiver configured for the old rate.
   */
  shell_handoff_t* mRecvLend = nullptr;
  std::atomic<float> mRecvRate{0.f};
  std::atomic<bool> mRecvStale{false};

  /*
   * The mono sum, and the drain buffer. Both are sized on the main thread --
   * OnReset for the first, the constructor for the second -- because a resize
   * on the audio thread is a malloc on the audio thread.
   */
  std::vector<float> mMono;
  /* One drain buffer per channel, plus one for the clash. Sized in the
   * constructor, never on the audio thread -- and never resized, because the
   * band count cannot change without a configure. */
  /*
   * ONE BUFFER PER CHANNEL, because the view is a SUM and the comparison is a
   * pair: both need several channels' columns alive at the same moment, and a
   * drained column is gone. Sized in the constructor for the most sources a
   * receiver will ever open.
   */
  std::vector<std::vector<unsigned char>> mChanCols;
  std::vector<int> mChanCount;
  std::vector<unsigned char> mSum;
  std::vector<unsigned char> mClash;

  /*
   * WHICH BUSES, AND WHAT COUNTS AS A CLASH.
   *
   * Both are saved state rather than parameters. The Trance Gate's rule is that
   * a number a host should own belongs in the parameter set -- a Listen-In's
   * BUS is one, because somebody will automate a switch between two sources.
   * A receiver's VIEW is not: nobody automates which picture they are looking
   * at, and this plugin's "NO PARAMETERS" above is a statement rather than an
   * omission.
   */
  std::vector<unsigned int> mSources;
  /* Which channels are added into the picture. Channel 0 is this track. */
  std::vector<int> mView;
  /* The two the clash is measured between, and whether it is wanted. */
  int mCmpA = 0;
  int mCmpB = 1;
  bool mClashOn = false;
  float mClashFloorDb = -60.0f;
  float mClashBalanceDb = 12.0f;

  /* The source list is rebuilt on a slow timer; this is the countdown. */
  int mSourceTick = 0;

  /*
   * THE HOST'S CLOCK, WRITTEN ON THE AUDIO THREAD AND READ ON THE MESSAGE ONE.
   *
   * ProcessBlock is the only place a host's transport is legible -- OnIdle runs
   * off a timer with no block to ask about -- so the position is maintained
   * there and published through these for OnIdle to send.
   *
   * Relaxed throughout: these are four independent facts for a picture, not a
   * protocol. A tick that reads a position from one block and a tempo from the
   * next is off by at most 20 ms of drawing, and no ordering between them would
   * make the picture more true than that.
   */
  static_assert(std::atomic<double>::is_always_lock_free,
                "a lock on the audio thread to publish a playhead is not a trade "
                "worth making -- publish it as fixed point instead");
  std::atomic<double> mPubPpq{0.0};
  std::atomic<double> mPubBpm{120.0};
  std::atomic<double> mPubPpqPerCol{0.0};
  std::atomic<int> mPubSig{(4 << 8) | 4};   /* numerator << 8 | denominator */
  std::atomic<bool> mPubRunning{false};

  /*
   * THE FREE-WHEELING POSITION, audio thread only.
   *
   * When the transport runs, this is the host's own PPQ. When it stops -- or
   * when the host reports no beat timeline at all, which is what CLAP does
   * without CLAP_TRANSPORT_HAS_BEATS_TIMELINE -- it keeps advancing at the last
   * tempo that was seen, so the picture goes on filling rather than freezing on
   * a stopped transport.
   */
  double mPos = 0.0;
  double mLastBpm = 120.0;

  /* What OnReset gave the analyzer. Kept because ppqPerCol is hop over the
   * sample rate, and the editor cannot know either. */
  int mHop = 0;

  /*
   * THERE IS NO PAUSE HERE, AND THAT IS THE DESIGN.
   *
   * Pause holds the VIEW. The analysis runs, the columns keep being sent, and
   * the editor keeps writing them into its history -- it simply stops
   * repainting. So unpausing shows a picture that is already current, with the
   * paused seconds present in it rather than cut out of it.
   *
   * An earlier version stopped the sending and dropped what arrived meanwhile,
   * which left a seam in the timeline at the exact moment someone had been
   * staring at it.
   */
  /* The hex payload, reused. Reserved once so OnIdle does not allocate 60 times
   * a second for the life of the session. */
  std::string mHex;

#ifdef WEBVIEW_EDITOR_DELEGATE
  /*
   * THE ANIMATED GROUND'S KICK DETECTOR. The design system gives every window a
   * ground that rings when a kick lands, and the editor is a WebView with no
   * access to the host's audio -- so the detection happens here and one message
   * per onset crosses over. engines/ground/include/ground_detect.h says why, and
   * shows the whole idiom.
   *
   * mGroundFires is the last count the EDITOR was told about, so it belongs to
   * the message thread alone and needs no atomic. Inside the editor guard, so a
   * build with no WebView carries no detector on its audio thread.
   */
  gnd_t* mGround = nullptr;
  uint32_t mGroundFires = 0;
#endif

};
