// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The animated ground keeps the host's time -- end to end, through the Audio
 * Unit this checkout BUILT, with its real editor open.
 *
 *   au_ground <bundle.component>
 *
 * WHY THIS ONE EXISTS. The owner's report was "the background doesn't move".
 * Every unit under the ground has its own tests -- the beat clock in Rust, the
 * C ABI from C, the editor's field in node, the harness in Chrome -- and every
 * one of them can be green while the plugin a person opens in a DAW stays
 * still, because the fault lives in the seams between them: the host's
 * transport reaching ProcessBlock, the editor-open switch, the idle tick, the
 * message crossing into the page. This walks every seam once, the way a host
 * does:
 *
 *   the AU wrapper    reads the transport from the host callbacks below --
 *                     beat and tempo, musical time location, transport state
 *                     -- which is how Logic, Live and every AU host hand it over
 *   the editor        is opened through kAudioUnitProperty_CocoaUI and the
 *                     bundle's own view factory, so OnUIOpen runs when the
 *                     WKWebView finishes loading the real index.html, exactly
 *                     as it does in a host's window
 *   the idle timer    is iPlug2's, on this process's main run loop
 *   the page          receives SAMFD(112, ...) as it would in a host; a
 *                     wrapper installed around the editor's own SAMFD records
 *                     each one, and the editor still handles it
 *
 * THE AUDIO IS SILENT, ON PURPOSE. The ground follows the host's tempo and
 * nothing else, so a plugin on a silent track has to ring exactly as one on a
 * drum bus does. A ground that still listened to audio would fail here.
 *
 * WHAT IT ASSERTS, at 120 BPM in 4/4 with the transport playing from bar 1:
 *
 *   - four seconds of audio is eight rings, one a beat: 2 Hz of song time
 *   - each ring comes from the block that holds its beat, not a block late
 *   - the 1st and the 5th -- each bar's downbeat -- are strong (1.000), the
 *     others are the beat strength (0.400)
 *   - the ground's canvas then changes at least every other 50 ms sample:
 *     it animates in the editor as a host shows it, which WebKit reports as
 *     hidden, with requestAnimationFrame all but stopped and page timers
 *     throttled -- because the plugin sends a frame tick (114) every idle
 *     tick while the editor reports its ground moving, and that is asserted
 *     too
 *   - with the transport stopped, no further ring
 *
 * NOTHING HERE IS TIMED ON THE WALL CLOCK. The render stops after each block
 * that holds a beat and waits -- run loop spinning, so iPlug2's idle timer
 * fires -- for that beat's ring to reach the page. One ring per wait, so two
 * beats can never merge into one idle tick however loaded the machine is, and
 * a ring that came a block late is a wait that times out.
 *
 * The bundle is loaded by path and registered in this process only
 * (au_bundle.h); nothing installed is read and nothing is installed. A window
 * is created to host the editor's view, as a host does, but never shown.
 */
#import <AppKit/AppKit.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <WebKit/WebKit.h>

#include "au_bundle.h"

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#define SR     48000.0
#define BLOCK  512
#define BPM    120.0

/* The editor protocol's ground, frame-tick and ready tags (ni/Editor.h;
 * editor_tags holds the editors' copies to the C++). */
#define TAG_GROUND 112
#define TAG_GROUND_TICK 114
#define TAG_READY  120

static AudioUnit gAu;
/* Where the song is, in samples. Written by the render thread only, between
 * renders; read by the host callbacks, which the AU calls inside a render. */
static double gSamplePos = 0.0;
static atomic_int gPlaying = 1;

static double Beat(void) { return gSamplePos / SR * (BPM / 60.0); }

static OSStatus BeatAndTempo(void *ud, Float64 *beat, Float64 *tempo)
{
    (void) ud;
    if (beat) *beat = Beat();
    if (tempo) *tempo = BPM;
    return noErr;
}

static OSStatus MusicalTime(void *ud, UInt32 *toNextBeat, Float32 *num, UInt32 *den,
                            Float64 *downBeat)
{
    (void) ud;
    const double b = Beat();
    if (toNextBeat) *toNextBeat = (UInt32) ((1.0 - (b - floor(b))) * SR * 60.0 / BPM);
    if (num) *num = 4.0f;
    if (den) *den = 4;
    if (downBeat) *downBeat = floor(b / 4.0) * 4.0;
    return noErr;
}

static OSStatus TransportState(void *ud, Boolean *playing, Boolean *changed,
                               Float64 *sampleInTimeline, Boolean *cycling,
                               Float64 *cycleStart, Float64 *cycleEnd)
{
    (void) ud;
    if (playing) *playing = atomic_load(&gPlaying) != 0;
    if (changed) *changed = false;
    if (sampleInTimeline) *sampleInTimeline = gSamplePos;
    if (cycling) *cycling = false;
    if (cycleStart) *cycleStart = 0;
    if (cycleEnd) *cycleEnd = 0;
    return noErr;
}

