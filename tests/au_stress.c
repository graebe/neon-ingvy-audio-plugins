// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * auval -stress's state path, against the Audio Unit this checkout BUILT.
 *
 *   au_stress <bundle.component> [seconds] [state-threads]
 *
 * WHAT IT DOES. The bundle is loaded from its path and registered in this
 * process only (au_bundle.h), then driven the way `auval -strict -stress`
 * drives a plugin, all at once:
 *
 *   a render thread       AudioUnitRender, block after block
 *   N state threads       get kAudioUnitProperty_ClassInfo and set it back --
 *                         SerializeState and UnserializeState -- as fast as
 *                         they can, on threads that are not the main thread
 *   the main thread       its run loop, so iPlug2's idle timer fires and the
 *                         plugin services what the others recorded
 *
 * WHY. A host decides which thread restores state. The Spectrogram once chose
 * its receiver's sources from UnserializeState, and auval's stress test parked
 * one of two such threads forever; it was only found because auval happened to
 * run against an installed copy. This finds it in the build, and for every
 * plugin.
 *
 * A HANG IS THE FAILURE, so it cannot be waited out: a watchdog ends the
 * process with a FAIL line naming what never came back. A crash fails too,
 * as a crash. Exit 0 only when every thread finished and each did real work.
 */
#include "au_bundle.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#define BLOCK        512
#define MAX_STATE    8
/* Past the run itself, how long the threads get to come back. A state set
 * that waits for a worker takes milliseconds; this is only reached by one
 * that never returns. */
#define GRACE_S      20

static AudioUnit gAu;
static atomic_int gRunning = 1;
static atomic_int gDone[1 + MAX_STATE];
static atomic_long gSets, gRenders;
static int gStateThreads = 2;
static double gSeconds = 3.0;

static OSStatus Input(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts,
                      UInt32 bus, UInt32 frames, AudioBufferList *io)
{
    (void) ref; (void) flags; (void) bus;
    /* A slow sine, so an analyser has something to draw. */
    const double t0 = ts->mSampleTime;
    for (UInt32 b = 0; b < io->mNumberBuffers; b++) {
        float *d = (float *) io->mBuffers[b].mData;
        for (UInt32 i = 0; i < frames; i++)
            d[i] = 0.25f * (float) sin(0.05 * (t0 + i));
    }
    return noErr;
}

static void *Render(void *arg)
{
    (void) arg;
    static float l[BLOCK], r[BLOCK];
    struct { AudioBufferList list; AudioBuffer second; } abl;
    AudioTimeStamp ts = {0};
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    while (gRunning) {
        abl.list.mNumberBuffers = 2;
        abl.list.mBuffers[0] = (AudioBuffer) {1, sizeof l, l};
        abl.list.mBuffers[1] = (AudioBuffer) {1, sizeof r, r};
        AudioUnitRenderActionFlags fl = 0;
        if (AudioUnitRender(gAu, &fl, &ts, 0, BLOCK, &abl.list) == noErr)
            gRenders++;
        ts.mSampleTime += BLOCK;
        /* About real time at 48 kHz, as a host's callback would be. */
        usleep(10000);
    }
    gDone[0] = 1;
    return NULL;
}

static void *State(void *arg)
{
    const int i = (int) (long) arg;
    while (gRunning) {
        CFPropertyListRef pl = NULL;
        UInt32 size = sizeof pl;
        if (AudioUnitGetProperty(gAu, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                                 &pl, &size) == noErr && pl) {
            if (AudioUnitSetProperty(gAu, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global,
                                     0, &pl, sizeof pl) == noErr)
                gSets++;
            CFRelease(pl);
        }
    }
    gDone[i] = 1;
    return NULL;
}

static int AllDone(void)
{
    for (int i = 0; i <= gStateThreads; i++)
        if (!gDone[i]) return 0;
    return 1;
}

