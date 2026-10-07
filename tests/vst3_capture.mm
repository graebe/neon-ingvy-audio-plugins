// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the iPlug2 VST3 builds save, captured as fixtures for the builds that
 * replace them.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Torben Gräber
 *
 *   vst3_capture <fixtures-dir> <bundle.vst3>...
 *   vst3_capture --check <fixtures-dir> <bundle.vst3>...
 *
 * WHY. Every Live set that holds one of these plugins holds what its VST3
 * wrote: the class ID it was made from, IComponent::getState's bytes, the
 * controller's, and automation keyed by parameter ID. The JUCE builds have to
 * read all of that, and the only honest specification of it is the shipped
 * build itself -- so this asks the shipped build, through the same calls Live
 * makes, and writes down the answers (tests/fixtures/iplug2/README.md lists the
 * files, FORMAT.md is the byte layout they follow).
 *
 * HOW, for each bundle, one at a time, in one process (as Live holds all four):
 *
 *   - the factory: its class, the class ID's bytes as a host receives them, the
 *     class flags (single component or split) -> <fixtures-dir>/ids.json;
 *   - the parameters: every ParameterInfo, bypass and MIDI CCs included ->
 *     <product>/parameters.json;
 *   - each scenario: a fresh instance, driven the way a DAW drives it -- values
 *     through IEditController::setParamNormalized AND the next block's
 *     IParameterChanges, the plugin's own performEdits fed back the same way,
 *     the editor's messages through the real WebView editor (a pattern, a
 *     Spectrogram's session and a Listen-In's name are not parameters, so that
 *     is the only door a user has to them) -- then the states:
 *       <scenario>.component.bin   IComponent::getState
 *       <scenario>.controller.bin  IEditController::getState
 *       <scenario>.json            what the host reads back, and the component
 *                                  state decoded per FORMAT.md
 *
 * AND THEN IT CHECKS ITSELF. Each state is loaded into a second, fresh instance
 * the way a host reopens a set (component setState, setComponentState,
 * controller setState); that instance must report the same parameters and
 * save the same bytes. A capture that does not survive its own reload is not
 * a fixture. The decoder that writes "decoded" refuses any byte FORMAT.md does
 * not account for, so the document and the bytes cannot drift apart silently.
 *
 * --check loads the committed fixtures into the given build instead and
 * compares the parameters only: it is the question "does this build still
 * open the sets the iPlug2 builds saved", which a later engine may answer yes
 * while saving different bytes.
 *
 * WHAT IT NEEDS: the capture opens editors, so a logged-in macOS session, as
 * editor_host does. --check opens none.
 */
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <dlfcn.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace Steinberg;
using Bytes = std::vector<uint8_t>;

static constexpr double kSampleRate = 48000.0;
static constexpr int kBlock = 512;
static constexpr double kBpm = 120.0;
/* The longest any condition is waited for: it bounds a hang, it is not a
 * speed requirement. */
static constexpr double kPatience = 60.0;

/* iPlug2's VST3 parameter IDs past the plugin's own (IPlugConstants.h). */
static constexpr Vst::ParamID kBypassId = 65536;
static constexpr Vst::ParamID kFirstMidiCcId = 65538;

[[noreturn]] static void Fail(const std::string& why)
{
  fprintf(stderr, "vst3_capture: %s\n", why.c_str());
  exit(1);
}

static void Require(bool ok, const std::string& what)
{
  if (!ok)
    Fail(what);
}

/* ------------------------------------------------------------------ JSON */

/*
 * A JSON value whose object keys keep the order they were added in, so a
 * fixture reads top to bottom the way FORMAT.md does. Reals are written with
 * 17 significant digits: a value read back is the value the plugin reported,
 * not a rounding of it.
 */
struct Json
{
  enum class Kind { Null, Bool, Int, Real, Text, List, Map };
  Kind kind = Kind::Null;
  bool flag = false;
  long long integer = 0;
  double real = 0.0;
  std::string text;
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> fields;

  static Json Bool(bool b) { Json j; j.kind = Kind::Bool; j.flag = b; return j; }
  static Json Int(long long v) { Json j; j.kind = Kind::Int; j.integer = v; return j; }
  static Json Real(double v) { Json j; j.kind = Kind::Real; j.real = v; return j; }
  static Json Text(std::string s) { Json j; j.kind = Kind::Text; j.text = std::move(s); return j; }
  static Json List() { Json j; j.kind = Kind::List; return j; }
  static Json Map() { Json j; j.kind = Kind::Map; return j; }

  Json& Add(const std::string& key, Json v)
  {
    fields.emplace_back(key, std::move(v));
    return *this;
  }
  Json& Push(Json v)
  {
    items.push_back(std::move(v));
    return *this;
  }
  bool Scalar() const { return kind != Kind::List && kind != Kind::Map; }
};

static void EmitText(const std::string& s, std::string& out)
{
  out += '"';
  for (unsigned char c : s)
  {
    if (c == '"' || c == '\\')
    {
      out += '\\';
      out += char(c);
    }
    else if (c == '\n')
      out += "\\n";
    else if (c < 0x20)
    {
      char esc[8];
      snprintf(esc, sizeof esc, "\\u%04x", c);
      out += esc;
    }
    else
      out += char(c);
  }
  out += '"';
}

static void Emit(const Json& j, std::string& out, int depth)
{
  const std::string pad(size_t(depth + 1) * 2, ' '), close(size_t(depth) * 2, ' ');
  switch (j.kind)
  {
    case Json::Kind::Null: out += "null"; break;
    case Json::Kind::Bool: out += j.flag ? "true" : "false"; break;
    case Json::Kind::Int: out += std::to_string(j.integer); break;
    case Json::Kind::Real:
    {
      char num[40];
      snprintf(num, sizeof num, "%.17g", j.real);
      out += num;
      break;
    }
    case Json::Kind::Text: EmitText(j.text, out); break;
    case Json::Kind::List:
    {
      /* A list of numbers stays on one line; a list of objects takes one each. */
      bool flat = true;
      for (const Json& i : j.items)
        flat = flat && i.Scalar();
      out += '[';
      for (size_t i = 0; i < j.items.size(); i++)
      {
        out += i ? (flat ? ", " : ",\n") : (flat ? "" : "\n");
        if (!flat) out += pad;
        Emit(j.items[i], out, depth + 1);
      }
      if (!flat && !j.items.empty()) out += "\n" + close;
      out += ']';
      break;
    }
    case Json::Kind::Map:
    {
      out += "{\n";
      for (size_t i = 0; i < j.fields.size(); i++)
      {
        out += pad;
        EmitText(j.fields[i].first, out);
        out += ": ";
        Emit(j.fields[i].second, out, depth + 1);
        out += i + 1 < j.fields.size() ? ",\n" : "\n";
      }
      out += close + "}";
      break;
    }
  }
}

