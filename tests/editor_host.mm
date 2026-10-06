// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editors, opened by a real host, in each format.
 *
 *   editor_host <vst3|au|clap> <bundle>
 *
 * WHAT IT DOES. The bundle this checkout BUILT is loaded in-process -- the
 * VST3 through its factory, the AU registered in this process only
 * (au_bundle.h), the CLAP through its entry -- and driven the way a DAW drives
 * it: a render thread feeding a 1 kHz sine with a kick under it, a transport
 * running at 120 BPM, the main run loop for iPlug2's idle timer, and the
 * editor opened through the format's own call (VST3 attached, AU
 * uiViewForAudioUnit, CLAP set_parent + show). The page is iPlug2's real
 * WKWebView with the shipped bundle in it. The product is the bundle's name.
 *
 * Then the page itself is asked what arrived. Twice: the editor is closed
 * through the format's own call (removed, removeFromSuperview, hide + destroy)
 * and opened again, because the shell's editor-open state is set and cleared
 * by that sequence and a dead reopen is as blank as a dead open. Each time:
 *
 *   every editor
 *   - the page says `ready` again and the plugin answers it with the defaults:
 *     the shell knows an editor is open;
 *   - the plugin's own state, saved and loaded back through the format's calls
 *     on a thread that is not the main one, never reaches the WebView from
 *     that thread, and every value and display string reaches the page after;
 *   - the ground rings on the beat: the shell's beat clock is active and
 *     its messages are sent;
 *
 *   the Spectrogram
 *   - the session and the axis answer the ready too;
 *   - column batches arrive whose bytes are not all zero -- a fresh instance's
 *     receiver is fed and drained, and the real encoder's payloads cross --
 *     the fortieth within ten seconds of the first;
 *   - the editor's own decoder and canvas put the columns that arrived on
 *     the visible canvas;
 *
 *   the Trance Gate and the Side-Chain
 *   - the playhead (the pattern's line; the shaper's sweep) moves with the
 *     transport: three positions, the third within five seconds of the first;
 *
 *   the Listen-In
 *   - the bus's state and its name answer the ready, and a fresh instance's
 *     name field is empty -- not "(null)", which is what an empty payload
 *     once reached the page as.
 *
 * And over the whole run, editor closed or open: nothing calls the WebView's
 * evaluateJavaScript: from a thread that is not the main one.
 *
 * WHY. The e2e suite drives the editors against a mock host, which cannot
 * notice when the real plugin-to-editor path stops delivering: the mock sends
 * what it sends whatever the plugin does. This is that path, end to end, with
 * nothing mocked but the DAW.
 *
 * WHAT IT NEEDS: a logged-in macOS session -- WKWebView needs a window
 * server. The window it opens may be behind others; WebKit then calls the page
 * hidden, services no animation frames and throttles timers, which the
 * editors must survive (it is what a host whose window WebKit misjudges looks
 * like).
 */
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <objc/runtime.h>

#include "au_bundle.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "clap/clap.h"

#include <dlfcn.h>
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

static constexpr double kRate = 48000.0;
static constexpr int kBlock = 512;
/* The longest any condition is waited for. Generous on purpose: it bounds a
 * hang, it is not a speed requirement -- an idle machine passes in seconds. */
static constexpr double kPatience = 60.0;

static int gFails = 0;
static void check(bool ok, const char* what, const std::string& detail = "")
{
  printf("  %-60s %s%s%s%s\n", what, ok ? "ok" : "FAIL", detail.empty() ? "" : " (",
         detail.c_str(), detail.empty() ? "" : ")");
  if (!ok)
    gFails++;
}

/* The source: a sine for the picture, and a kick every half second the
 * ground no longer listens to -- it rings on the transport's beat. */
static void source(float* l, float* r, int n)
{
  static long t = 0;
  static double phase = 0.0;
  for (int i = 0; i < n; i++, t++)
  {
    const double k = (t % 24000) / kRate;
    const double kick = 0.8 * std::exp(-k * 25.0) * std::sin(2.0 * M_PI * (50.0 + 150.0 * std::exp(-k * 40.0)) * k);
    l[i] = r[i] = float(0.2 * std::sin(phase) + kick);
    phase += 2.0 * M_PI * 1000.0 / kRate;
  }
}

/* The host's transport: playing from bar 1 at 120 BPM in 4/4. */
static constexpr double kBpm = 120.0;
static double Beats(double samples) { return samples / kRate * (kBpm / 60.0); }

/* ------------------------------------------------------------- the formats */

/* What a DAW does with a plugin, in the three dialects. */
struct Format
{
  virtual ~Format() = default;
  virtual bool Load(const char* bundle) = 0;
  /* One block through the audio path. The render thread. */
  virtual void Process() = 0;
  /* The editor into `parent`, and out again. The main thread. */
  virtual bool Open(NSView* parent) = 0;
  virtual void Close() = 0;
  /* The plugin's own state, saved and loaded straight back through the
   * format's calls: a preset recall that changes nothing. Both on the calling
   * thread, which is the point -- CheckReload calls it from one that is not
   * the main thread. */
  virtual bool Reload() = 0;
  virtual void Unload() = 0;
};

using namespace Steinberg;

