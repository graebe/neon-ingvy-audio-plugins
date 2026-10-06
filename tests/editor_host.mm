/*
 * The editors, opened by a real host, in each format.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 *   editor_host <vst3|au|clap> <bundle> [seconds]
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
 *   - the ground hears the kick: the shell's detector is active and its
 *     messages are sent;
 *
 *   the Spectrogram
 *   - the session and the axis answer the ready too;
 *   - column batches arrive whose bytes are not all zero -- a fresh instance's
 *     receiver is fed and drained, and the real encoder's payloads cross;
 *   - the editor's own decoder and canvas put every one of those columns on
 *     the visible canvas;
 *
 *   the Trance Gate and the Side-Chain
 *   - the playhead (the pattern's line; the shaper's sweep) moves with the
 *     transport.
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

#include "au_bundle.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "clap/clap.h"

#include <dlfcn.h>
#include <pthread.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static constexpr double kRate = 48000.0;
static constexpr int kBlock = 512;

static int gFails = 0;
static void check(bool ok, const char* what, const std::string& detail = "")
{
  printf("  %-60s %s%s%s%s\n", what, ok ? "ok" : "FAIL", detail.empty() ? "" : " (",
         detail.c_str(), detail.empty() ? "" : ")");
  if (!ok)
    gFails++;
}

/* The source: a sine for the picture and a kick every half second for the
 * ground's detector. */
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
  virtual void Unload() = 0;
};

using namespace Steinberg;

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
  NSDate* until = [NSDate dateWithTimeIntervalSinceNow:5];
  while (!done && [until timeIntervalSinceNow] > 0)
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
  return out ? std::string(out.UTF8String) : std::string();
}

/* Every message the plugin sends from here on is counted by tag in the page,
 * wrapped around the bridge's own SAMFD, which still runs. With `columns`, a
 * Spectrogram column batch (tag 64) is also checked for a byte above zero past
 * its header, and its columns are counted. */
static NSString* Recorder(bool columns)
{
  return [NSString stringWithFormat:@"(() => {"
    "if (typeof globalThis.SAMFD !== 'function') return 'no';"
    "if (!globalThis.__ni) {"
    "  const bridge = globalThis.SAMFD;"
    "  globalThis.__ni = { tags: {}, loud: 0, columns: 0 };"
    "  globalThis.SAMFD = (tag, n, b64) => {"
    "    __ni.tags[tag] = (__ni.tags[tag] || 0) + 1;"
    "    if (%s && tag === 64) {"
    "      const s = atob(b64); let colons = 0, i = 0;"
    "      for (; i < s.length && colons < 3; i++) if (s[i] === ':') colons++;"
    "      __ni.columns += parseInt(s.split(':')[1], 10) || 0;"
    "      for (; i < s.length; i++) if (s.charCodeAt(i) > 0) { __ni.loud++; break; }"
    "    }"
    "    return bridge(tag, n, b64);"
    "  };"
    "}"
    "__ni.tags = {}; __ni.loud = 0; __ni.columns = 0; return 'yes'; })()",
    columns ? "true" : "false"];
}
static NSString* const kRecord = Recorder(false);
static NSString* const kRecordColumns = Recorder(true);

/*
 * What the editor drew: how many of the picture's columns hold a bright cell
 * -- the sine is near the top of the ramp, and the floor and the haze are far
 * below it -- in the editor's own columns, so the page's zoom and pixel ratio
 * do not matter. Every column that arrived should be on screen, not only the
 * ones a repaint happened to catch.
 */
static NSString* const kPainted = @"(() => {"
  "const c = document.querySelector('.spectro-canvas'); if (!c) return 0;"
  "const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;"
  "let painted = 0;"
  "for (let x = 0; x < c.width; x++) {"
  "  for (let y = 0; y < c.height; y++) {"
  "    const i = (y * c.width + x) * 4;"
  "    if (d[i] + d[i + 1] + d[i + 2] >= 300) { painted++; break; }"
  "  }"
  "}"
  "return Math.round(painted * 606 / c.width); })()";

static int Count(WKWebView* web, int tag)
{
  const std::string s = Eval(web, [NSString stringWithFormat:@"String(__ni.tags[%d] || 0)", tag]);
  return s.empty() ? -1 : atoi(s.c_str());
}

/* Which editor the bundle holds, from its name. */
enum class Product { Spectrogram, TranceGate, SideChain, Other };