static void WriteFile(const std::string& path, const void* data, size_t size)
{
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  Require(f && f.write(static_cast<const char*>(data), std::streamsize(size)) && f.flush(),
          "cannot write " + path);
}

static void WriteJson(const std::string& path, const Json& j)
{
  std::string out;
  Emit(j, out, 0);
  out += '\n';
  WriteFile(path, out.data(), out.size());
}

static Bytes ReadFile(const std::string& path)
{
  std::ifstream f(path, std::ios::binary);
  Require(bool(f), "cannot read " + path);
  return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static std::string Hex(const void* data, size_t n)
{
  std::string s;
  char b[4];
  for (size_t i = 0; i < n; i++)
  {
    snprintf(b, sizeof b, "%02X", static_cast<const uint8_t*>(data)[i]);
    s += b;
  }
  return s;
}

static std::string Utf8(const Vst::TChar* s, size_t cap)
{
  size_t n = 0;
  while (n < cap && s[n])
    n++;
  NSString* str = [NSString stringWithCharacters:reinterpret_cast<const unichar*>(s) length:n];
  return str.UTF8String ? std::string(str.UTF8String) : std::string();
}

/* --------------------------------------------------------- the run loop */

/* iPlug2's idle timer, the WebView and its script messages all live on the
 * main run loop; everything here runs on the main thread and turns it. */
static void Spin(double seconds)
{
  NSDate* until = [NSDate dateWithTimeIntervalSinceNow:seconds];
  while ([until timeIntervalSinceNow] > 0)
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
}

static bool WaitFor(const std::function<bool()>& done)
{
  NSDate* start = [NSDate date];
  while (!done())
  {
    if (-[start timeIntervalSinceNow] > kPatience)
      return false;
    Spin(0.02);
  }
  return true;
}

/* ------------------------------------------------------------- the host */

/*
 * THE HOST'S SIDE OF AN EDIT. A plugin that moves its own parameter -- the
 * Trance Gate following a slot switch -- tells the host through performEdit,
 * and a DAW sends that value to the processor with its next block. Recorded
 * here and replayed by Instance::Process, as a DAW would.
 */
class Handler final : public Vst::IComponentHandler
{
public:
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
  {
    if (FUnknownPrivate::iidEqual(iid, Vst::IComponentHandler::iid) ||
        FUnknownPrivate::iidEqual(iid, FUnknown::iid))
    {
      *obj = this;
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  /* Owned by its Instance, which outlives every call into it. */
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  tresult PLUGIN_API beginEdit(Vst::ParamID) override { return kResultOk; }
  tresult PLUGIN_API performEdit(Vst::ParamID id, Vst::ParamValue v) override
  {
    pending.emplace_back(id, v);
    edits++;
    return kResultOk;
  }
  tresult PLUGIN_API endEdit(Vst::ParamID) override { return kResultOk; }
  tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }

  std::vector<std::pair<Vst::ParamID, Vst::ParamValue>> pending;
  long edits = 0;
};

/* A bundle's factory, found the way a host finds it on macOS. */
struct Module
{
  std::string bundle;
  std::string name;   /* NITranceGate: the bundle's name, which the tool keys on */
  IPluginFactory* factory = nullptr;

  void Open(const std::string& path)
  {
    bundle = path;
    while (!bundle.empty() && bundle.back() == '/')
      bundle.pop_back();
    name = [[[NSString stringWithUTF8String:bundle.c_str()] lastPathComponent]
             stringByDeletingPathExtension].UTF8String;
    void* h = dlopen((bundle + "/Contents/MacOS/" + name).c_str(), RTLD_NOW | RTLD_LOCAL);
    Require(h != nullptr, "cannot load " + bundle + ": " + (dlerror() ?: "?"));
    auto entry = (bool (*)(CFBundleRef)) dlsym(h, "bundleEntry");
    auto get = (IPluginFactory * (*)()) dlsym(h, "GetPluginFactory");
    Require(entry && get, bundle + " has no bundleEntry/GetPluginFactory");
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      nullptr, (const UInt8*) bundle.c_str(), CFIndex(bundle.size()), true);
    CFBundleRef cfb = CFBundleCreate(nullptr, url);
    CFRelease(url);
    Require(entry(cfb), bundle + ": bundleEntry failed");
    factory = get();
    Require(factory != nullptr, bundle + ": no factory");
  }

  /* The audio effect class: its index and info. */
  PClassInfo2 Effect(int* index = nullptr) const
  {
    FUnknownPtr<IPluginFactory2> f2(factory);
    Require(f2 != nullptr, name + ": the factory has no IPluginFactory2");
    for (int32 i = 0; i < factory->countClasses(); i++)
    {
      PClassInfo2 ci;
      if (f2->getClassInfo2(i, &ci) == kResultOk && !strcmp(ci.category, kVstAudioEffectClass))
      {
        if (index) *index = i;
        return ci;
      }
    }
    Fail(name + ": no " + std::string(kVstAudioEffectClass));
  }
};

/*
 * ONE PLUGIN INSTANCE, DRIVEN AS A DAW DRIVES IT: initialised, its main buses
 * active, processing at 48 kHz in blocks of 512 under a transport playing at
 * 120 BPM in 4/4 -- on the main thread, so a block, an idle tick and an editor
 * message happen in the order this file writes them.
 */
class Instance
{
public:
  Vst::IComponent* comp = nullptr;
  Vst::IAudioProcessor* proc = nullptr;
  Vst::IEditController* ctrl = nullptr;
  Handler handler;

  void Create(const Module& m)
  {
    const PClassInfo2 ci = m.Effect();
    Require(m.factory->createInstance(ci.cid, Vst::IComponent::iid, (void**) &comp) == kResultOk && comp,
            m.name + ": createInstance failed");
    Require(comp->initialize(nullptr) == kResultOk, m.name + ": initialize failed");
    comp->queryInterface(Vst::IAudioProcessor::iid, (void**) &proc);
    /* A single-component plugin is its own controller; a split one would
     * answer no here and name a controller class instead (ids.json). */
    comp->queryInterface(Vst::IEditController::iid, (void**) &ctrl);
    Require(proc && ctrl, m.name + ": not a single-component IAudioProcessor + IEditController");
    ctrl->setComponentHandler(&handler);
    Vst::ProcessSetup setup{Vst::kRealtime, Vst::kSample32, kBlock, kSampleRate};
    proc->setupProcessing(setup);
    comp->activateBus(Vst::kAudio, Vst::kInput, 0, true);
    comp->activateBus(Vst::kAudio, Vst::kOutput, 0, true);
    comp->setActive(true);
    proc->setProcessing(true);
    ctx.sampleRate = kSampleRate;
    ctx.tempo = kBpm;
    ctx.timeSigNumerator = ctx.timeSigDenominator = 4;
    ctx.state = Vst::ProcessContext::kPlaying | Vst::ProcessContext::kTempoValid |
                Vst::ProcessContext::kTimeSigValid | Vst::ProcessContext::kProjectTimeMusicValid;
  }

  void Destroy()
  {
    if (!comp)
      return;
    proc->setProcessing(false);
    comp->setActive(false);
    ctrl->setComponentHandler(nullptr);
    comp->terminate();
    ctrl->release();
    proc->release();
    comp->release();
    comp = nullptr;
    proc = nullptr;
    ctrl = nullptr;
  }

  /* One block, carrying whatever the host has queued for the processor. */
  void Process()
  {
    for (const auto& [id, v] : handler.pending)
      queued.emplace_back(id, v);
    handler.pending.clear();
    Vst::ParameterChanges changes(int32(queued.size()) + 1);
    for (const auto& [id, v] : queued)
    {
      int32 at = 0, point = 0;
      if (Vst::IParamValueQueue* q = changes.addParameterData(id, at))
        q->addPoint(0, v, point);
    }
    queued.clear();

    for (int i = 0; i < kBlock; i++, t++)
      l[size_t(i)] = r[size_t(i)] = float(0.2 * std::sin(2.0 * M_PI * 220.0 * double(t) / kSampleRate));
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
    pd.inputParameterChanges = &changes;
    pd.processContext = &ctx;
    proc->process(pd);
    ctx.projectTimeSamples += kBlock;
    ctx.projectTimeMusic = double(ctx.projectTimeSamples) / kSampleRate * (kBpm / 60.0);
  }

  /*
   * A VALUE AS A DAW SETS ONE, in the parameter's own units: the controller
   * told, and the processor given it with the next block. Both, because a
   * VST3 host does both and a plugin is entitled to listen to either.
   */
  void Set(Vst::ParamID id, double plain)
  {
    SetNormalized(id, ctrl->plainParamToNormalized(id, plain));
  }

  void SetNormalized(Vst::ParamID id, double normalized)
  {
    ctrl->setParamNormalized(id, normalized);
    queued.emplace_back(id, normalized);
  }

  /*
   * UNTIL NOTHING MOVES: blocks and idle ticks, until a round in which the
   * plugin asked the host for no edit. A slot switch publishes at a block's
   * end, the idle tick hands the new slot's values to the host, and the host
   * sends them back with the next block -- three hops, all of which must have
   * happened before a state is worth saving.
   */
  void Settle()
  {
    for (int round = 0, quiet = 0; quiet < 3; round++)
    {
      Require(round < 200, "the plugin never settled: it keeps editing its own parameters");
      const long before = handler.edits;
      Process();
      Spin(0.03);
      Process();
      quiet = handler.edits == before ? quiet + 1 : 0;
    }
  }

  static Bytes Save(const std::function<tresult(IBStream*)>& get, const char* what)
  {
    MemoryStream s;
    Require(get(&s) == kResultOk, std::string(what) + " getState failed");
    return Bytes(s.getData(), s.getData() + s.getSize());
  }
  Bytes ComponentState() { return Save([&](IBStream* s) { return comp->getState(s); }, "IComponent"); }
  Bytes ControllerState() { return Save([&](IBStream* s) { return ctrl->getState(s); }, "IEditController"); }

  /* A set reopened: the component's state, the controller told of it, then the
   * controller's own -- the order a VST3 host restores in. */
  void Load(const Bytes& component, const Bytes& controller)
  {
    MemoryStream c((void*) component.data(), TSize(component.size()));
    Require(comp->setState(&c) == kResultOk, "IComponent::setState refused the state");
    c.seek(0, IBStream::kIBSeekSet, nullptr);
    ctrl->setComponentState(&c);
    MemoryStream e((void*) controller.data(), TSize(controller.size()));
    Require(ctrl->setState(&e) == kResultOk, "IEditController::setState refused the state");
  }

private:
  std::vector<std::pair<Vst::ParamID, Vst::ParamValue>> queued;
  Vst::ProcessContext ctx{};
  long t = 0;
  std::vector<float> l = std::vector<float>(kBlock), r = l, ol = l, orr = l;
};

/* ----------------------------------------------------------- the editor */

static WKWebView* FindWebView(NSView* v)
{
  if ([v isKindOfClass:[WKWebView class]])
    return (WKWebView*) v;
  for (NSView* s in v.subviews)
    if (WKWebView* w = FindWebView(s))
      return w;
  return nil;
}

/* The page's answer to `js` as a string, "" on an error. */
static std::string Eval(WKWebView* web, NSString* js)
{
  /* The block writes, the wait reads: one flag both can reach, as a block
   * cannot share a __block variable with a lambda. */
  __block NSString* out = nil;
  bool done = false;
  bool* answered = &done;
  [web evaluateJavaScript:js completionHandler:^(id result, NSError* error) {
    out = error ? nil : [NSString stringWithFormat:@"%@", result];
    *answered = true;
  }];
  Require(WaitFor([&] { return done; }), "the editor's page stopped answering");
  return out ? std::string(out.UTF8String) : std::string();
}

/*
 * THE WINDOW'S SIDE OF A RESIZE. A host gives every view an IPlugFrame before
 * attaching it, and an editor that asks for a new size -- the Trance Gate's
 * grows with Length -- calls it without checking: iPlug2 dereferences it.
 */
class Frame final : public IPlugFrame
{
public:
  NSWindow* window = nil;

  tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* size) override
  {
    if (!view || !size)
      return kInvalidArgument;
    [window setContentSize:NSMakeSize(size->getWidth(), size->getHeight())];
    return view->onSize(size);
  }
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
  {
    if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid))
    {
      *obj = this;
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  /* Owned by its Editor, which detaches it before going. */
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
};