/* The smallest IBStream a host could hand over: bytes and a position. */
struct Stream : IBStream
{
  std::vector<char> bytes;
  int64 pos = 0;

  tresult PLUGIN_API queryInterface(const TUID, void** obj) override
  {
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }

  tresult PLUGIN_API read(void* buffer, int32 n, int32* got) override
  {
    const int32 k = int32(std::clamp<int64>(int64(bytes.size()) - pos, 0, n));
    if (k > 0)
      memcpy(buffer, bytes.data() + pos, size_t(k));
    pos += k;
    if (got)
      *got = k;
    return kResultOk;
  }
  tresult PLUGIN_API write(void* buffer, int32 n, int32* put) override
  {
    if (size_t(pos + n) > bytes.size())
      bytes.resize(size_t(pos + n));
    memcpy(bytes.data() + pos, buffer, size_t(n));
    pos += n;
    if (put)
      *put = n;
    return kResultOk;
  }
  tresult PLUGIN_API seek(int64 to, int32 mode, int64* at) override
  {
    const int64 base = mode == kIBSeekSet ? 0 : mode == kIBSeekCur ? pos : int64(bytes.size());
    if (base + to < 0 || base + to > int64(bytes.size()))
      return kResultFalse;
    pos = base + to;
    if (at)
      *at = pos;
    return kResultOk;
  }
  tresult PLUGIN_API tell(int64* at) override
  {
    if (at)
      *at = pos;
    return kResultOk;
  }
};

struct Vst3 : Format
{
  Vst::IComponent* comp = nullptr;
  Vst::IAudioProcessor* proc = nullptr;
  Vst::IEditController* ctrl = nullptr;
  IPlugView* view = nullptr;
  Vst::ProcessContext ctx{};
  std::vector<float> l = std::vector<float>(kBlock), r = l, ol = l, orr = l;

  bool Load(const char* bundle) override
  {
    const std::string path(bundle);
    const std::string name = [[[NSString stringWithUTF8String:bundle] lastPathComponent]
                               stringByDeletingPathExtension].UTF8String;
    void* h = dlopen((path + "/Contents/MacOS/" + name).c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h)
      return false;
    auto entry = (bool (*)(CFBundleRef)) dlsym(h, "bundleEntry");
    auto factory = (IPluginFactory * (*)()) dlsym(h, "GetPluginFactory");
    if (!entry || !factory)
      return false;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*) bundle, strlen(bundle), true);
    CFBundleRef cfb = CFBundleCreate(nullptr, url);
    CFRelease(url);
    if (!entry(cfb))
      return false;
    IPluginFactory* f = factory();
    for (int i = 0; f && i < f->countClasses() && !comp; i++)
    {
      PClassInfo ci;
      if (f->getClassInfo(i, &ci) == kResultOk && !strcmp(ci.category, kVstAudioEffectClass))
        f->createInstance(ci.cid, Vst::IComponent::iid, (void**) &comp);
    }
    if (!comp || comp->initialize(nullptr) != kResultOk)
      return false;
    comp->queryInterface(Vst::IAudioProcessor::iid, (void**) &proc);
    comp->queryInterface(Vst::IEditController::iid, (void**) &ctrl);
    if (!proc || !ctrl)
      return false;
    Vst::ProcessSetup setup{Vst::kRealtime, Vst::kSample32, kBlock, kRate};
    proc->setupProcessing(setup);
    comp->activateBus(Vst::kAudio, Vst::kInput, 0, true);
    comp->activateBus(Vst::kAudio, Vst::kOutput, 0, true);
    comp->setActive(true);
    proc->setProcessing(true);
    ctx.sampleRate = kRate;
    ctx.tempo = kBpm;
    ctx.timeSigNumerator = ctx.timeSigDenominator = 4;
    ctx.state = Vst::ProcessContext::kPlaying | Vst::ProcessContext::kTempoValid |
                Vst::ProcessContext::kTimeSigValid | Vst::ProcessContext::kProjectTimeMusicValid;
    return true;
  }

  void Process() override
  {
    source(l.data(), r.data(), kBlock);
    float* in[2] = {l.data(), r.data()};
    float* out[2] = {ol.data(), orr.data()};
    Vst::AudioBusBuffers ib{}, ob{};
    ib.numChannels = ob.numChannels = 2;
    ib.channelBuffers32 = in;
    ob.channelBuffers32 = out;
    Vst::ProcessData pd{};
    pd.processMode = Vst::kRealtime;
    pd.symbolicSampleSize = Vst::kSample32;
    pd.numSamples = kBlock;
    pd.numInputs = pd.numOutputs = 1;
    pd.inputs = &ib;
    pd.outputs = &ob;
    pd.processContext = &ctx;
    proc->process(pd);
    ctx.projectTimeSamples += kBlock;
    ctx.projectTimeMusic = Beats(double(ctx.projectTimeSamples));
  }

  bool Open(NSView* parent) override
  {
    view = ctrl->createView(Vst::ViewType::kEditor);
    return view && view->attached((__bridge void*) parent, kPlatformTypeNSView) == kResultOk;
  }

  void Close() override
  {
    view->removed();
    view->release();
    view = nullptr;
  }

  /* A host restores both halves, the component's and then the controller's.
   * iPlug2's single-component wrapper restores from setState and ignores
   * setComponentState; it is called because a host calls it. */
  bool Reload() override
  {
    Stream state;
    if (comp->getState(&state) != kResultOk)
      return false;
    state.pos = 0;
    if (comp->setState(&state) != kResultOk)
      return false;
    state.pos = 0;
    return ctrl->setComponentState(&state) == kResultOk;
  }

  void Unload() override
  {
    proc->setProcessing(false);
    comp->setActive(false);
    comp->terminate();
  }
};