/* Digital silence on every input. */
static OSStatus Silence(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts,
                        UInt32 bus, UInt32 frames, AudioBufferList *io)
{
    (void) ref; (void) ts; (void) bus; (void) frames;
    for (UInt32 b = 0; b < io->mNumberBuffers; b++)
        memset(io->mBuffers[b].mData, 0, io->mBuffers[b].mDataByteSize);
    *flags |= kAudioUnitRenderAction_OutputIsSilence;
    return noErr;
}

static int gFails = 0;
static void ok(int cond, const char *what, const char *detail)
{
    printf("  %-58s %s%s%s%s\n", what, cond ? "ok" : "FAIL", detail ? " (" : "",
           detail ? detail : "", detail ? ")" : "");
    if (!cond) gFails++;
}

static int Setup(void)
{
    AudioStreamBasicDescription f = {
        .mSampleRate = SR,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked |
                        kAudioFormatFlagIsNonInterleaved,
        .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4,
        .mChannelsPerFrame = 2, .mBitsPerChannel = 32,
    };
    UInt32 inputs = 1, size = sizeof inputs;
    AudioUnitGetProperty(gAu, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, 0,
                         &inputs, &size);
    AURenderCallbackStruct cb = {Silence, NULL};
    for (UInt32 e = 0; e < inputs; e++) {
        AudioUnitSetProperty(gAu, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, e,
                             &f, sizeof f);
        AudioUnitSetProperty(gAu, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                             e, &cb, sizeof cb);
    }
    AudioUnitSetProperty(gAu, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f,
                         sizeof f);
    UInt32 maxFrames = BLOCK;
    AudioUnitSetProperty(gAu, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global,
                         0, &maxFrames, sizeof maxFrames);
    HostCallbackInfo host = {
        .hostUserData = NULL,
        .beatAndTempoProc = BeatAndTempo,
        .musicalTimeLocationProc = MusicalTime,
        .transportStateProc = TransportState,
    };
    if (AudioUnitSetProperty(gAu, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0,
                             &host, sizeof host) != noErr)
        return 0;
    return AudioUnitInitialize(gAu) == noErr;
}

/* ------------------------------------------------------------ the render */

static double Now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double) t.tv_sec + (double) t.tv_nsec / 1e9;
}

static int gBlocks = 0;
static atomic_int gRendering = 0;
static double gSampleTime = 0.0;

/* `gBlocks` blocks, as a host's render thread renders them. A stopped
 * transport does not advance the song position, as in a host. */
static void *Render(void *arg)
{
    (void) arg;
    static float l[BLOCK], r[BLOCK];
    struct { AudioBufferList list; AudioBuffer second; } abl;
    AudioTimeStamp ts = {0};
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    for (int k = 0; k < gBlocks; k++) {
        abl.list.mNumberBuffers = 2;
        abl.list.mBuffers[0] = (AudioBuffer) {1, sizeof l, l};
        abl.list.mBuffers[1] = (AudioBuffer) {1, sizeof r, r};
        ts.mSampleTime = gSampleTime;
        AudioUnitRenderActionFlags fl = 0;
        if (AudioUnitRender(gAu, &fl, &ts, 0, BLOCK, &abl.list) != noErr) {
            printf("  FAIL: AudioUnitRender failed at sample %.0f\n", gSampleTime);
            gFails++;
            break;
        }
        gSampleTime += BLOCK;
        if (atomic_load(&gPlaying)) gSamplePos += BLOCK;
    }
    atomic_store(&gRendering, 0);
    return NULL;
}

static void Spin(double secs)
{
    const double end = Now() + secs;
    while (Now() < end)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
}

/* Render `blocks` blocks on a render thread while the main thread -- the
 * plugin's main thread -- spins its run loop. */
static void RenderBlocks(int blocks)
{
    if (blocks <= 0) return;
    gBlocks = blocks;
    atomic_store(&gRendering, 1);
    pthread_t t;
    pthread_create(&t, NULL, Render, NULL);
    while (atomic_load(&gRendering))
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, false);
    pthread_join(t, NULL);
}

/* ------------------------------------------------------------ the editor */

static WKWebView *FindWebView(NSView *v)
{
    if ([v isKindOfClass:[WKWebView class]]) return (WKWebView *) v;
    for (NSView *s in v.subviews) {
        WKWebView *w = FindWebView(s);
        if (w) return w;
    }
    return nil;
}