/*
 * THE EDITOR, FOR WHAT ONLY THE EDITOR CAN SET. The plugin's real WebView
 * editor, opened through IPlugView in a window, and spoken to the way its own
 * page speaks: IPlugSendMsg with a SAMFUI message, the payload base64 as
 * ui-kit's sendMessage encodes it. The plugin cannot tell these from a user's
 * clicks, because there is no difference.
 */
class Editor
{
public:
  void Open(Vst::IEditController* ctrl, const std::string& product)
  {
    view = ctrl->createView(Vst::ViewType::kEditor);
    Require(view != nullptr, product + ": no editor");
    ViewRect rect;
    view->getSize(&rect);
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, rect.getWidth(), rect.getHeight())
                                         styleMask:NSWindowStyleMaskTitled
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    window.releasedWhenClosed = NO;
    [window orderFrontRegardless];
    frame.window = window;
    view->setFrame(&frame);
    Require(view->attached((__bridge void*) window.contentView, kPlatformTypeNSView) == kResultOk,
            product + ": the editor would not attach");
    web = FindWebView(window.contentView);
    Require(web != nil, product + ": the editor is not a WKWebView");
    /* The bridge exists once the page's module has run, the editor once its
     * component has rendered: a message before then would reach a plugin whose
     * page has not yet asked for anything, which a user cannot do either. */
    Require(WaitFor([&] {
              return Eval(web, @"(typeof globalThis.IPlugSendMsg === 'function' && "
                                "document.getElementById('root')?.childElementCount > 0) ? 'up' : 'no'") == "up";
            }),
            product + ": the editor's page never came up");
  }

  void Send(int tag, const std::string& text)
  {
    NSData* raw = [NSData dataWithBytes:text.data() length:text.size()];
    NSString* js = [NSString stringWithFormat:
      @"IPlugSendMsg({msg: 'SAMFUI', msgTag: %d, ctrlTag: -1, data: '%@'}), 'sent'", tag,
      [raw base64EncodedStringWithOptions:0]];
    Require(Eval(web, js) == "sent", "the editor's page would not send");
    /* The script message and this answer travel the same connection, in
     * order: once it is back, the message has reached the main run loop. */
    Eval(web, @"'flushed'");
    Spin(0.02);
  }

  void Close()
  {
    if (!view)
      return;
    view->removed();
    view->setFrame(nullptr);
    view->release();
    view = nullptr;
    [window orderOut:nil];
    frame.window = nil;
    window = nil;
    web = nil;
  }