struct Au : Format
{
  AudioUnit au = nullptr;
  NSView* view = nil;
  AudioTimeStamp ts{};
  std::vector<float> l = std::vector<float>(kBlock), r = l;

  /* The transport, through the host callbacks iPlug2's AU wrapper reads. */
  static OSStatus BeatAndTempo(void* self, Float64* beat, Float64* tempo)
  {
    if (beat) *beat = Beats(static_cast<Au*>(self)->ts.mSampleTime);
    if (tempo) *tempo = kBpm;
    return noErr;
  }
  static OSStatus MusicalTime(void* self, UInt32* toNext, Float32* num, UInt32* den, Float64* down)
  {
    const double beat = Beats(static_cast<Au*>(self)->ts.mSampleTime);
    if (toNext) *toNext = UInt32((1.0 - (beat - std::floor(beat))) * kRate * 60.0 / kBpm);
    if (num) *num = 4.0f;
    if (den) *den = 4;
    if (down) *down = std::floor(beat / 4.0) * 4.0;
    return noErr;
  }
  static OSStatus TransportState(void* self, Boolean* playing, Boolean* changed, Float64* at,
                                 Boolean* cycling, Float64* cycleStart, Float64* cycleEnd)
  {
    if (playing) *playing = true;
    if (changed) *changed = false;
    if (at) *at = static_cast<Au*>(self)->ts.mSampleTime;
    if (cycling) *cycling = false;
    if (cycleStart) *cycleStart = 0;
    if (cycleEnd) *cycleEnd = 0;
    return noErr;
  }

  static OSStatus Input(void* self, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32,
                        UInt32 frames, AudioBufferList* io)
  {
    source((float*) io->mBuffers[0].mData, (float*) io->mBuffers[1].mData, int(frames));
    return noErr;
  }

  bool Load(const char* bundle) override
  {
    AudioComponent c = ni_au_register(bundle, nullptr);
    if (!c || AudioComponentInstanceNew(c, &au) != noErr)
      return false;
    AudioStreamBasicDescription fmt = {};
    fmt.mSampleRate = kRate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame = 4;
    fmt.mFramesPerPacket = 1;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt);
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof fmt);
    UInt32 max = kBlock;
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max, sizeof max);
    HostCallbackInfo host = {};
    host.hostUserData = this;
    host.beatAndTempoProc = BeatAndTempo;
    host.musicalTimeLocationProc = MusicalTime;
    host.transportStateProc = TransportState;
    AudioUnitSetProperty(au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &host, sizeof host);
    AURenderCallbackStruct cb = {Input, this};
    AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    return AudioUnitInitialize(au) == noErr;
  }

  void Process() override
  {
    struct { AudioBufferList list; AudioBuffer second; } abl;
    abl.list.mNumberBuffers = 2;
    abl.list.mBuffers[0] = {1, UInt32(sizeof(float) * kBlock), l.data()};
    abl.list.mBuffers[1] = {1, UInt32(sizeof(float) * kBlock), r.data()};
    AudioUnitRenderActionFlags flags = 0;
    AudioUnitRender(au, &flags, &ts, 0, kBlock, &abl.list);
    ts.mSampleTime += kBlock;
  }

  /* What an AU host does: ask for the view factory, take the view it makes,
   * and put it in a window. */
  bool Open(NSView* parent) override
  {
    AudioUnitCocoaViewInfo info;
    UInt32 size = sizeof info;
    if (AudioUnitGetProperty(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &size) != noErr)
      return false;
    Class cls = NSClassFromString((__bridge NSString*) info.mCocoaAUViewClass[0]);
    CFRelease(info.mCocoaAUViewClass[0]);
    if (info.mCocoaAUViewBundleLocation)
      CFRelease(info.mCocoaAUViewBundleLocation);
    id<AUCocoaUIBase> factory = [[cls alloc] init];
    view = [factory uiViewForAudioUnit:au withSize:parent.bounds.size];
    if (!view)
      return false;
    [parent addSubview:view];
    return true;
  }

  /* An AU's editor is closed by taking its view out of the window: iPlug2's
   * helper view hears that in removeFromSuperview. */
  void Close() override
  {
    [view removeFromSuperview];
    view = nil;
  }

  /* ClassInfo, which an AU host gets and sets on threads of its own. */
  bool Reload() override
  {
    CFPropertyListRef state = nullptr;
    UInt32 size = sizeof state;
    if (AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                             &state, &size) != noErr || !state)
      return false;
    const OSStatus err = AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo,
                                              kAudioUnitScope_Global, 0, &state, sizeof state);
    CFRelease(state);
    return err == noErr;
  }

  void Unload() override
  {
    AudioUnitUninitialize(au);
    AudioComponentInstanceDispose(au);
  }
};