/* Run `js` in the page and wait for its result, spinning the run loop. */
static id Eval(WKWebView *web, NSString *js, double timeout)
{
    __block id result = nil;
    __block BOOL done = NO;
    [web evaluateJavaScript:js completionHandler:^(id r, NSError *e) {
        result = e ? nil : r;
        done = YES;
    }];
    const double end = Now() + timeout;
    while (!done && Now() < end)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
    return result;
}

static BOOL WaitFor(WKWebView *web, NSString *js, double timeout)
{
    const double end = Now() + timeout;
    while (Now() < end) {
        id r = Eval(web, js, 1.0);
        if ([r isKindOfClass:[NSNumber class]] && [r boolValue]) return YES;
        Spin(0.05);
    }
    return NO;
}

/* Every SAMFD the plugin sends from here on is recorded in window.__msgs as
 * [tag, ms, text] -- and still handed to the editor's own SAMFD. */
static NSString *const kRecorder =
    @"(() => {"
     "  const own = window.SAMFD;"
     "  window.__msgs = [];"
     "  window.SAMFD = (tag, n, b64) => {"
     "    let text = '';"
     "    try { text = atob(b64); } catch (e) {}"
     "    window.__msgs.push([tag | 0, performance.now(), text]);"
     "    return own(tag, n, b64);"
     "  };"
     "  return true;"
     "})()";

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc < 2) {
            fprintf(stderr, "usage: au_ground <bundle.component>\n");
            return 2;
        }
        [NSApplication sharedApplication];

        AudioComponent comp = ni_au_register(argv[1], NULL);
        if (!comp) return 1;
        if (AudioComponentInstanceNew(comp, &gAu) != noErr || !gAu) {
            printf("  FAIL: could not instantiate %s\n", argv[1]);
            return 1;
        }
        ok(Setup(), "it initialises with a host transport", NULL);

        /* THE EDITOR, the way a host opens it: the bundle's own view factory. */
        AudioUnitCocoaViewInfo info = {0};
        UInt32 size = sizeof info;
        if (AudioUnitGetProperty(gAu, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0,
                                 &info, &size) != noErr || !info.mCocoaAUViewBundleLocation) {
            printf("  FAIL: the AU offers no Cocoa view\n");
            return 1;
        }
        NSBundle *viewBundle =
            [NSBundle bundleWithURL:(__bridge NSURL *) info.mCocoaAUViewBundleLocation];
        Class factoryClass =
            [viewBundle classNamed:(__bridge NSString *) info.mCocoaAUViewClass[0]];
        id<AUCocoaUIBase> factory = [[factoryClass alloc] init];
        NSView *view = [factory uiViewForAudioUnit:gAu withSize:NSZeroSize];
        CFRelease(info.mCocoaAUViewBundleLocation);
        CFRelease(info.mCocoaAUViewClass[0]);
        if (!view) {
            printf("  FAIL: the view factory returned no view\n");
            return 1;
        }
        NSWindow *window =
            [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, view.frame.size.width,
                                                             view.frame.size.height)
                                        styleMask:NSWindowStyleMaskBorderless
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        window.releasedWhenClosed = NO;
        [window.contentView addSubview:view];

        WKWebView *web = FindWebView(view);
        ok(web != nil, "the editor is a WKWebView", NULL);
        if (!web) return 1;

        /* Loaded, the editor's bridge installed and its ground mounted. */
        ok(WaitFor(web,
                   @"typeof window.SAMFD === 'function' && "
                    "!!document.querySelector('canvas.ground')",
                   60.0),
           "the editor loads, with its ground", NULL);
        ok([Eval(web, kRecorder, 5.0) boolValue], "the page records what the plugin sends",
           NULL);

        /* Ask for the handshake again and wait for its answer: a reply means
         * the plugin has the editor open (it sends nothing otherwise), so the
         * ground is switched on before the first beat. */
        Eval(web,
             [NSString stringWithFormat:@"IPlugSendMsg({msg: 'SAMFUI', msgTag: %d, "
                                         "ctrlTag: -1, data: ''}); true",
                                        TAG_READY],
             5.0);
        ok(WaitFor(web, @"window.__msgs.length > 0", 5.0),
           "the plugin answers the editor (it is open)", NULL);
        Eval(web, @"window.__msgs.length = 0; true", 5.0);

        /* PLAYING, from bar 1 beat 1: four seconds is eight beats, beat j at
         * sample 24 000 j. Render up to and including the block that holds
         * each beat, then wait for its ring. */
        NSString *count = [NSString stringWithFormat:
            @"window.__msgs.filter((m) => m[0] === %d).length", TAG_GROUND];
        const int samplesPerBeat = (int) (SR * 60.0 / BPM);
        const int total = (int) (4.0 * SR / BLOCK);
        atomic_store(&gPlaying, 1);
        int rendered = 0, onTime = 0;
        for (int beat = 0; beat < 8; beat++) {
            const int holder = beat * samplesPerBeat / BLOCK;
            RenderBlocks(holder + 1 - rendered);
            rendered = holder + 1;
            if (WaitFor(web, [NSString stringWithFormat:@"%@ >= %d", count, beat + 1], 10.0))
                onTime++;
        }
        RenderBlocks(total - rendered);
        Spin(0.3);
        NSArray *playing = Eval(web,
                                [NSString stringWithFormat:@"window.__msgs.filter("
                                                            "(m) => m[0] === %d)",
                                                           TAG_GROUND],
                                5.0);

        /* AND THE GROUND MOVES. The rings are in the field now; sample the
         * canvas every 50 ms for a second and count how often it changed
         * between two samples. The page is HIDDEN, as WebKit reports a plugin
         * editor in a real host (document.hidden, about one
         * requestAnimationFrame in three seconds, page timers throttled to a
         * few hertz) -- the conditions the field has to animate in. */
        NSString *hash =
            @"(() => { const c = document.querySelector('canvas.ground');"
             "  const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;"
             "  let h = 2166136261;"
             "  for (let i = 0; i < d.length; i += 4)"
             "    h = Math.imul(h ^ (d[i] ^ (d[i + 1] << 8) ^ (d[i + 2] << 16)), 16777619);"
             "  return h >>> 0; })()";
        id hidden = Eval(web, @"document.hidden", 5.0);
        NSString *tickCount = [NSString stringWithFormat:
            @"window.__msgs.filter((m) => m[0] === %d).length", TAG_GROUND_TICK];
        const int ticksBefore = [Eval(web, tickCount, 5.0) intValue];
        id last = Eval(web, hash, 5.0);
        int samples = 0, changes = 0;
        const double sampleEnd = Now() + 1.0;
        while (Now() < sampleEnd) {
            Spin(0.05);
            id h = Eval(web, hash, 5.0);
            samples++;
            if (h && last && ![h isEqual:last]) changes++;
            last = h;
        }
        const int ticks = [Eval(web, tickCount, 5.0) intValue] - ticksBefore;

        /* STOPPED: the song position holds and the rings stop. */
        Eval(web, @"window.__msgs.length = 0; true", 5.0);
        atomic_store(&gPlaying, 0);
        RenderBlocks((int) (1.5 * SR / BLOCK));
        Spin(0.5);
        NSArray *stopped = Eval(web,
                                [NSString stringWithFormat:@"window.__msgs.filter("
                                                            "(m) => m[0] === %d)",
                                                           TAG_GROUND],
                                5.0);

        char detail[256];
        const NSUInteger n = [playing isKindOfClass:[NSArray class]] ? playing.count : 0;
        snprintf(detail, sizeof detail, "%lu rings", (unsigned long) n);
        ok(n == 8, "silent audio, 120 BPM: one ring per beat for 4 s (2 Hz)", detail);

        snprintf(detail, sizeof detail, "%d of 8", onTime);
        ok(onTime == 8, "each ring comes from the block that holds its beat", detail);

        NSMutableString *seen = [NSMutableString string];
        int pattern = n == 8;
        for (NSUInteger i = 0; i < n; i++) {
            NSString *s = playing[i][2];
            [seen appendFormat:@"%@%@", i ? @" " : @"", s];
            if (![s isEqualToString:(i % 4 == 0) ? @"1.000" : @"0.400"]) pattern = 0;
        }
        snprintf(detail, sizeof detail, "%s", seen.UTF8String);
        ok(pattern, "every 4th ring -- the downbeat -- is the strong one", detail);

        snprintf(detail, sizeof detail, "changed in %d of %d 50 ms samples; document.hidden %s",
                 changes, samples, [hidden boolValue] ? "true" : "false");
        ok(samples >= 5 && changes * 2 >= samples, "the ground animates in the hidden editor",
           detail);
        snprintf(detail, sizeof detail, "%d in about a second", ticks);
        ok(ticks >= 15, "the plugin sends its frame ticks while it moves", detail);

        const NSUInteger after = [stopped isKindOfClass:[NSArray class]] ? stopped.count : 99;
        snprintf(detail, sizeof detail, "%lu rings", (unsigned long) after);
        ok(after == 0, "the transport stopped: the ground rests", detail);

        /* A host closes the editor by taking its view away. */
        [view removeFromSuperview];
        [window close];
        AudioUnitUninitialize(gAu);
        AudioComponentInstanceDispose(gAu);

        printf(gFails ? "\n%d failure(s)\n" : "\nall ok\n", gFails);
        return gFails ? 1 : 0;
    }
}