private:
  Frame frame;
  IPlugView* view = nullptr;
  NSWindow* window = nil;
  WKWebView* web = nil;
};

/* ----------------------------------------------------------- the format */

/*
 * THE COMPONENT STATE, DECODED PER FORMAT.md -- and nothing it does not
 * describe. `nParams` doubles, then IByteChunk strings to the end of the body,
 * then the int32 bypass the VST3 wrapper appends. A chunk without the NIst
 * header is refused here: every build that shipped as a VST3 wrote one.
 */
struct Decoded
{
  int32_t version = 0;
  int32_t bodyBytes = 0;
  std::vector<double> params;
  std::vector<std::string> strings;
  int32_t bypass = 0;
};

static Decoded Decode(const Bytes& b, int nParams, const std::string& what)
{
  static const uint8_t kMagic[8] = {'N', 'I', 's', 't', 0x00, 0x00, 0xF8, 0x7F};
  Decoded d;
  size_t pos = 0;
  const auto need = [&](size_t n, const char* field) {
    Require(pos + n <= b.size(), what + ": runs out inside " + field);
  };
  need(16, "the header");
  Require(memcmp(b.data(), kMagic, 8) == 0, what + ": no NIst header");
  memcpy(&d.version, b.data() + 8, 4);
  memcpy(&d.bodyBytes, b.data() + 12, 4);
  pos = 16;
  const size_t end = 16 + size_t(d.bodyBytes);
  Require(d.bodyBytes >= 0 && end <= b.size(), what + ": the header's size runs past the stream");
  for (int i = 0; i < nParams; i++)
  {
    need(8, "the parameters");
    double v = 0.0;
    memcpy(&v, b.data() + pos, 8);
    d.params.push_back(v);
    pos += 8;
  }
  while (pos < end)
  {
    need(4, "a string's length");
    int32_t len = 0;
    memcpy(&len, b.data() + pos, 4);
    pos += 4;
    Require(len >= 0 && pos + size_t(len) <= end, what + ": a string runs past the body");
    d.strings.emplace_back(reinterpret_cast<const char*>(b.data() + pos), size_t(len));
    pos += size_t(len);
  }
  Require(pos == end, what + ": the body does not end where the header says");
  Require(b.size() == end + 4, what + ": expected exactly the int32 bypass after the body, found " +
                                   std::to_string(b.size() - end) + " bytes");
  memcpy(&d.bypass, b.data() + end, 4);
  return d;
}

/* ------------------------------------------------------ what a host reads */

struct ParamInfo
{
  Vst::ParameterInfo info;
  std::string title, shortTitle, units;
};

static std::vector<ParamInfo> Parameters(Vst::IEditController* ctrl)
{
  std::vector<ParamInfo> out;
  for (int32 i = 0; i < ctrl->getParameterCount(); i++)
  {
    ParamInfo p{};
    Require(ctrl->getParameterInfo(i, p.info) == kResultOk, "getParameterInfo failed");
    p.title = Utf8(p.info.title, 128);
    p.shortTitle = Utf8(p.info.shortTitle, 128);
    p.units = Utf8(p.info.units, 128);
    out.push_back(p);
  }
  return out;
}

static Json FlagNames(int32 flags)
{
  static const std::pair<int32, const char*> kFlags[] = {
    {Vst::ParameterInfo::kCanAutomate, "kCanAutomate"}, {Vst::ParameterInfo::kIsReadOnly, "kIsReadOnly"},
    {Vst::ParameterInfo::kIsWrapAround, "kIsWrapAround"}, {Vst::ParameterInfo::kIsList, "kIsList"},
    {Vst::ParameterInfo::kIsHidden, "kIsHidden"}, {Vst::ParameterInfo::kIsProgramChange, "kIsProgramChange"},
    {Vst::ParameterInfo::kIsBypass, "kIsBypass"}};
  Json names = Json::List();
  for (const auto& [bit, name] : kFlags)
    if (flags & bit)
      names.Push(Json::Text(name));
  return names;
}