struct Clap : Format
{
  const clap_plugin_entry* entry = nullptr;
  const clap_plugin* plugin = nullptr;
  const clap_plugin_gui* gui = nullptr;
  clap_host host{};
  std::vector<float> l = std::vector<float>(kBlock), r = l, ol = l, orr = l;
  int64_t steady = 0;

  static uint32_t NoEvents(const clap_input_events*) { return 0; }
  static const clap_event_header* NoEvent(const clap_input_events*, uint32_t) { return nullptr; }
  static bool Drop(const clap_output_events*, const clap_event_header*) { return true; }

  bool Load(const char* bundle) override
  {
    const std::string path(bundle);
    const std::string name = [[[NSString stringWithUTF8String:bundle] lastPathComponent]
                               stringByDeletingPathExtension].UTF8String;
    void* h = dlopen((path + "/Contents/MacOS/" + name).c_str(), RTLD_NOW | RTLD_LOCAL);
    entry = h ? (const clap_plugin_entry*) dlsym(h, "clap_entry") : nullptr;
    if (!entry || !entry->init(bundle))
      return false;
    auto* factory = (const clap_plugin_factory*) entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!factory || factory->get_plugin_count(factory) < 1)
      return false;
    host.clap_version = CLAP_VERSION;
    host.name = "editor_host";
    host.vendor = "Neon Ingvy tests";
    host.url = host.version = "";
    host.get_extension = [](const clap_host*, const char*) -> const void* { return nullptr; };
    host.request_restart = host.request_process = host.request_callback = [](const clap_host*) {};
    plugin = factory->create_plugin(factory, &host, factory->get_plugin_descriptor(factory, 0)->id);
    if (!plugin || !plugin->init(plugin) || !plugin->activate(plugin, kRate, 1, kBlock))
      return false;
    gui = (const clap_plugin_gui*) plugin->get_extension(plugin, CLAP_EXT_GUI);
    return gui != nullptr;
  }

  void Process() override
  {
    static thread_local bool started = false;
    if (!started)
      started = plugin->start_processing(plugin);
    source(l.data(), r.data(), kBlock);
    float* in[2] = {l.data(), r.data()};
    float* out[2] = {ol.data(), orr.data()};
    clap_audio_buffer ib{}, ob{};
    ib.data32 = in;
    ob.data32 = out;
    ib.channel_count = ob.channel_count = 2;
    clap_input_events ie{nullptr, NoEvents, NoEvent};
    clap_output_events oe{nullptr, Drop};
    clap_event_transport tr{};
    tr.header.size = sizeof tr;
    tr.header.type = CLAP_EVENT_TRANSPORT;
    tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
               CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_IS_PLAYING;
    tr.tempo = kBpm;
    tr.song_pos_beats = clap_beattime(Beats(double(steady)) * double(CLAP_BEATTIME_FACTOR));
    tr.tsig_num = tr.tsig_denom = 4;
    clap_process p{};
    p.transport = &tr;
    p.steady_time = steady;
    p.frames_count = kBlock;
    p.audio_inputs = &ib;
    p.audio_outputs = &ob;
    p.audio_inputs_count = p.audio_outputs_count = 1;
    p.in_events = &ie;
    p.out_events = &oe;
    plugin->process(plugin, &p);
    steady += kBlock;
  }

  bool Open(NSView* parent) override
  {
    clap_window w{CLAP_WINDOW_API_COCOA, {(__bridge void*) parent}};
    if (!gui->create(plugin, CLAP_WINDOW_API_COCOA, false) || !gui->set_parent(plugin, &w))
      return false;
    /* iPlug2 opens the window in set_parent and answers show with false
     * because it is already showing; a host calls it regardless. */
    gui->show(plugin);
    return true;
  }

  void Close() override
  {
    gui->hide(plugin);
    gui->destroy(plugin);
  }

  /* CLAP names the main thread for both. This host offers no thread_check
   * extension, so the wrapper cannot tell, as a release build would not
   * look: the load runs where it is called, as a careless host's would. */
  bool Reload() override
  {
    auto* state = (const clap_plugin_state*) plugin->get_extension(plugin, CLAP_EXT_STATE);
    if (!state)
      return false;
    Stream bytes;
    clap_ostream out{&bytes, [](const clap_ostream* s, const void* b, uint64_t n) -> int64_t {
      int32 put = 0;
      static_cast<Stream*>(s->ctx)->write(const_cast<void*>(b), int32(n), &put);
      return put;
    }};
    if (!state->save(plugin, &out))
      return false;
    bytes.pos = 0;
    clap_istream in{&bytes, [](const clap_istream* s, void* b, uint64_t n) -> int64_t {
      int32 got = 0;
      static_cast<Stream*>(s->ctx)->read(b, int32(std::min<uint64_t>(n, INT32_MAX)), &got);
      return got;
    }};
    return state->load(plugin, &in);
  }

  void Unload() override
  {
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
  }
};

/* ------------------------------------------------------------ the page */

static void Spin(double seconds)
{
  NSDate* until = [NSDate dateWithTimeIntervalSinceNow:seconds];
  while ([until timeIntervalSinceNow] > 0)
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
}

