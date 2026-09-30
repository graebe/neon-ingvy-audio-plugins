/*
 * Render through the Audio Unit this checkout BUILT and compare it against the
 * engine running the same settings directly.
 *
 *   sc_au_render <NISideChain.component>
 *
 * THIS IS THE ONLY TEST THAT EXERCISES THE WHOLE CHAIN AS A DAW DOES: the
 * component is loaded from its bundle -- by path, registered in this process
 * only under a test-only manufacturer (tests/au_bundle.h), so it is never an
 * installed copy answering to the same triple -- its
 * parameters are set through the AU parameter API -- the same one automation
 * drives -- the transport arrives through the host callbacks iPlug2's wrapper
 * actually reads, and the audio comes back through AudioUnitRender.
 *
 * WHY auval IS NOT ENOUGH, even though it passes. auval proves the component
 * is well-formed: it renders, it has the buses it claims, it survives the
 * property calls. It has no idea what the plugin is FOR, so a Side-Chain that
 * initialised, accepted every parameter and then passed the audio through
 * untouched would pass it cleanly.
 *
 * AVAudioEngine cannot do this job either: it supplies no musical context, so
 * the plugin correctly sees "stopped", holds the gate open and renders dry --
 * a passing render that proves nothing. The three callbacks below are the
 * entire difference.
 *
 * THE COMPONENT TYPE IS kAudioUnitType_MusicEffect ('aumf'), not 'aufx'.
 * PLUG_DOES_MIDI_IN makes it one; see config.h. The bundle's Info.plist says
 * so, and au_bundle.h registers what the plist says.
 */
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "au_bundle.h"
#include "sc_core.h"

#define SR      44100.0
#define BLOCK   128
#define BPM     120.0
#define SECONDS 2
#define FRAMES  (44100 * SECONDS)

static double gSamplePos = 0.0;   /* where the "song" is, in samples */

static OSStatus BeatAndTempo(void *ud, Float64 *beat, Float64 *tempo)
{
    (void) ud;
    if (beat)  *beat  = gSamplePos / SR * (BPM / 60.0);
    if (tempo) *tempo = BPM;
    return noErr;
}

static OSStatus MusicalTime(void *ud, UInt32 *toNextBeat, Float32 *num,
                            UInt32 *den, Float64 *downBeat)
{
    (void) ud;
    const double beat = gSamplePos / SR * (BPM / 60.0);
    if (toNextBeat) *toNextBeat = (UInt32)((1.0 - (beat - floor(beat))) * SR * 60.0 / BPM);
    if (num) *num = 4.0f;
    if (den) *den = 4;
    if (downBeat) *downBeat = floor(beat / 4.0) * 4.0;
    return noErr;
}

static OSStatus TransportState(void *ud, Boolean *playing, Boolean *changed,
                               Float64 *sampleInTimeline, Boolean *cycling,
                               Float64 *cycleStart, Float64 *cycleEnd)
{
    (void) ud;
    if (playing) *playing = true;
    if (changed) *changed = false;
    if (sampleInTimeline) *sampleInTimeline = gSamplePos;
    if (cycling) *cycling = false;
    if (cycleStart) *cycleStart = 0;
    if (cycleEnd) *cycleEnd = 0;
    return noErr;
}

/*
 * DC AT 0.5, WHICH MAKES THE OUTPUT THE GAIN.
 *
 * A sine would work too, but then every comparison has to account for where in
 * the cycle each sample sits. With a constant input, `out / 0.5` IS the gain
 * the plugin applied -- so a mismatch names itself instead of needing a phase
 * argument to interpret.
 */
static OSStatus Source(void *ud, AudioUnitRenderActionFlags *flags,
                       const AudioTimeStamp *ts, UInt32 bus, UInt32 frames,
                       AudioBufferList *io)
{
    (void) ud; (void) flags; (void) ts; (void) bus;
    for (UInt32 ch = 0; ch < io->mNumberBuffers; ch++) {
        float *p = (float *) io->mBuffers[ch].mData;
        for (UInt32 i = 0; i < frames; i++) p[i] = 0.5f;
    }
    return noErr;
}