static Json ParametersJson(const std::vector<ParamInfo>& ps)
{
  Json list = Json::List();
  for (size_t i = 0; i < ps.size(); i++)
  {
    const ParamInfo& p = ps[i];
    list.Push(Json::Map()
                .Add("index", Json::Int(long(i)))
                .Add("id", Json::Int(p.info.id))
                .Add("title", Json::Text(p.title))
                .Add("shortTitle", Json::Text(p.shortTitle))
                .Add("units", Json::Text(p.units))
                .Add("stepCount", Json::Int(p.info.stepCount))
                .Add("defaultNormalizedValue", Json::Real(p.info.defaultNormalizedValue))
                .Add("unitId", Json::Int(p.info.unitId))
                .Add("flags", Json::Int(p.info.flags))
                .Add("flagNames", FlagNames(p.info.flags)));
  }
  return list;
}

/* The plugin's own parameters and the bypass: everything but the MIDI CC block,
 * which iPlug2 exports for a MIDI-in plugin and nobody automates by hand. */
static bool Recorded(const ParamInfo& p) { return p.info.id < kFirstMidiCcId; }

struct Value
{
  Vst::ParamID id;
  std::string title;
  double normalized, plain;
  std::string display;
};

static std::vector<Value> Values(Vst::IEditController* ctrl, const std::vector<ParamInfo>& ps)
{
  std::vector<Value> out;
  for (const ParamInfo& p : ps)
  {
    if (!Recorded(p))
      continue;
    const double n = ctrl->getParamNormalized(p.info.id);
    Vst::String128 text{};
    ctrl->getParamStringByValue(p.info.id, n, text);
    out.push_back({p.info.id, p.title, n, ctrl->normalizedParamToPlain(p.info.id, n), Utf8(text, 128)});
  }
  return out;
}

static Json ValuesJson(const std::vector<Value>& vs)
{
  Json list = Json::List();
  for (const Value& v : vs)
    list.Push(Json::Map()
                .Add("id", Json::Int(v.id))
                .Add("title", Json::Text(v.title))
                .Add("normalized", Json::Real(v.normalized))
                .Add("plain", Json::Real(v.plain))
                .Add("display", Json::Text(v.display)));
  return list;
}

/* ------------------------------------------------------------- scenarios */

/* What a scenario has to work with: the instance, and its editor on demand. */
struct Session
{
  Instance& plugin;
  const std::string& product;
  Editor editor;
  bool editorOpen = false;

  void Set(Vst::ParamID id, double plain) { plugin.Set(id, plain); }
  void Settle() { plugin.Settle(); }
  void Send(int tag, const std::string& text)
  {
    if (!editorOpen)
    {
      editor.Open(plugin.ctrl, product);
      editorOpen = true;
    }
    editor.Send(tag, text);
  }
  void Finish()
  {
    if (editorOpen)
      editor.Close();
    editorOpen = false;
    plugin.Settle();
  }
};

/* What a finished capture must show, or the scenario did not happen. */
using Expect = std::function<void(const Decoded&)>;

struct Scenario
{
  const char* name;
  const char* description;
  std::function<void(Session&)> run;
  Expect expect;
};

/* The Trance Gate's parameter IDs (plugins/trance-gate/Params.h) and editor
 * message tags (TranceGate.h), named here so the scenario reads as one. */
namespace tg {
enum : Vst::ParamID { kSlot = 0, kLength, kRate, kLegato, kTimeMode, kCurve, kAmount, kWidth,
                      kAttack, kDecay, kSustain, kRelease, kFade, kFadeSoft, kFadeDir };
enum { kMsgSetStep = 96, kMsgSetDepth = 97, kMsgSetOrder = 103, kMsgRandomize = 104 };

/* "x.x-" as pad states, one message a step: on, off, tie. */
static void Draw(Session& s, const char* pads)
{
  for (int i = 0; pads[i]; i++)
    s.Send(kMsgSetStep, std::to_string(i) + ":" + (pads[i] == 'x' ? "1" : pads[i] == '-' ? "2" : "0"));
}

/* A slot's field from the blob: "<steps>:<ties>:<length>[:...]". */
static std::string Field(const std::string& blob, const std::string& key)
{
  const std::string needle = "\"" + key + "\":\"";
  const size_t at = blob.find(needle);
  if (at == std::string::npos)
    return {};
  const size_t from = at + needle.size(), to = blob.find('"', from);
  return to == std::string::npos ? std::string() : blob.substr(from, to - from);
}

static std::string Part(const std::string& field, int n)
{
  size_t from = 0;
  for (int i = 0; i < n; i++)
  {
    from = field.find(':', from);
    if (from == std::string::npos)
      return {};
    from++;
  }
  return field.substr(from, field.find(':', from) - from);
}
} // namespace tg

