/*
 * Generates the ENVELOPE fixture -- and it MEASURES rather than transcribes.
 *
 * The trick, which is the Trance Gate's and worth stating again: drive the real
 * DSP with a DC input of 1.0 at depth 1.0, and the output sample IS the gain,
 * so it is `1 - duck` read straight off the thing that makes the sound. No
 * model of the envelope is consulted anywhere in producing this file.
 *
 * That is what caught `att_from` in the Trance Gate: five of sixty measured
 * cases started part way up the scale while the model insisted they started at
 * zero, and every one of them was a retrigger.
 *
 * Regenerate with
 *
 *     ./build/engines/pump/pump_envelope_table > plugins/pump/ui/test/envelope_table.txt
 *
 * Format: a "# case" line naming the settings, then one value per sample.
 */

#include <stdio.h>
#include <string.h>
#include "pump_core.h"

#define SR 48000.0
/* 120 bpm, 1/4 -> 24 000 samples per cycle. Decimated by 64 so a case is 375
 * rows rather than 24 000: the shape is what is being pinned, and every 64th
 * sample of a stage hundreds of samples long describes it completely. */
#define DECIMATE 64
#define FRAMES   (24000 + 4096)

static void one_case(const char *name, int curve,
                     double delay, double attack, double hold, double release,
                     int retrigger_at)
{
    pump_core_t *c = pump_core_create(SR);
    pump_core_set_num(c, PUMP_P_SOURCE, 1);        /* MIDI: fires on demand   */
    pump_core_set_num(c, PUMP_P_DEPTH, 1.0);       /* so output == 1 - duck   */
    pump_core_set_num(c, PUMP_P_CURVE, curve);
    pump_core_set_num(c, PUMP_P_DELAY, delay);
    pump_core_set_num(c, PUMP_P_ATTACK, attack);
    pump_core_set_num(c, PUMP_P_HOLD, hold);
    pump_core_set_num(c, PUMP_P_RELEASE, release);

    printf("# case %s curve=%d delay=%g attack=%g hold=%g release=%g retrigger=%d\n",
           name, curve, delay, attack, hold, release, retrigger_at);

    const unsigned char on[3] = { 0x90, 36, 127 };
    /* One block, so the whole envelope is rendered against one tempo and the
     * sample offsets are unambiguous. */
    static float l[FRAMES], r[FRAMES];
    for (int i = 0; i < FRAMES; i++) { l[i] = 1.0f; r[i] = 1.0f; }

    pump_core_on_midi(c, on, 3, 0);
    if (retrigger_at > 0 && retrigger_at < FRAMES)
        pump_core_on_midi(c, on, 3, retrigger_at);

    /* No transport: the MIDI source does not need one, and leaving it out
     * keeps the cycle's phase-locked loop out of the measurement. */
    pump_core_process_f32_split(c, l, r, FRAMES, NULL);

    for (int i = 0; i < FRAMES; i += DECIMATE)
        printf("%d %.9g\n", i, (double)l[i]);

    pump_core_destroy(c);
}

int main(void)
{
    printf("# sample gain -- the OUTPUT of a DC input at depth 1, i.e. 1 - duck\n");
    printf("# sample rate %g, 120 bpm, rate 1/4 (24000 samples per cycle)\n", SR);
    for (int curve = 0; curve < 4; curve++) {
        one_case("plain",        curve,  0,  5, 10,  40, 0);
        one_case("delayed",      curve, 20,  5, 10,  40, 0);
        one_case("slow-attack",  curve,  0, 60,  5,  30, 0);
        one_case("no-attack",    curve,  0,  0, 20,  50, 0);
        one_case("no-hold",      curve,  0, 10,  0,  60, 0);
        one_case("no-release",   curve,  0, 10, 20,   0, 0);
        one_case("long",         curve,  0, 25, 25, 150, 0);
        /* THE RETRIGGER CASES ARE THE POINT. A second trigger part way down
         * the release must anchor on the level the envelope is actually at. */
        one_case("retrig-rel",   curve,  0, 10, 10, 150, 9000);
        one_case("retrig-att",   curve,  0, 60, 10,  40, 600);
    }
    return 0;
}
