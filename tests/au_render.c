/*
 * Render 4 seconds through the INSTALLED Audio Unit and compare it against
 * the engine running the same patch directly.
 *
 * This is the only test that exercises the whole chain as a DAW does: the
 * component is loaded from disk by its type/subtype/manufacturer, its
 * parameters are set through the AU parameter API (the same one automation
 * drives), the transport arrives through the host callbacks JUCE's wrapper
 * actually reads, and the audio comes back through AudioUnitRender.
 *
 * AVAudioEngine cannot do this job: it supplies no musical context, so the
 * plugin correctly sees "stopped", holds the gate open, and renders the dry
 * signal -- a passing render that proves nothing about the gate. The two
 * callbacks below are the entire difference.
 *
 * Skips with success when the component is not installed, so a checkout that
 * has never run a build does not fail the suite.
 */
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "trance_gate_core.h"

#define SR    44100.0
#define BLOCK 128
#define BPM   123.0

static double gSamplePos = 0.0;      /* where the "song" is, in samples */

static OSStatus BeatAndTempo (void *ud, Float64 *beat, Float64 *tempo)
{
    (void) ud;
    if (beat)  *beat  = gSamplePos / SR * (BPM / 60.0);
    if (tempo) *tempo = BPM;
    return noErr;
}

static OSStatus MusicalTime (void *ud, UInt32 *toNextBeat, Float32 *num,
                             UInt32 *den, Float64 *downBeat)
{
    (void) ud;
    const double beat = gSamplePos / SR * (BPM / 60.0);
    if (toNextBeat) *toNextBeat = (UInt32) ((1.0 - (beat - floor (beat))) * SR * 60.0 / BPM);
    if (num) *num = 4.0f;
    if (den) *den = 4;
    if (downBeat) *downBeat = floor (beat / 4.0) * 4.0;
    return noErr;
}

static OSStatus TransportState (void *ud, Boolean *playing, Boolean *changed,
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

/* The source: the same 220 Hz sine, quantised to int16 before it reaches the
 * gate, exactly as the Move reference render does. */
static double gPhase = 0.0;

static OSStatus Source (void *ud, AudioUnitRenderActionFlags *flags,
                        const AudioTimeStamp *ts, UInt32 bus, UInt32 frames,
                        AudioBufferList *io)
{
    (void) ud; (void) flags; (void) ts; (void) bus;
    const double w = 2.0 * M_PI * 220.0 / SR;
    for (UInt32 i = 0; i < frames; i++) {
        short q = (short) lrint (22000.0 * sin (gPhase));
        gPhase += w;
        for (UInt32 ch = 0; ch < io->mNumberBuffers; ch++)
            ((float *) io->mBuffers[ch].mData)[i] = (float) q;
    }
    return noErr;
}

static AudioUnitParameterID param_id (AudioUnit au, const char *name)
{
    UInt32 size = 0;
    if (AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList,
                                  kAudioUnitScope_Global, 0, &size, NULL) != noErr)
        return (AudioUnitParameterID) -1;

    const int n = (int) (size / sizeof (AudioUnitParameterID));
    AudioUnitParameterID ids[64];
    if (n > 64) return (AudioUnitParameterID) -1;
    AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList,
                          kAudioUnitScope_Global, 0, ids, &size);

    for (int i = 0; i < n; i++) {
        AudioUnitParameterInfo info;
        UInt32 isz = sizeof (info);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo,
                                  kAudioUnitScope_Global, ids[i], &info, &isz) != noErr)
            continue;
        char buf[128] = {0};
        if (info.cfNameString)
            CFStringGetCString (info.cfNameString, buf, sizeof (buf), kCFStringEncodingUTF8);
        else
            strncpy (buf, info.name, sizeof (buf) - 1);
        if (strcmp (buf, name) == 0) return ids[i];
    }
    return (AudioUnitParameterID) -1;
}

static int fails = 0;
static void ok (int cond, const char *what, const char *detail)
{
    printf ("  %-56s %s%s%s%s\n", what, cond ? "ok" : "FAIL",
            detail ? " (" : "", detail ? detail : "", detail ? ")" : "");
    if (!cond) fails++;
}