static std::vector<Scenario> TranceGateScenarios()
{
  using namespace tg;
  return {
    {"default", "A fresh instance, nothing touched.",
     [](Session&) {},
     [](const Decoded& d) {
       Require(d.params.size() == 15 && d.params[kSlot] == 1.0 && d.params[kLength] == 16.0,
               "default: Slot 1, Length 16");
       Require(d.strings.size() == 1 && d.strings[0].find("\"sv\":7") != std::string::npos,
               "default: one string, the engine's v7 blob");
     }},
    {"slots",
     "Three slots, each its own sound and pattern; slot 2 current. Slot 1: 1/8, Amount 80, "
     "Attack 5, Decay 30, Sustain 60, Release 25, Exponential, a drawn 16-step pattern with a tie "
     "and two accents. Slot 2: 32 steps, Width 50, Join Neighbors, Fade 50 soft and out, a roll "
     "with seed 2202. Slot 3: 12 steps, 1/16T, Env Time %, S-Curve, Sustain 40, Decay 60, a drawn "
     "pattern with a tie and a moved arrival. Slots 4-8 untouched.",
     [](Session& s) {
       s.Set(kRate, 5);   /* 1/8 */
       s.Set(kLength, 16);
       s.Set(kAmount, 80);
       s.Set(kAttack, 5);
       s.Set(kDecay, 30);
       s.Set(kSustain, 60);
       s.Set(kRelease, 25);
       s.Set(kCurve, 1);  /* Exponential */
       s.Settle();
       Draw(s, "x.x-.x.xxx.x..xx");
       s.Send(kMsgSetDepth, "5:0.5");
       s.Send(kMsgSetDepth, "9:0.25");
       s.Settle();

       s.Set(kSlot, 2);
       s.Settle();
       s.Set(kLength, 32);
       s.Set(kWidth, 50);
       s.Set(kLegato, 1);
       s.Set(kFade, 50);
       s.Set(kFadeSoft, 1);
       s.Set(kFadeDir, 1);
       s.Settle();
       s.Send(kMsgRandomize, "2202");
       s.Settle();

       s.Set(kSlot, 3);
       s.Settle();
       s.Set(kLength, 12);
       s.Set(kRate, 8);   /* 1/16T */
       s.Set(kTimeMode, 1);
       s.Set(kCurve, 2);  /* S-Curve */
       s.Set(kSustain, 40);
       s.Set(kDecay, 60);
       s.Settle();
       Draw(s, "xx.x.x-.x.x.");
       s.Send(kMsgSetOrder, "0:3");
       s.Settle();

       s.Set(kSlot, 2);
       s.Settle();
     },
     [](const Decoded& d) {
       Require(d.params[kSlot] == 2.0 && d.params[kLength] == 32.0 && d.params[kWidth] == 50.0 &&
                 d.params[kLegato] == 1.0 && d.params[kFadeDir] == 1.0,
               "slots: the host's values are slot 2's");
       const std::string& blob = d.strings.at(0);
       Require(blob.find("\"slot\":1,") != std::string::npos, "slots: the blob's current slot is 2");
       const std::string p0 = Field(blob, "p0"), p1 = Field(blob, "p1"), p2 = Field(blob, "p2");
       Require(Part(p0, 2) == "16" && Part(p0, 1).find_first_not_of('0') != std::string::npos &&
                 !Part(p0, 3).empty(),
               "slots: slot 1 has its 16 steps, a tie and accents (" + p0 + ")");
       Require(Part(p1, 2) == "32", "slots: slot 2 has 32 steps (" + p1 + ")");
       /* "<steps>:<ties>:<length>:<depths>:<orders>": the orders are written only
        * when they are not position order. */
       Require(Part(p2, 2) == "12" && !Part(p2, 4).empty(),
               "slots: slot 3 has 12 steps and a moved arrival (" + p2 + ")");
       Require(!Field(blob, "s0").empty() && !Field(blob, "s2").empty(),
               "slots: slots 1 and 3 carry sounds of their own");
     }},
    {"bypassed", "A fresh instance with the host's bypass on.",
     [](Session& s) {
       s.plugin.SetNormalized(kBypassId, 1.0);
       s.Settle();
     },
     [](const Decoded& d) { Require(d.bypass == 1, "bypassed: the trailing int32 is 1"); }},
  };
}

static std::vector<Scenario> SideChainScenarios()
{
  return {
    {"default", "A fresh instance, nothing touched.",
     [](Session&) {},
     [](const Decoded& d) {
       Require(d.params.size() == 15 && d.strings.empty(), "default: fifteen values and nothing else");
     }},
    {"custom",
     "Every parameter away from its default: Sidechain source, 1/8, % of cycle, Delay -20, "
     "Attack 10, Hold 15, Release 50, Depth 75, S-Curve, channel 10, trigger C3 (60), Gate, "
     "Vel 40, Threshold -18 dB, Lockout 50 ms.",
     [](Session& s) {
       const double values[15] = {2, 6, 1, -20, 10, 15, 50, 75, 2, 10, 60, 1, 40, -18, 50};
       for (Vst::ParamID id = 0; id < 15; id++)
         s.Set(id, values[id]);
       s.Settle();
     },
     [](const Decoded& d) {
       const double values[15] = {2, 6, 1, -20, 10, 15, 50, 75, 2, 10, 60, 1, 40, -18, 50};
       for (size_t i = 0; i < 15; i++)
         Require(std::fabs(d.params.at(i) - values[i]) < 1e-9,
                 "custom: parameter " + std::to_string(i) + " is " + std::to_string(d.params.at(i)));
     }},
  };
}

/* The Spectrogram's editor message tags (Spectrogram.h). */
namespace spectro {
enum { kMsgRange = 96, kMsgSelect = 97, kMsgClash = 98, kMsgView = 99, kMsgCompare = 100 };
}

static std::vector<Scenario> SpectrogramScenarios()
{
  using namespace spectro;
  return {
    {"default", "A fresh instance, nothing touched.",
     [](Session&) {},
     [](const Decoded& d) {
       Require(d.params.empty() && d.strings.size() == 5, "default: no parameters, five strings");
     }},
    {"session",
     "What the editor sends when buses 2 and 5 are shown beside this track, the clash compares "
     "them, and the zoom is Bass: select \"2,5\", view \"0,1,2\", compare \"1:2:1\", clash "
     "\"-60:12\" (the editor's constants), range \"40:800\".",
     [](Session& s) {
       s.Send(kMsgClash, "-60:12");
       s.Send(kMsgSelect, "2,5");
       s.Send(kMsgView, "0,1,2");
       s.Send(kMsgCompare, "1:2:1");
       s.Send(kMsgRange, "40:800");
       s.Settle();
     },
     [](const Decoded& d) {
       const std::vector<std::string> want = {"2,5", "-60.00:12.00", "0,1,2", "1:2:1", "40.00:800.00"};
       Require(d.strings == want, "session: the five strings are what the editor chose");
     }},
  };
}

/* The Listen-In's editor message tag (ListenIn.h). */
namespace listenin {
enum { kMsgLabel = 96 };
}

static std::vector<Scenario> ListenInScenarios()
{
  return {
    {"default", "A fresh instance, nothing touched: bus 1, no name.",
     [](Session&) {},
     [](const Decoded& d) {
       Require(d.params.size() == 1 && d.params[0] == 1.0 && d.strings.size() == 1 && d.strings[0].empty(),
               "default: bus 1 and an empty name");
     }},
    {"labelled", "Bus 3, named \"Bässe & Kick\" in the editor (UTF-8, two bytes for the ä).",
     [](Session& s) {
       s.Set(0, 3);
       s.Settle();
       s.Send(listenin::kMsgLabel, "Bässe & Kick");
       s.Settle();
     },
     [](const Decoded& d) {
       Require(d.params.at(0) == 3.0 && d.strings.at(0) == "Bässe & Kick", "labelled: bus 3 and its name");
     }},
  };
}

static std::vector<Scenario> ScenariosFor(const std::string& product)
{
  if (product == "NITranceGate") return TranceGateScenarios();
  if (product == "NISideChain") return SideChainScenarios();
  if (product == "NISpectrogram") return SpectrogramScenarios();
  if (product == "NIListenIn") return ListenInScenarios();
  Fail("no scenarios for " + product);
}