static void *Watchdog(void *arg)
{
    (void) arg;
    const useconds_t step = 50000;
    for (double waited = 0; waited < gSeconds + GRACE_S; waited += step / 1e6) {
        if (!gRunning && AllDone()) return NULL;
        usleep(step);
    }
    printf("  FAIL: hung -- after %.0f s, render %s", gSeconds + GRACE_S,
           gDone[0] ? "returned" : "STUCK");
    for (int i = 1; i <= gStateThreads; i++)
        printf(", state %d %s", i, gDone[i] ? "returned" : "STUCK");
    printf(" (%ld state sets before)\n", (long) gSets);
    fflush(stdout);
    _exit(1);
}

static int Setup(void)
{
    AudioStreamBasicDescription f = {
        .mSampleRate = 48000,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked |
                        kAudioFormatFlagIsNonInterleaved,
        .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4,
        .mChannelsPerFrame = 2, .mBitsPerChannel = 32,
    };
    UInt32 inputs = 1, size = sizeof inputs;
    AudioUnitGetProperty(gAu, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, 0,
                         &inputs, &size);
    AURenderCallbackStruct cb = {Input, NULL};
    /* Every input -- a side-chain too -- is fed, so none renders from nothing. */
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
    return AudioUnitInitialize(gAu) == noErr;
}

static void Stop(CFRunLoopTimerRef timer, void *info)
{
    (void) timer; (void) info;
    gRunning = 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: au_stress <bundle.component> [seconds] [state-threads]\n");
        return 2;
    }
    if (argc > 2) gSeconds = atof(argv[2]);
    if (argc > 3) gStateThreads = atoi(argv[3]);
    if (gSeconds <= 0 || gStateThreads < 1 || gStateThreads > MAX_STATE) {
        fprintf(stderr, "au_stress: seconds > 0, 1..%d state threads\n", MAX_STATE);
        return 2;
    }

    AudioComponent comp = ni_au_register(argv[1], NULL);
    if (!comp) return 1;
    if (AudioComponentInstanceNew(comp, &gAu) != noErr || !gAu) {
        printf("  FAIL: could not instantiate %s\n", argv[1]);
        return 1;
    }
    if (!Setup()) {
        printf("  FAIL: could not initialise %s\n", argv[1]);
        return 1;
    }

    pthread_t dog, render, state[MAX_STATE];
    pthread_create(&dog, NULL, Watchdog, NULL);
    pthread_create(&render, NULL, Render, NULL);
    for (int i = 0; i < gStateThreads; i++)
        pthread_create(&state[i], NULL, State, (void *) (long) (i + 1));

    /* The main thread is the plugin's main thread: its run loop is where
     * iPlug2's idle timer fires. */
    CFRunLoopTimerRef stop =
        CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + gSeconds, 0, 0, 0, Stop, NULL);
    CFRunLoopAddTimer(CFRunLoopGetMain(), stop, kCFRunLoopCommonModes);
    while (gRunning || !AllDone())
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, false);
    CFRelease(stop);

    pthread_join(render, NULL);
    for (int i = 0; i < gStateThreads; i++)
        pthread_join(state[i], NULL);
    pthread_join(dog, NULL);

    int fails = 0;
    printf("  %-40s %ld\n", "state sets (SetState on other threads)", (long) gSets);
    printf("  %-40s %ld\n", "renders", (long) gRenders);
    if (gSets < 1) { printf("  FAIL: no state was ever set\n"); fails++; }
    if (gRenders < 1) { printf("  FAIL: nothing rendered\n"); fails++; }

    /* Still a working plugin: it uninitialises and goes away cleanly. */
    if (AudioUnitUninitialize(gAu) != noErr) { printf("  FAIL: uninitialise\n"); fails++; }
    AudioComponentInstanceDispose(gAu);
    printf("%s\n", fails ? "FAIL" : "ok: every thread came back");
    return fails ? 1 : 0;
}