static WKWebView* FindWebView(NSView* v)
{
  if ([v isKindOfClass:[WKWebView class]])
    return (WKWebView*) v;
  for (NSView* s in v.subviews)
    if (WKWebView* w = FindWebView(s))
      return w;
  return nil;
}

/* The page's answer to `js`, as a string; "" on an error or a timeout. */
static std::string Eval(WKWebView* web, NSString* js)
{
  __block NSString* out = nil;
  __block bool done = false;
  [web evaluateJavaScript:js completionHandler:^(id result, NSError* error) {
    out = error ? nil : [NSString stringWithFormat:@"%@", result];
    done = true;
  }];
  NSDate* until = [NSDate dateWithTimeIntervalSinceNow:kPatience];
  while (!done && [until timeIntervalSinceNow] > 0)
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
  return out ? std::string(out.UTF8String) : std::string();
}

/* The Spectrogram's picture is judged once this many batches have arrived:
 * about a second of columns, at any speed. */
static constexpr int kBatches = 40;

/*
 * Every message the plugin sends from here on is counted by tag in the page,
 * wrapped around the bridge's own SAMFD, which still runs; every value
 * (SPVFD) and every display string (a SAMFD tagged with its parameter's
 * index, below 64) is noted by index. The defaults (kDefaults, tag 113) say
 * how many parameters there are: one each. With `columns`, a Spectrogram column
 * batch (tag 64) is also checked for a byte above zero past its header, its
 * columns are counted, and the arrivals of the first batch and the
 * kBatches-th are timed.
 *
 * NOT BEFORE THE EDITOR CAN SHOW THEM, so the recorder starts only once the
 * root has children AND document.readyState is 'complete'. SAMFD exists as
 * soon as the bridge's module has run, the editor's listeners only once its
 * component has, and the Spectrogram's picture only after the window's `load`:
 * that is when it builds its history, and every batch before it is dropped
 * (ui-kit/src/components/Spectrogram.jsx, onMount). Counted, those batches
 * were never the editor's to draw -- on a loaded machine the gap holds a
 * second of columns, and the canvas was then judged against columns that had
 * gone nowhere. The browser sets readyState to 'complete' in the task that
 * fires `load`, before the listeners run, so a script that sees it runs after
 * them. Every editor waits the same way; only the Spectrogram needs it.
 */
static NSString* Recorder(bool columns)
{
  return [NSString stringWithFormat:@"(() => {"
    "if (typeof globalThis.SAMFD !== 'function') return 'no';"
    "if (typeof globalThis.SPVFD !== 'function') return 'no';"
    "if (!(document.getElementById('root')?.childElementCount > 0)) return 'no';"
    "if (document.readyState !== 'complete') return 'no';"
    "if (!globalThis.__ni) {"
    "  const bridge = globalThis.SAMFD, value = globalThis.SPVFD;"
    "  globalThis.__ni = {};"
    "  globalThis.SAMFD = (tag, n, b64) => {"
    "    __ni.tags[tag] = (__ni.tags[tag] || 0) + 1;"
    "    if (tag < 64) __ni.shown.add(tag);"
    "    if (tag === 113) __ni.params = b64 ? atob(b64).split(':').length : 0;"
    "    if (%s && tag === 64) {"
    "      if (__ni.tags[64] === 1) __ni.first = performance.now();"
    "      if (__ni.tags[64] === %d) __ni.kth = performance.now();"
    "      const s = atob(b64); let colons = 0, i = 0;"
    "      for (; i < s.length && colons < 3; i++) if (s[i] === ':') colons++;"
    "      __ni.columns += parseInt(s.split(':')[1], 10) || 0;"
    "      for (; i < s.length; i++) if (s.charCodeAt(i) > 0) { __ni.loud++; break; }"
    "    }"
    "    return bridge(tag, n, b64);"
    "  };"
    "  globalThis.SPVFD = (idx, v) => { __ni.values.add(idx); return value(idx, v); };"
    "}"
    "Object.assign(__ni, { tags: {}, values: new Set(), shown: new Set(), params: -1,"
    "                      loud: 0, columns: 0, first: -1, kth: -1 });"
    "return 'yes'; })()",
    columns ? "true" : "false", kBatches];
}
static NSString* const kRecord = Recorder(false);
static NSString* const kRecordColumns = Recorder(true);

/*
 * ONE SAMPLE, TAKEN IN ONE JAVASCRIPT TASK: the batches and columns received
 * so far, how many of those batches carried the sine, what the editor drew,
 * and the milliseconds from the first batch to the kBatches-th (-1 until it
 * has come). No message can land between the counts and the canvas read, so
 * the picture is judged against exactly the columns that had arrived -- a
 * slow machine delivers fewer, and is held to fewer.
 *
 * Drawn is how many of the picture's columns hold a bright cell -- the sine is
 * near the top of the ramp, the floor and the haze far below it -- in the
 * editor's own columns, so the page's zoom and pixel ratio do not matter.
 */