/* ------------------------------------------------------------ the class */

static uint32_t Word(const char* tuid, int at)
{
  const auto* b = reinterpret_cast<const uint8_t*>(tuid);
  return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | uint32_t(b[at + 3]);
}

static std::string FourCC(uint32_t v)
{
  const char s[5] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v), 0};
  return s;
}

/*
 * THE CLASS AS A HOST SEES IT. The TUID's sixteen bytes are read back from the
 * factory, not computed: on macOS (COM_COMPATIBLE 0) they are the FUID's four
 * words big-endian, which is also the 32-hex-digit string a host and
 * moduleinfo.json write. On Windows the same FUID's first eight bytes are in
 * GUID order -- the string is the same, the bytes are not -- which is recorded
 * here as arithmetic, because this build is macOS only.
 */
static Json ClassJson(const Module& m, Instance& probe)
{
  int index = -1;
  const PClassInfo2 ci = m.Effect(&index);
  PFactoryInfo fi;
  m.factory->getFactoryInfo(&fi);
  const uint32_t l1 = Word(ci.cid, 0), l2 = Word(ci.cid, 4), l3 = Word(ci.cid, 8), l4 = Word(ci.cid, 12);
  const uint8_t win[8] = {uint8_t(l1), uint8_t(l1 >> 8), uint8_t(l1 >> 16), uint8_t(l1 >> 24),
                          uint8_t(l2 >> 16), uint8_t(l2 >> 24), uint8_t(l2), uint8_t(l2 >> 8)};
  const std::string winBytes = Hex(win, 8) + Hex(ci.cid + 8, 8);

  Json classes = Json::List();
  FUnknownPtr<IPluginFactory2> f2(m.factory);
  for (int32 i = 0; i < m.factory->countClasses(); i++)
  {
    PClassInfo2 c;
    f2->getClassInfo2(i, &c);
    classes.Push(Json::Map().Add("category", Json::Text(c.category)).Add("name", Json::Text(c.name))
                   .Add("cid", Json::Text(Hex(c.cid, 16))));
  }

  TUID controller{};
  const tresult hasController = probe.comp->getControllerClassId(controller);
  char words[64];
  snprintf(words, sizeof words, "0x%08X, 0x%08X, 0x%08X, 0x%08X", l1, l2, l3, l4);

  return Json::Map()
    .Add("name", Json::Text(ci.name))
    .Add("vendor", Json::Text(ci.vendor))
    .Add("factoryVendor", Json::Text(fi.vendor))
    .Add("capturedFromVersion", Json::Text(ci.version))
    .Add("sdkVersion", Json::Text(ci.sdkVersion))
    .Add("category", Json::Text(ci.category))
    .Add("subCategories", Json::Text(ci.subCategories))
    .Add("cardinality", Json::Int(ci.cardinality))
    .Add("classFlags", Json::Int(ci.classFlags))
    .Add("classFlagNames", [&] {
      Json n = Json::List();
      if (ci.classFlags & Vst::kDistributable) n.Push(Json::Text("kDistributable"));
      if (ci.classFlags & Vst::kSimpleModeSupported) n.Push(Json::Text("kSimpleModeSupported"));
      return n;
    }())
    .Add("factoryClasses", classes)
    .Add("singleComponent",
         Json::Bool(m.factory->countClasses() == 1 && hasController != kResultOk && probe.ctrl != nullptr))
    .Add("getControllerClassId",
         Json::Text(hasController == kResultOk ? Hex(controller, 16)
                    : hasController == kNotImplemented ? "kNotImplemented" : "failed"))
    .Add("fuid", Json::Text(words))
    .Add("fuidWords", Json::Map()
                        .Add("l1", Json::Text("F2AEE70D: iPlug2's VST3_PROCESSOR_UID constant"))
                        .Add("l2", Json::Text("00DE4F4E: iPlug2's VST3_PROCESSOR_UID constant"))
                        .Add("l3", Json::Text(Hex(ci.cid + 8, 4) + ": PLUG_MFR_ID '" + FourCC(l3) + "'"))
                        .Add("l4", Json::Text(Hex(ci.cid + 12, 4) + ": PLUG_UNIQUE_ID '" + FourCC(l4) + "'")))
    .Add("cid", Json::Text(Hex(ci.cid, 16)))
    .Add("tuidBytesMacOS", Json::Text(Hex(ci.cid, 16)))
    .Add("tuidBytesWindows", Json::Text(winBytes))
    .Add("moduleinfoJson", Json::Bool([[NSFileManager defaultManager]
      fileExistsAtPath:[NSString stringWithUTF8String:(m.bundle + "/Contents/Resources/moduleinfo.json").c_str()]]));
}

/* --------------------------------------------------------------- main */

static int ParamCount(const std::vector<ParamInfo>& ps)
{
  int n = 0;
  for (const ParamInfo& p : ps)
    n += p.info.id < kBypassId ? 1 : 0;
  return n;
}

static Json DecodedJson(const Decoded& d)
{
  Json params = Json::List(), strings = Json::List();
  for (double v : d.params)
    params.Push(Json::Real(v));
  for (const std::string& s : d.strings)
    strings.Push(Json::Text(s));
  return Json::Map()
    .Add("magic", Json::Text("NIst"))
    .Add("version", Json::Int(d.version))
    .Add("bodyBytes", Json::Int(d.bodyBytes))
    .Add("params", params)
    .Add("strings", strings)
    .Add("bypass", Json::Int(d.bypass));
}

static bool SameValues(const std::vector<Value>& a, const std::vector<Value>& b, std::string& why)
{
  if (a.size() != b.size())
  {
    why = "a different number of parameters";
    return false;
  }
  for (size_t i = 0; i < a.size(); i++)
    if (a[i].id != b[i].id || a[i].normalized != b[i].normalized)
    {
      why = "parameter " + std::to_string(a[i].id) + " (" + a[i].title + ") reads " +
            std::to_string(b[i].normalized) + ", not " + std::to_string(a[i].normalized);
      return false;
    }
  return true;
}