static Product ProductOf(const std::string& bundle)
{
  const std::string name = [[[NSString stringWithUTF8String:bundle.c_str()] lastPathComponent]
                            stringByDeletingPathExtension].UTF8String;
  if (name == "NISpectrogram") return Product::Spectrogram;
  if (name == "NITranceGate") return Product::TranceGate;
  if (name == "NISideChain") return Product::SideChain;
  return Product::Other;
}

/* The Spectrogram: the reply to ready, and the picture. */
static void CheckSpectrogram(WKWebView* web, double seconds)
{
  check(Count(web, 69) >= 1, "... and the session");
  check(Count(web, 65) >= 1, "... and the axis");
  const int cols = Count(web, 64);
  const int loud = atoi(Eval(web, @"String(__ni.loud)").c_str());
  check(cols >= int(seconds * 20), "column batches arrive (about 47 columns a second)",
        std::to_string(cols) + " batches");
  check(loud >= cols / 2 && loud > 0, "... and carry the sine, not silence",
        std::to_string(loud) + " with a byte above the floor");
  /* Read straight after a batch could have landed: the newest column or two
   * may not have been drawn yet, nothing more. */
  const int columns = atoi(Eval(web, @"String(__ni.columns)").c_str());
  const int painted = atoi(Eval(web, kPainted).c_str());
  check(columns > 0 && painted >= columns * 9 / 10, "the editor's canvas shows every column that arrived",
        std::to_string(painted) + " of " + std::to_string(columns) + " drawn");
}

/* A playhead -- an SVG line placed by the editor's clock -- moving with the
 * transport: its x read a few times over a second must take several values. */
static void CheckPlayhead(WKWebView* web, NSString* selector, const char* what)
{
  NSString* read = [NSString stringWithFormat:
    @"(() => { const l = document.querySelector('%@'); return l ? l.getAttribute('x1') : 'none'; })()",
    selector];
  std::vector<std::string> seen;
  for (int i = 0; i < 5; i++)
  {
    const std::string x = Eval(web, read);
    if (seen.empty() || seen.back() != x)
      seen.push_back(x);
    Spin(0.23);
  }
  const bool drawn = !seen.empty() && seen.front() != "none" && !seen.front().empty();
  std::string trail;
  for (const auto& x : seen)
    trail += (trail.empty() ? "" : " ") + x.substr(0, 6);
  check(drawn && seen.size() >= 3, what, "x " + trail);
}

/* One open of the editor, and what reached it. */
static void Session(Format& f, Product product, NSWindow* window, const char* label, double seconds)
{
  printf(" %s\n", label);
  check(f.Open(window.contentView), "the host opens the editor");
  WKWebView* web = FindWebView(window.contentView);
  check(web != nil, "the editor is a WKWebView");
  if (!web)
    return;

  /* The bridge exists once the page's module has run. */
  bool recording = false;
  for (int i = 0; i < 1000 && !recording; i++)
  {
    Spin(0.01);
    recording = Eval(web, product == Product::Spectrogram ? kRecordColumns : kRecord) == "yes";
  }
  check(recording, "the page's bridge is up");
  if (!recording)
    return;

  /* The handshake, asked again from the page: answered only if the shell
   * knows an editor is open. */
  Eval(web, @"IPlugSendMsg({msg: 'SAMFUI', msgTag: 120, ctrlTag: -1, data: ''}), 1");
  Spin(seconds);

  check(Count(web, 113) >= 1, "a ready is answered with the defaults");
  switch (product)
  {
    case Product::Spectrogram: CheckSpectrogram(web, seconds); break;
    case Product::TranceGate:
      CheckPlayhead(web, @"line.playhead", "the pattern's playhead moves with the transport");
      break;
    case Product::SideChain:
      CheckPlayhead(web, @"line.sweep", "the shaper's sweep moves with the transport");
      break;
    case Product::Other: break;
  }
  check(Count(web, 112) >= 1, "the ground hears the kick");

  f.Close();
  Spin(0.2);
}

int main(int argc, char** argv)
{
  if (argc < 3)
  {
    fprintf(stderr, "usage: editor_host <vst3|au|clap> <bundle> [seconds]\n");
    return 2;
  }
  const std::string kind = argv[1];
  const double seconds = argc > 3 ? atof(argv[3]) : 2.0;

  @autoreleasepool
  {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

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
    Session(*f, product, window, "first open", seconds);
    Session(*f, product, window, "closed and opened again", seconds);

    stop.store(true);
    pthread_join(render, nullptr);
    [window orderOut:nil];
    f->Unload();
  }

  printf("%s\n", gFails ? "FAIL" : "ok");
  return gFails ? 1 : 0;
}