static NSString* const kSample = @"(() => {"
  "const c = document.querySelector('.spectro-canvas');"
  "let drawn = 0;"
  "if (c) {"
  "  const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;"
  "  for (let x = 0; x < c.width; x++) {"
  "    for (let y = 0; y < c.height; y++) {"
  "      const i = (y * c.width + x) * 4;"
  "      if (d[i] + d[i + 1] + d[i + 2] >= 300) { drawn++; break; }"
  "    }"
  "  }"
  "  drawn = Math.round(drawn * 606 / c.width);"
  "}"
  "const ms = __ni.kth < 0 ? -1 : Math.round(__ni.kth - __ni.first);"
  "return [__ni.tags[64] || 0, __ni.columns, __ni.loud, drawn, ms].join(' '); })()";

/* A whole number the page computes; -1 if it could not. */
static int Int(WKWebView* web, NSString* js)
{
  const std::string s = Eval(web, js);
  return s.empty() ? -1 : atoi(s.c_str());
}

/* How many messages with this tag have arrived since the page was hooked. */
static int Count(WKWebView* web, int tag)
{
  return Int(web, [NSString stringWithFormat:@"String(__ni.tags[%d] || 0)", tag]);
}

/* Which editor the bundle holds, from its name. */
enum class Product { Spectrogram, TranceGate, SideChain, ListenIn, Other };

static Product ProductOf(const std::string& bundle)
{
  const std::string name = [[[NSString stringWithUTF8String:bundle.c_str()] lastPathComponent]
                            stringByDeletingPathExtension].UTF8String;
  if (name == "NISpectrogram") return Product::Spectrogram;
  if (name == "NITranceGate") return Product::TranceGate;
  if (name == "NISideChain") return Product::SideChain;
  if (name == "NIListenIn") return Product::ListenIn;
  return Product::Other;
}

/*
 * LOAD DOES NOT DECIDE A VERDICT. A machine busy with other builds delivers
 * the same messages later, so no check waits for its condition over a tight
 * wall-clock window: each waits up to kPatience, and a slow machine merely
 * takes longer to pass. What a check says on failure includes what it saw and
 * how long it waited.
 *
 * EXCEPT FOR TWO RATES, which no wait can see: a delivery that has slowed to a
 * crawl -- a throttled timer, a backlog -- still meets every condition inside
 * a minute. They are held to bounds of their own, apart from kPatience, each
 * at least ten times what an idle machine needs: it takes about a second for
 * the batches and a tenth of one for the playhead. The time measured is
 * printed either way.
 */
/* The Spectrogram's kBatches-th column batch, after its first. */
static constexpr double kBatchSpan = 10.0;
/* A playhead's third position, after its first. */
static constexpr double kGlide = 5.0;

static bool WaitFor(const std::function<bool()>& done, double* waited = nullptr)
{
  NSDate* start = [NSDate date];
  bool ok = done();
  while (!ok && -[start timeIntervalSinceNow] < kPatience)
  {
    Spin(0.02);
    ok = done();
  }
  if (waited)
    *waited = -[start timeIntervalSinceNow];
  return ok;
}

static std::string Seconds(double s)
{
  char buf[32];
  snprintf(buf, sizeof buf, "%.1f s", s);
  return buf;
}

/* A message with this tag has arrived since the page was hooked. */
static void CheckArrives(WKWebView* web, int tag, const char* what)
{
  double waited = 0.0;
  const bool ok = WaitFor([&] { return Count(web, tag) >= 1; }, &waited);
  check(ok, what, ok ? "" : "none in " + Seconds(waited));
}

/* The Spectrogram: the picture, judged over a number of received batches. */
static void CheckSpectrogram(WKWebView* web, const std::string& format)
{
  int batches = 0, columns = 0, loud = 0, drawn = 0, ms = -1;
  const auto sample = [&] {
    const std::string s = Eval(web, kSample);
    return sscanf(s.c_str(), "%d %d %d %d %d", &batches, &columns, &loud, &drawn, &ms) == 5;
  };
  double waited = 0.0;
  const bool arrived = WaitFor([&] { return sample() && batches >= kBatches; }, &waited);
  /* The canvas holds 606 columns; anything older has scrolled out. */
  const int expected = std::min(columns, 606);
  const std::string seen = format + ": " + std::to_string(batches) + " batches, " +
                           std::to_string(columns) + " columns received, " +
                           std::to_string(loud) + " loud, " + std::to_string(drawn) +
                           " drawn, after " + Seconds(waited);
  check(arrived, "column batches arrive", arrived ? std::to_string(batches) + " batches" : seen);
  /* The rate (kBatchSpan), measured in the page from the first counted
   * batch's arrival to the kBatches-th's. */
  const bool brisk = ms >= 0 && ms < kBatchSpan * 1000.0;
  const std::string within = "... the " + std::to_string(kBatches) + "th within " +
                             Seconds(kBatchSpan) + " of the first";
  check(brisk, within.c_str(),
        ms >= 0 ? format + ": " + Seconds(ms / 1000.0)
                : format + ": only " + std::to_string(batches) + " arrived");
  const bool sine = loud >= batches / 2 && loud > 0;
  check(sine, "... and carry the sine, not silence", sine ? std::to_string(loud) + " loud" : seen);
  const bool painted = expected > 0 && drawn >= expected * 9 / 10;
  check(painted, "the editor's canvas shows the columns that arrived",
        painted ? std::to_string(drawn) + " of " + std::to_string(expected) : seen);
}