static void Capture(const Module& m, const std::string& dir, Json& ids)
{
  printf("%s\n", m.name.c_str());
  [[NSFileManager defaultManager] createDirectoryAtPath:[NSString stringWithUTF8String:dir.c_str()]
                            withIntermediateDirectories:YES attributes:nil error:nil];

  Instance probe;
  probe.Create(m);
  const std::vector<ParamInfo> params = Parameters(probe.ctrl);
  ids.Add(m.name, ClassJson(m, probe));
  WriteJson(dir + "/parameters.json", Json::Map()
                                       .Add("product", Json::Text(m.name))
                                       .Add("count", Json::Int(long(params.size())))
                                       .Add("parameters", ParametersJson(params)));
  probe.Destroy();

  for (const Scenario& sc : ScenariosFor(m.name))
  {
    Instance plugin;
    plugin.Create(m);
    plugin.Settle();
    Session s{plugin, m.name};
    sc.run(s);
    s.Finish();
    const Bytes component = plugin.ComponentState(), controller = plugin.ControllerState();
    const std::vector<Value> values = Values(plugin.ctrl, params);
    plugin.Destroy();

    const Decoded d = Decode(component, ParamCount(params), m.name + "/" + sc.name);
    sc.expect(d);
    /* The host's reading of each parameter and the chunk's own double agree. */
    for (const Value& v : values)
      if (v.id < kBypassId)
        Require(std::fabs(v.plain - d.params.at(v.id)) < 1e-9 * std::max(1.0, std::fabs(v.plain)),
                m.name + "/" + sc.name + ": parameter " + std::to_string(v.id) +
                  " is " + std::to_string(v.plain) + " to the host and " +
                  std::to_string(d.params.at(v.id)) + " in the chunk");

    /*
     * The reload: a second instance, the set reopened. It must report the same
     * parameters and save the same chunk. NOT NECESSARILY THE SAME BYPASS:
     * iPlug2's setState restores the saved flag into the controller's Bypass
     * parameter only (IPlugVST3State::SetState -> UpdateParams), so the
     * processor runs unbypassed, and saves 0, until a host sends it the
     * parameter. What it saves is recorded, because it is iPlug2's behaviour.
     */
    Instance reopened;
    reopened.Create(m);
    reopened.Load(component, controller);
    reopened.Settle();
    std::string why;
    Require(SameValues(values, Values(reopened.ctrl, params), why),
            m.name + "/" + sc.name + ": reloaded, " + why);
    const Bytes again = reopened.ComponentState();
    const Decoded r = Decode(again, ParamCount(params), m.name + "/" + sc.name + " reloaded");
    Require(again.size() == component.size() && std::equal(component.begin(), component.end() - 4, again.begin()),
            m.name + "/" + sc.name + ": reloaded, it saves another chunk");
    reopened.Destroy();

    const std::string base = dir + "/" + sc.name;
    WriteFile(base + ".component.bin", component.data(), component.size());
    WriteFile(base + ".controller.bin", controller.data(), controller.size());
    WriteJson(base + ".json",
              Json::Map()
                .Add("product", Json::Text(m.name))
                .Add("scenario", Json::Text(sc.name))
                .Add("description", Json::Text(sc.description))
                .Add("component", Json::Map()
                                    .Add("file", Json::Text(std::string(sc.name) + ".component.bin"))
                                    .Add("bytes", Json::Int(long(component.size()))))
                .Add("controller", Json::Map()
                                     .Add("file", Json::Text(std::string(sc.name) + ".controller.bin"))
                                     .Add("bytes", Json::Int(long(controller.size())))
                                     .Add("sameAsComponent", Json::Bool(controller == component)))
                .Add("decoded", DecodedJson(d))
                .Add("values", ValuesJson(values))
                .Add("reload", Json::Map()
                                 .Add("sameParameters", Json::Bool(true))
                                 .Add("sameChunk", Json::Bool(true))
                                 .Add("bypassSavedAfterReload", Json::Int(r.bypass))));
    printf("  %-10s %5zu bytes, reloads to the same parameters and chunk\n", sc.name, component.size());
  }
}

/*
 * THE COMMITTED FIXTURES, OPENED BY THIS BUILD: each scenario's states loaded
 * into a fresh instance, whose parameters must read what the capture recorded.
 * Bytes are not compared -- a later engine may write the same patch
 * differently -- only what a reopened set would show.
 */
static int Check(const Module& m, const std::string& dir)
{
  printf("%s\n", m.name.c_str());
  int fails = 0;
  for (const Scenario& sc : ScenariosFor(m.name))
  {
    const std::string base = dir + "/" + sc.name;
    NSData* json = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:(base + ".json").c_str()]];
    Require(json != nil, "no " + base + ".json");
    NSDictionary* doc = [NSJSONSerialization JSONObjectWithData:json options:0 error:nil];
    Require([doc isKindOfClass:[NSDictionary class]], base + ".json is not an object");

    Instance plugin;
    plugin.Create(m);
    plugin.Load(ReadFile(base + ".component.bin"), ReadFile(base + ".controller.bin"));
    plugin.Settle();
    std::string trail;
    bool ok = true;
    for (NSDictionary* v in doc[@"values"])
    {
      const auto id = Vst::ParamID([v[@"id"] unsignedIntValue]);
      const double want = [v[@"normalized"] doubleValue], got = plugin.ctrl->getParamNormalized(id);
      if (std::fabs(want - got) > 1e-9)
      {
        ok = false;
        trail += " " + std::to_string(id) + ":" + std::to_string(got) + "!=" + std::to_string(want);
      }
    }
    plugin.Destroy();
    printf("  %-60s %s%s\n", (std::string(sc.name) + " reopens with its parameters").c_str(),
           ok ? "ok" : "FAIL", trail.c_str());
    fails += ok ? 0 : 1;
  }
  return fails;
}

int main(int argc, char** argv)
{
  const bool check = argc > 1 && !strcmp(argv[1], "--check");
  const int first = check ? 2 : 1;
  if (argc < first + 2)
  {
    fprintf(stderr, "usage: vst3_capture [--check] <fixtures-dir> <bundle.vst3>...\n");
    return 2;
  }
  const std::string root = argv[first];
  /* A line at a time: a run that dies says how far it got. */
  setvbuf(stdout, nullptr, _IOLBF, 0);
  int fails = 0;
  @autoreleasepool
  {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    Json ids = Json::Map();
    for (int i = first + 1; i < argc; i++)
    {
      Module m;
      m.Open(argv[i]);
      if (check)
        fails += Check(m, root + "/" + m.name);
      else
        Capture(m, root + "/" + m.name, ids);
    }
    if (!check)
      WriteJson(root + "/ids.json", ids);
  }
  printf("%s\n", fails ? "FAIL" : "ok");
  return fails ? 1 : 0;
}