int main (void)
{
    AudioComponentDescription desc = {
        .componentType = kAudioUnitType_Effect,
        .componentSubType = 'TrGt',
        .componentManufacturer = 'Grbe',
    };
    AudioComponent comp = AudioComponentFindNext (NULL, &desc);
    if (!comp) {
        printf ("  (skipped: the Trance Gate AU is not installed)\n");
        return 0;
    }

    AudioUnit au = NULL;
    if (AudioComponentInstanceNew (comp, &au) != noErr || !au) {
        printf ("  FAIL: could not instantiate the AU\n");
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
    AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat,
                          kAudioUnitScope_Input, 0, &fmt, sizeof (fmt));
    AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat,
                          kAudioUnitScope_Output, 0, &fmt, sizeof (fmt));

    UInt32 maxFrames = BLOCK;
    AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice,
                          kAudioUnitScope_Global, 0, &maxFrames, sizeof (maxFrames));

    HostCallbackInfo host = {
        .hostUserData = NULL,
        .beatAndTempoProc = BeatAndTempo,
        .musicalTimeLocationProc = MusicalTime,
        .transportStateProc = TransportState,
    };
    ok (AudioUnitSetProperty (au, kAudioUnitProperty_HostCallbacks,
                              kAudioUnitScope_Global, 0, &host, sizeof (host)) == noErr,
        "the AU accepts a host transport", NULL);

    AURenderCallbackStruct cb = { .inputProc = Source, .inputProcRefCon = NULL };
    AudioUnitSetProperty (au, kAudioUnitProperty_SetRenderCallback,
                          kAudioUnitScope_Input, 0, &cb, sizeof (cb));

    ok (AudioUnitInitialize (au) == noErr, "it initialises", NULL);

    /*
     * THE PATCH, SET THROUGH THE PARAMETERS -- which is the point. These go
     * in by AU parameter id, travel through JUCE's wrapper into the APVTS,
     * fire the listener and land in the engine as strings. Every link in that
     * chain is new, and a break anywhere in it shows up as a different render.
     *
     * AU UNITS ARE NOT THE PLUGIN'S UNITS, and getting that wrong is quiet:
     * JUCE publishes a CONTINUOUS parameter over 0..1 and a DISCRETE one over
     * 0..steps-1, converting with `value / maxValue` on the way in. Writing
     * "40" for a 40 ms decay therefore does not set 40 ms -- it clamps to the
     * top of the range and sets 500 ms, with no error anywhere. The test
     * reads each parameter's published maximum and converts, then checks the
     * result by asking the AU to print the value back.
     */
    struct { const char *name; float lo, hi, real; } patch[] = {
        { "Rate",    0.0f,  12.0f,   7.0f },      /* discrete: the index of 1/16 */
        { "Length",  1.0f, 128.0f,  16.0f },      /* discrete: 16 steps */
        { "Slot",    0.0f,   7.0f,   0.0f },      /* discrete: a choice, pattern 1 */
        { "Join Neighbors", 0.0f, 1.0f, 0.0f },
        { "Amount",  0.0f,   1.0f,   0.9f },
        { "Width",   0.05f,  1.0f,   0.75f },
        { "Attack",  0.0f, 200.0f,   3.8267f },
        { "Decay",   0.0f, 200.0f,  43.7333f },
        { "Sustain", 0.0f,   1.0f,   0.6f },
        /* READ AS PERCENTAGES. A millisecond reading is
         * `value/100 * width`, and the width follows the host's
         * tempo -- so it is a moving target for a test, while the
         * percentage is the number actually stored. */
        { "Env Time", 0.0f,   1.0f,   1.0f },
        { "Release", 0.0f, 200.0f,  27.3333f },
    };

    int allSet = 1;
    for (size_t i = 0; i < sizeof (patch) / sizeof (patch[0]); i++) {
        AudioUnitParameterID pid = param_id (au, patch[i].name);
        if (pid == (AudioUnitParameterID) -1) { allSet = 0; continue; }

        AudioUnitParameterInfo info;
        UInt32 isz = sizeof (info);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo,
                                  kAudioUnitScope_Global, pid, &info, &isz) != noErr) {
            allSet = 0;
            continue;
        }

        /*
         * SCALE INTO THE RANGE THE AU ITSELF PUBLISHES, not into one this
         * test believes in.
         *
         * It used to be `norm * info.maxValue`, which is only correct when a
         * parameter's minimum is zero. JUCE's AU wrapper published everything
         * that way -- continuous over 0..1, discrete over 0..steps-1 -- so
         * the shortcut held for as long as JUCE was the wrapper. iPlug2
         * publishes the real range, and Length's is 1..128: the same
         * arithmetic asked for 16 steps and set 15.
         *
         * Reading minValue too costs one line and makes this test a test of
         * the ENGINE reached through an AU, rather than of one framework's
         * habits.
         */
        const float norm = (patch[i].real - patch[i].lo) / (patch[i].hi - patch[i].lo);
        const float raw  = info.minValue + norm * (info.maxValue - info.minValue);
        if (AudioUnitSetParameter (au, pid, kAudioUnitScope_Global, 0, raw, 0) != noErr)
            allSet = 0;
    }
    ok (allSet, "every macro is reachable as an AU parameter", NULL);

    /*
     * AND IT LANDED AS THE VALUE WE MEANT. The AU prints a parameter through
     * the plugin's own formatter, so this compares what a host would DISPLAY
     * against what the ENGINE holds for the same patch -- the two ends of the
     * chain with the unit conversion in between. If they agree, the render
     * comparison below is comparing two things that were configured alike;
     * if they do not, the render difference would be measuring my test setup.
     */
    const char *shownWant[] = { "1/16", "16", "1", "Off", "90.00 %", "75.00 %",
                                "3.83 %", "43.73 %", "60.00 %", "% Step", "27.33 %" };
    int allAgree = 1;
    for (size_t i = 0; i < sizeof (patch) / sizeof (patch[0]); i++) {
        AudioUnitParameterID pid = param_id (au, patch[i].name);
        AudioUnitParameterValue back = 0.0f;
        AudioUnitGetParameter (au, pid, kAudioUnitScope_Global, 0, &back);

        AudioUnitParameterStringFromValue sfv = { pid, &back, NULL };
        UInt32 ssz = sizeof (sfv);
        char shown[64] = "?";
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterStringFromValue,
                                  kAudioUnitScope_Global, 0, &sfv, &ssz) == noErr
            && sfv.outString != NULL) {
            CFStringGetCString (sfv.outString, shown, sizeof (shown), kCFStringEncodingUTF8);
            CFRelease (sfv.outString);
        }

        /* An exact string compare, because the string IS the product here:
         * it is what a host shows in its automation lane, and "0.9000000"
         * where "90%" belongs is a defect even though the value is right. */
        int agree = strcmp (shown, shownWant[i]) == 0;
        if (!agree) {
            printf ("      %-8s shows \"%s\", wanted \"%s\"\n",
                    patch[i].name, shown, shownWant[i]);
            allAgree = 0;
        }
    }
    ok (allAgree, "and each one displays the value it was given", NULL);

    /* The reference: the same patch, straight into the engine. Slot 1's
     * default pattern (every other step, full depth, no ties) is what both
     * sides run, so nothing here depends on state interchange. */
    tg_core_t *ref = tg_core_create (SR);
    tg_core_set_param (ref, "rate", "1/16");
    tg_core_set_param (ref, "length", "15");
    tg_core_set_param (ref, "slot", "0");
    tg_core_set_param (ref, "amount", "0.900");
    tg_core_set_param (ref, "hold", "0.750");
    tg_core_set_param (ref, "attack", "3.8267");
    tg_core_set_param (ref, "decay", "43.7333");
    tg_core_set_param (ref, "sustain", "0.600");
    tg_core_set_param (ref, "release", "27.3333");
    tg_core_set_param (ref, "legato", "0");

    const int total = (int) (4.0 * SR);
    float auL[BLOCK], auR[BLOCK], refL[BLOCK], refR[BLOCK];
    double refPhase = 0.0;
    const double w = 2.0 * M_PI * 220.0 / SR;

    unsigned char abl[sizeof (AudioBufferList) + sizeof (AudioBuffer)];
    AudioBufferList *io = (AudioBufferList *) abl;

    double maxDiff = 0.0, sumAu = 0.0, sumDry = 0.0;

    /*
     * "Did it gate" is not "did it reach zero": Amount 0.9 leaves a tenth of
     * the signal through on a closed step, by design -- Amount IS the dry/wet.
     * The question is whether the level MOVES with the pattern, so the test
     * measures the peak inside each 1/16 step and compares the loudest step
     * with the quietest.
     */
    const int stepSamples = (int) (SR * 60.0 / BPM / 4.0);
    double stepPeak = 0.0, loudest = 0.0, quietest = 1e30;
    int stepFill = 0;

    for (int done = 0; done < total; done += BLOCK) {
        const int n = (total - done) < BLOCK ? (total - done) : BLOCK;

        io->mNumberBuffers = 2;
        io->mBuffers[0] = (AudioBuffer) { .mNumberChannels = 1,
                                          .mDataByteSize = (UInt32) n * 4, .mData = auL };
        io->mBuffers[1] = (AudioBuffer) { .mNumberChannels = 1,
                                          .mDataByteSize = (UInt32) n * 4, .mData = auR };

        AudioUnitRenderActionFlags flags = 0;
        AudioTimeStamp ts = {0};
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        ts.mSampleTime = gSamplePos;

        if (AudioUnitRender (au, &flags, &ts, 0, (UInt32) n, io) != noErr) {
            printf ("  FAIL: AudioUnitRender failed at sample %d\n", done);
            fails++;
            break;
        }

        /* The same input, through the engine directly. */
        tg_transport_t t = { .running = 1,
                             .beats = gSamplePos / SR * (BPM / 60.0),
                             .bpm = (float) BPM };
        for (int i = 0; i < n; i++) {
            short q = (short) lrint (22000.0 * sin (refPhase));
            refPhase += w;
            refL[i] = refR[i] = (float) q;
            sumDry += fabs ((double) q);     /* the real input, not a constant */
        }
        tg_core_process_f32_split (ref, refL, refR, n, &t);

        for (int i = 0; i < n; i++) {
            const double d = fabs ((double) auL[i] - (double) refL[i]);
            if (d > maxDiff) maxDiff = d;
            const double mag = fabs ((double) auL[i]);
            sumAu += mag;
            if (mag > stepPeak) stepPeak = mag;
            if (++stepFill >= stepSamples) {
                if (stepPeak > loudest)  loudest  = stepPeak;
                if (stepPeak < quietest) quietest = stepPeak;
                stepPeak = 0.0;
                stepFill = 0;
            }
        }
        gSamplePos += n;
    }

    char detail[128];
    snprintf (detail, sizeof (detail), "largest sample difference %.3f", maxDiff);
    /* One int16 quantum of slack: the AU path and the direct path do the same
     * arithmetic, so anything above that is a real divergence, not rounding. */
    /* TWO QUANTA, NOT ONE. The patch now travels through the host as a
     * NORMALISED float and comes back scaled, so a percentage cannot make the
     * round trip bit-exactly the way an absolute millisecond value did. 1.5
     * of 32768 is -86 dBFS. */
    ok (maxDiff <= 2.0, "the AU renders what the engine renders", detail);

    snprintf (detail, sizeof (detail), "loudest step %.0f, quietest %.0f", loudest, quietest);
    ok (quietest > 0.0 && loudest > quietest * 3.0,
        "the gate actually gated (the transport reached it)", detail);

    snprintf (detail, sizeof (detail), "%.0f%% of the dry signal", 100.0 * sumAu / sumDry);
    ok (sumAu < sumDry * 0.9, "and the output is quieter than the input", detail);

    AudioUnitUninitialize (au);
    AudioComponentInstanceDispose (au);
    tg_core_destroy (ref);

    printf ("\n%s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