/* A playhead -- an SVG line placed by the editor's clock -- moving with the
 * transport: its x must take three different values, however long the
 * machine takes to deliver them, and the third must come within kGlide of the
 * first -- still a playhead that moves, not one that jumps every few
 * seconds. */
static void CheckPlayhead(WKWebView* web, NSString* selector, const char* what,
                          const std::string& format)
{
  NSString* read = [NSString stringWithFormat:
    @"(() => { const l = document.querySelector('%@'); return l ? l.getAttribute('x1') : 'none'; })()",
    selector];
  std::vector<std::string> seen;
  NSDate* first = nil;
  double glide = 0.0, waited = 0.0;
  const bool ok = WaitFor([&] {
    const std::string x = Eval(web, read);
    if (!x.empty() && x != "none" && (seen.empty() || seen.back() != x))
    {
      seen.push_back(x);
      if (!first)
        first = [NSDate date];
      glide = -[first timeIntervalSinceNow];
    }
    return seen.size() >= 3;
  }, &waited);
  std::string trail;
  for (const auto& x : seen)
    trail += (trail.empty() ? "" : " ") + x.substr(0, 6);
  check(ok, what, ok ? "x " + trail
                     : format + ": " + std::to_string(seen.size()) + " positions (" + trail +
                         ") in " + Seconds(waited));
  const bool smooth = ok && glide < kGlide;
  const std::string within = "... the third within " + Seconds(kGlide) + " of the first";
  check(smooth, within.c_str(),
        ok ? format + ": " + Seconds(glide)
           : format + ": only " + std::to_string(seen.size()) + " positions");
}

/*
 * THE WEBVIEW, SPOKEN TO FROM ANOTHER THREAD. WebKit's API is the main
 * thread's, and a call from any other is undefined: in a host, a crash some
 * time later, not an error now. Every -[WKWebView
 * evaluateJavaScript:completionHandler:] in this process -- the one call
 * iPlug2's WebView sends everything to the page through -- passes through
 * here, swizzled before any WebView exists: from the main thread it goes on,
 * from any other it is counted and dropped, so the run lives to report it.
 * Whatever reaches the page has therefore come from the main thread.
 */
static std::atomic<int> gOffMain{0};

static void GuardWebViews()
{
  using Fn = void (*)(id, SEL, NSString*, void (^)(id, NSError*));
  const SEL sel = @selector(evaluateJavaScript:completionHandler:);
  const Method m = class_getInstanceMethod([WKWebView class], sel);
  static const Fn original = (Fn) method_getImplementation(m);
  method_setImplementation(m, imp_implementationWithBlock(
    ^(WKWebView* web, NSString* js, void (^done)(id, NSError*)) {
      if (![NSThread isMainThread])
      {
        gOffMain.fetch_add(1);
        return;
      }
      original(web, sel, js, done);
    }));
}

/*
 * A STATE LOAD ON THE HOST'S THREAD, with the editor open: the plugin's own
 * state, saved and loaded straight back through the format's calls
 * (Format::Reload) on a std::thread, while the main thread runs on, idle ticks
 * and all, as a host's does. An AU host gets and sets ClassInfo on threads of
 * its own -- auval's stress test does; VST3 and CLAP name the main thread for
 * it, which nothing makes a host keep. All three formats can do it here.
 *
 * iPlug2 reports every parameter the load set, and then that the state was
 * restored, from the loading thread (OnParamChangeUI, OnRestoreState), and its
 * WebView delegate's default sends both into the page from there. ni::WebPlugin
 * overrides both to send only from the main thread, and off it to mark the
 * editor stale for the next idle tick to send (editor::Stale). So:
 *
 *   - nothing calls the WebView off the main thread while the load runs;
 *   - afterwards every parameter's value (SPVFD) and display string reaches
 *     the page -- through the guard, so from the main thread. Nothing but that
 *     flush and a ready sends a display string for every parameter, so no
 *     other traffic can pass this.
 *
 * The parameter count is the defaults'. The Spectrogram has none, so its load
 * need only stay off the WebView.
 */
static NSString* const kRefreshed = @"(() => { let v = 0, s = 0;"
  "for (let i = 0; i < __ni.params; i++) { v += __ni.values.has(i); s += __ni.shown.has(i); }"
  "return v + ' ' + s; })()";

static void CheckReload(Format& f, WKWebView* web, const std::string& format)
{
  const int params = Int(web, @"String(__ni.params)");
  Eval(web, @"(__ni.values = new Set(), __ni.shown = new Set(), 1)");

  const int before = gOffMain.load();
  std::atomic<int> loaded{-1};
  std::thread host([&] { loaded = f.Reload() ? 1 : 0; });
  double waited = 0.0;
  WaitFor([&] { return loaded.load() >= 0; }, &waited);
  host.join();
  check(loaded == 1, "the host loads the plugin's state on a thread of its own",
        loaded == 1 ? "" : format + ": the format's call failed");
  const int off = gOffMain.load() - before;
  check(off == 0, "... and nothing speaks to the WebView off the main thread",
        off == 0 ? "" : format + ": " + std::to_string(off) + " calls");

  const char* what = "... and every value and display string reaches the page";
  if (params <= 0)
  {
    check(params == 0, what, params == 0 ? "no parameters" : format + ": no defaults arrived");
    return;
  }
  int values = 0, shown = 0;
  const bool sent = WaitFor([&] {
    return sscanf(Eval(web, kRefreshed).c_str(), "%d %d", &values, &shown) == 2 &&
           values == params && shown == params;
  }, &waited);
  const std::string of = " of " + std::to_string(params);
  check(sent, what,
        sent ? std::to_string(params) + (params == 1 ? " parameter" : " parameters")
             : format + ": " + std::to_string(values) + of + " values, " +
                 std::to_string(shown) + of + " display strings, in " + Seconds(waited));
}