static AudioUnitParameterID param_id(AudioUnit au, const char *name,
                                     AudioUnitParameterInfo *out)
{
    UInt32 size = 0;
    if (AudioUnitGetPropertyInfo(au, kAudioUnitProperty_ParameterList,
                                 kAudioUnitScope_Global, 0, &size, NULL) != noErr)
        return (AudioUnitParameterID) -1;

    const int n = (int)(size / sizeof(AudioUnitParameterID));
    AudioUnitParameterID ids[64];
    if (n > 64 || n <= 0) return (AudioUnitParameterID) -1;
    AudioUnitGetProperty(au, kAudioUnitProperty_ParameterList,
                         kAudioUnitScope_Global, 0, ids, &size);

    for (int i = 0; i < n; i++) {
        AudioUnitParameterInfo info;
        UInt32 isz = sizeof(info);
        if (AudioUnitGetProperty(au, kAudioUnitProperty_ParameterInfo,
                                 kAudioUnitScope_Global, ids[i], &info, &isz) != noErr)
            continue;
        char buf[128] = {0};
        if (info.cfNameString)
            CFStringGetCString(info.cfNameString, buf, sizeof buf, kCFStringEncodingUTF8);
        else
            strncpy(buf, info.name, sizeof(buf) - 1);
        if (strcmp(buf, name) == 0) {
            if (out) *out = info;
            return ids[i];
        }
    }
    return (AudioUnitParameterID) -1;
}

static int fails = 0;
static void ok(int cond, const char *what, const char *detail)
{
    printf("  %-56s %s%s%s%s\n", what, cond ? "ok" : "FAIL",
           detail ? " (" : "", detail ? detail : "", detail ? ")" : "");
    if (!cond) fails++;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: sc_au_render <NISideChain.component>\n");
        return 2;
    }
    AudioComponent comp = ni_au_register(argv[1], NULL);
    if (!comp)
        return 1;

    AudioUnit au = NULL;
    if (AudioComponentInstanceNew(comp, &au) != noErr || !au) {
        printf("  FAIL: could not instantiate the AU\n");
        return 1;
    }

    AudioStreamBasicDescription fmt = {
        .mSampleRate = SR,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked
                      | kAudioFormatFlagIsNonInterleaved,
        .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4,
        .mChannelsPerFrame = 2, .mBitsPerChannel = 32,
    };
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Input, 0, &fmt, sizeof fmt);
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Output, 0, &fmt, sizeof fmt);

    UInt32 maxFrames = BLOCK;
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice,
                         kAudioUnitScope_Global, 0, &maxFrames, sizeof maxFrames);

    HostCallbackInfo host = {
        .hostUserData = NULL,
        .beatAndTempoProc = BeatAndTempo,
        .musicalTimeLocationProc = MusicalTime,
        .transportStateProc = TransportState,
    };
    ok(AudioUnitSetProperty(au, kAudioUnitProperty_HostCallbacks,
                            kAudioUnitScope_Global, 0, &host, sizeof host) == noErr,
       "the AU accepts a host transport", NULL);

    AURenderCallbackStruct cb = { .inputProc = Source, .inputProcRefCon = NULL };
    AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback,
                         kAudioUnitScope_Input, 0, &cb, sizeof cb);

    ok(AudioUnitInitialize(au) == noErr, "it initialises", NULL);

    /*
     * THE SETTINGS, THROUGH THE PARAMETER API -- which is the point of the
     * test. Each one travels the whole way: AU parameter id, iPlug2's wrapper,
     * IParam, PushParams, sc_core_set_num. A break anywhere in that chain shows
     * up as a different render and nowhere else.
     *
     * THE RANGE IS READ, NOT ASSUMED. iPlug2 publishes an InitDouble's real
     * range and an enum's as 0..nEnums-1, but assuming that and being wrong is
     * silent: the value clamps and the plugin does something reasonable with a
     * setting nobody chose. Every parameter below is checked against the
     * maximum the AU itself reports.
     */
    struct { const char *name; float value; } patch[] = {
        { "Source",  0.0f },     /* Cycle */
        { "Rate",    4.0f },     /* 1/4 -- the fifth entry of the rate table */
        { "Depth", 100.0f },     /* percent */
        { "Delay",   0.0f },
        { "Attack",  4.0f },     /* percent of the cycle */
        { "Hold",   10.0f },
        { "Release", 40.0f },
        { "Curve",   1.0f },     /* Exponential */
    };

    for (size_t i = 0; i < sizeof patch / sizeof patch[0]; i++) {
        AudioUnitParameterInfo info;
        const AudioUnitParameterID id = param_id(au, patch[i].name, &info);
        char what[96], detail[96];
        snprintf(what, sizeof what, "\"%s\" is an AU parameter", patch[i].name);
        ok(id != (AudioUnitParameterID) -1, what, NULL);
        if (id == (AudioUnitParameterID) -1) continue;

        snprintf(what, sizeof what, "  and %g is inside its published range", patch[i].value);
        snprintf(detail, sizeof detail, "%g..%g", info.minValue, info.maxValue);
        ok(patch[i].value >= info.minValue && patch[i].value <= info.maxValue, what, detail);

        AudioUnitSetParameter(au, id, kAudioUnitScope_Global, 0, patch[i].value, 0);

        Float32 back = -1.0f;
        AudioUnitGetParameter(au, id, kAudioUnitScope_Global, 0, &back);
        snprintf(what, sizeof what, "  and it reads back");
        snprintf(detail, sizeof detail, "%g", back);
        ok(fabsf(back - patch[i].value) < 1e-3f, what, detail);
    }

    /* ------------------------------------------------------- the render --- */

    static float outL[FRAMES], outR[FRAMES];
    AudioBufferList *abl = (AudioBufferList *)
        malloc(sizeof(AudioBufferList) + sizeof(AudioBuffer));
    abl->mNumberBuffers = 2;

    AudioTimeStamp ts = {0};
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    gSamplePos = 0.0;

    OSStatus renderErr = noErr;
    for (int off = 0; off + BLOCK <= FRAMES; off += BLOCK) {
        ts.mSampleTime = (Float64) off;
        for (int ch = 0; ch < 2; ch++) {
            abl->mBuffers[ch].mNumberChannels = 1;
            abl->mBuffers[ch].mDataByteSize = BLOCK * sizeof(float);
            abl->mBuffers[ch].mData = (ch ? outR : outL) + off;
        }
        AudioUnitRenderActionFlags flags = 0;
        const OSStatus e = AudioUnitRender(au, &flags, &ts, 0, BLOCK, abl);
        if (e != noErr) { renderErr = e; break; }
        gSamplePos += BLOCK;
    }
    ok(renderErr == noErr, "it renders", NULL);

    /* ---------------------------------------------- against the engine ---- */

    static float engL[FRAMES], engR[FRAMES];
    for (int i = 0; i < FRAMES; i++) { engL[i] = 0.5f; engR[i] = 0.5f; }

    sc_core_t *c = sc_core_create(SR);
    sc_core_set_num(c, SC_P_SOURCE, 0);
    sc_core_set_param(c, "rate", "1/4");
    sc_core_set_num(c, SC_P_DEPTH, 1.0);
    sc_core_set_num(c, SC_P_DELAY, 0.0);
    sc_core_set_num(c, SC_P_ATTACK, 4.0);
    sc_core_set_num(c, SC_P_HOLD, 10.0);
    sc_core_set_num(c, SC_P_RELEASE, 40.0);
    sc_core_set_num(c, SC_P_CURVE, 1.0);
    for (int off = 0; off + BLOCK <= FRAMES; off += BLOCK) {
        sc_transport_t t;
        t.running = 1;
        t.bpm = (float) BPM;
        t.beats = (double) off / SR * (BPM / 60.0);
        sc_core_process_f32_split(c, engL + off, engR + off, BLOCK, &t);
    }
    sc_core_destroy(c);

    double worst = 0.0;
    int worst_at = -1;
    for (int i = 0; i < FRAMES - BLOCK; i++) {
        const double d = fabs((double) outL[i] - (double) engL[i]);
        if (d > worst) { worst = d; worst_at = i; }
    }
    char detail[96];
    snprintf(detail, sizeof detail, "largest difference %.6f at sample %d", worst, worst_at);
    /*
     * 1e-4, AND IT MEASURES 0.000000 -- the tolerance is headroom, not a known
     * discrepancy. Recorded because the two are easy to confuse later: a reader
     * who finds a loose tolerance usually assumes something does not quite line
     * up, and here everything does.
     *
     * The headroom is for the AU path converting through iPlug2's `sample`
     * (double) and back, and for a host handing the transport over in units
     * that need not land on exactly the doubles this loop computes -- either
     * could move the phase-locked loop's correction in the last places. A real
     * break in the chain is not a thousandth; it is the whole duck.
     */
    ok(worst < 1e-4, "the AU renders what the engine renders", detail);

    /* --------------------------------------------- and it actually ducked -- */

    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < FRAMES - BLOCK; i++) {
        if (outL[i] < lo) lo = outL[i];
        if (outL[i] > hi) hi = outL[i];
    }
    snprintf(detail, sizeof detail, "%.4f .. %.4f of a 0.5 input", lo, hi);
    /*
     * THE CHECK auval CANNOT MAKE. A plugin that initialised, took every
     * parameter and passed the audio through would satisfy every other
     * assertion in this file.
     */
    ok(lo < 0.05f, "the transport actually reached the duck", detail);
    ok(hi > 0.45f, "and it opened again between triggers", detail);

    /* Both channels, the same gain: the engine applies one gain law to both. */
    int stereo_ok = 1;
    for (int i = 0; i < FRAMES - BLOCK; i++)
        if (outL[i] != outR[i]) { stereo_ok = 0; break; }
    ok(stereo_ok, "both channels get the same gain", NULL);

    AudioUnitUninitialize(au);
    AudioComponentInstanceDispose(au);
    free(abl);

    printf(fails ? "\nFAILED (%d)\n" : "\nPASS\n", fails);
    return fails ? 1 : 0;
}