/* The Listen-In: the bus's name, which a fresh instance does not have. It is
 * sent empty in reply to the ready, and iPlug2 once printed an empty payload
 * as "(null)", which the field then showed as the bus's name
 * (ni::WebPlugin::Send). Bracketed, so an empty field and a failed read
 * differ. */
static void CheckListenIn(WKWebView* web, const std::string& format)
{
  CheckArrives(web, 64, "... and the bus's state");
  CheckArrives(web, 96, "... and its name");
  const std::string name = Eval(web,
    @"(() => { const f = document.querySelector('input.name-field');"
     "return f ? '[' + f.value + ']' : 'none'; })()");
  check(name == "[]", "a fresh bus's name field is empty, not \"(null)\"", format + ": " + name);
}

/* One open of the editor, and what reached it. */
static void Session(Format& f, Product product, const std::string& format, NSWindow* window,
                    const char* label)
{
  printf(" %s\n", label);
  check(f.Open(window.contentView), "the host opens the editor");
  WKWebView* web = FindWebView(window.contentView);
  check(web != nil, "the editor is a WKWebView");
  if (!web)
    return;

  /* The bridge exists once the page's module has run, the editor once its
   * component has. */
  double waited = 0.0;
  const bool recording = WaitFor([&] {
    return Eval(web, product == Product::Spectrogram ? kRecordColumns : kRecord) == "yes";
  }, &waited);
  check(recording, "the page's bridge is up", recording ? "" : format + ": not after " + Seconds(waited));
  if (!recording)
    return;

  /* The handshake, asked again from the page: answered only if the shell
   * knows an editor is open. */
  Eval(web, @"IPlugSendMsg({msg: 'SAMFUI', msgTag: 120, ctrlTag: -1, data: ''}), 1");

  CheckArrives(web, 113, "a ready is answered with the defaults");
  switch (product)
  {
    case Product::Spectrogram:
      CheckArrives(web, 69, "... and the session");
      CheckArrives(web, 65, "... and the axis");
      CheckSpectrogram(web, format);
      break;
    case Product::TranceGate:
      CheckPlayhead(web, @"line.playhead", "the pattern's playhead moves with the transport", format);
      break;
    case Product::SideChain:
      CheckPlayhead(web, @"line.sweep", "the shaper's sweep moves with the transport", format);
      break;
    case Product::ListenIn: CheckListenIn(web, format); break;
    case Product::Other: break;
  }
  CheckReload(f, web, format);
  CheckArrives(web, 112, "the ground rings on the transport's beat");

  f.Close();
  Spin(0.2);
}

int main(int argc, char** argv)
{
  if (argc < 3)
  {
    fprintf(stderr, "usage: editor_host <vst3|au|clap> <bundle>\n");
    return 2;
  }
  const std::string kind = argv[1];

  @autoreleasepool
  {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    GuardWebViews();

    Vst3 vst3;
    Au au;
    Clap clap;
    Format* f = kind == "vst3" ? (Format*) &vst3 : kind == "au" ? (Format*) &au
                : kind == "clap" ? (Format*) &clap : nullptr;
    if (!f)
    {
      fprintf(stderr, "editor_host: unknown format %s\n", kind.c_str());
      return 2;
    }
    printf("editor_host %s %s\n", kind.c_str(), argv[2]);
    if (!f->Load(argv[2]))
    {
      printf("  FAIL: could not load and activate %s\n", argv[2]);
      return 1;
    }

    /* The render thread, paced at the session's rate. */
    static std::atomic<bool> stop{false};
    static Format* rendering = f;
    pthread_t render;
    pthread_create(&render, nullptr, [](void*) -> void* {
      while (!stop.load())
      {
        rendering->Process();
        usleep(useconds_t(kBlock * 1e6 / kRate));
      }
      return nullptr;
    }, nullptr);

    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, 1000, 800)
                                                   styleMask:NSWindowStyleMaskTitled
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.releasedWhenClosed = NO;
    [window orderFrontRegardless];

    const Product product = ProductOf(argv[2]);
    Session(*f, product, kind, window, "first open");
    Session(*f, product, kind, window, "closed and opened again");

    stop.store(true);
    pthread_join(render, nullptr);
    [window orderOut:nil];
    f->Unload();
  }

  /* The guard saw every thread, the render thread's included, from the first
   * open to the unload. */
  printf(" the whole run\n");
  const int off = gOffMain.load();
  check(off == 0, "nothing spoke to the WebView off the main thread",
        off == 0 ? "" : kind + ": " + std::to_string(off) + " calls");

  printf("%s\n", gFails ? "FAIL" : "ok");
  return gFails ? 1 : 0;
}
