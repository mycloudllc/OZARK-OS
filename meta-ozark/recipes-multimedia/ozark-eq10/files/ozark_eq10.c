/*
 * ozark_eq10: 10-band stereo graphic equalizer as a LADSPA plugin.
 *
 * Peaking filters (RBJ Audio EQ Cookbook) on the ISO octave bands 31 Hz to
 * 16 kHz, +/-12 dB each, plus a preamp. PulseAudio runs it through
 * module-ladspa-sink; Dash changes the gains live over PulseAudio's D-Bus
 * interface (org.PulseAudio.Ext.Ladspa1), so filters are recalculated when a
 * control changes, not per sample.
 *
 * SPDX-License-Identifier: MIT
 */

#include <ladspa.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BANDS 10
#define CHANNELS 2

enum {
    PORT_IN_L,
    PORT_IN_R,
    PORT_OUT_L,
    PORT_OUT_R,
    PORT_GAIN0,                       /* PORT_GAIN0 .. PORT_GAIN0 + 9 */
    PORT_PREAMP = PORT_GAIN0 + BANDS,
    PORT_COUNT
};

static const double band_hz[BANDS] = {31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
static const char *band_names[BANDS] = {"31 Hz", "62 Hz", "125 Hz", "250 Hz", "500 Hz",
                                        "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz"};
/* One octave per band */
#define BAND_Q 1.414

typedef struct {
    float b0, b1, b2, a1, a2;
} Coeffs;

typedef struct {
    float z1, z2;
} State;

typedef struct {
    LADSPA_Data *ports[PORT_COUNT];
    double rate;
    float gains[BANDS];  /* last values the filters were computed for */
    float preamp_db;
    float preamp;
    int bands;           /* bands below Nyquist at this sample rate */
    Coeffs coeffs[BANDS];
    State state[CHANNELS][BANDS];
} Eq;

static void compute(Eq *eq)
{
    for (int b = 0; b < eq->bands; b++) {
        const double gain_db = eq->gains[b];
        Coeffs *c = &eq->coeffs[b];
        if (fabs(gain_db) < 0.01) {
            /* 0 dB: pass through exactly */
            c->b0 = 1.0f;
            c->b1 = c->b2 = c->a1 = c->a2 = 0.0f;
            continue;
        }
        const double a = pow(10.0, gain_db / 40.0);
        const double w0 = 2.0 * M_PI * band_hz[b] / eq->rate;
        const double alpha = sin(w0) / (2.0 * BAND_Q);
        const double cosw = cos(w0);
        const double a0 = 1.0 + alpha / a;
        c->b0 = (float)((1.0 + alpha * a) / a0);
        c->b1 = (float)((-2.0 * cosw) / a0);
        c->b2 = (float)((1.0 - alpha * a) / a0);
        c->a1 = (float)((-2.0 * cosw) / a0);
        c->a2 = (float)((1.0 - alpha / a) / a0);
    }
    eq->preamp = (float)pow(10.0, eq->preamp_db / 20.0);
}

static LADSPA_Handle instantiate(const LADSPA_Descriptor *descriptor, unsigned long rate)
{
    (void)descriptor;
    Eq *eq = calloc(1, sizeof(Eq));
    if (!eq)
        return NULL;
    eq->rate = (double)rate;
    eq->bands = 0;
    while (eq->bands < BANDS && band_hz[eq->bands] < 0.45 * eq->rate)
        eq->bands++;
    eq->preamp = 1.0f;
    compute(eq);
    return eq;
}

static void connect_port(LADSPA_Handle handle, unsigned long port, LADSPA_Data *data)
{
    Eq *eq = handle;
    if (port < PORT_COUNT)
        eq->ports[port] = data;
}

static void activate(LADSPA_Handle handle)
{
    Eq *eq = handle;
    memset(eq->state, 0, sizeof(eq->state));
}

static float clamp_db(const LADSPA_Data *port)
{
    float v = port ? *port : 0.0f;
    if (v < -12.0f)
        v = -12.0f;
    if (v > 12.0f)
        v = 12.0f;
    return v;
}

static void run(LADSPA_Handle handle, unsigned long samples)
{
    Eq *eq = handle;

    /* Recalculate only when a control moved */
    int changed = 0;
    for (int b = 0; b < BANDS; b++) {
        const float g = clamp_db(eq->ports[PORT_GAIN0 + b]);
        if (g != eq->gains[b]) {
            eq->gains[b] = g;
            changed = 1;
        }
    }
    const float p = clamp_db(eq->ports[PORT_PREAMP]);
    if (p != eq->preamp_db) {
        eq->preamp_db = p;
        changed = 1;
    }
    if (changed)
        compute(eq);

    for (int ch = 0; ch < CHANNELS; ch++) {
        const LADSPA_Data *in = eq->ports[PORT_IN_L + ch];
        LADSPA_Data *out = eq->ports[PORT_OUT_L + ch];
        if (!in || !out)
            continue;

        for (unsigned long i = 0; i < samples; i++) {
            float x = in[i] * eq->preamp;
            for (int b = 0; b < eq->bands; b++) {
                const Coeffs *c = &eq->coeffs[b];
                State *s = &eq->state[ch][b];
                /* Transposed direct form II */
                const float y = c->b0 * x + s->z1;
                s->z1 = c->b1 * x - c->a1 * y + s->z2;
                s->z2 = c->b2 * x - c->a2 * y;
                x = y;
            }
            out[i] = x;
        }

        /* Flush denormals once per block */
        for (int b = 0; b < eq->bands; b++) {
            State *s = &eq->state[ch][b];
            if (fabsf(s->z1) < 1e-20f)
                s->z1 = 0.0f;
            if (fabsf(s->z2) < 1e-20f)
                s->z2 = 0.0f;
        }
    }
}

static void cleanup(LADSPA_Handle handle)
{
    free(handle);
}

static LADSPA_Descriptor descriptor;
static LADSPA_PortDescriptor port_descriptors[PORT_COUNT];
static const char *port_names[PORT_COUNT];
static LADSPA_PortRangeHint port_hints[PORT_COUNT];

__attribute__((constructor)) static void init(void)
{
    port_descriptors[PORT_IN_L] = port_descriptors[PORT_IN_R] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
    port_descriptors[PORT_OUT_L] = port_descriptors[PORT_OUT_R] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
    port_names[PORT_IN_L] = "Input L";
    port_names[PORT_IN_R] = "Input R";
    port_names[PORT_OUT_L] = "Output L";
    port_names[PORT_OUT_R] = "Output R";

    for (int p = PORT_GAIN0; p <= PORT_PREAMP; p++) {
        port_descriptors[p] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        port_names[p] = (p == PORT_PREAMP) ? "Preamp (dB)" : band_names[p - PORT_GAIN0];
        port_hints[p].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0;
        port_hints[p].LowerBound = -12.0f;
        port_hints[p].UpperBound = 12.0f;
    }

    /* Any value outside the reserved 1-1000 range; not registered */
    descriptor.UniqueID = 4810;
    descriptor.Label = "ozark_eq10";
    descriptor.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
    descriptor.Name = "Ozark 10-band equalizer";
    descriptor.Maker = "Ozark Mountain Technologies";
    descriptor.Copyright = "MIT";
    descriptor.PortCount = PORT_COUNT;
    descriptor.PortDescriptors = port_descriptors;
    descriptor.PortNames = port_names;
    descriptor.PortRangeHints = port_hints;
    descriptor.instantiate = instantiate;
    descriptor.connect_port = connect_port;
    descriptor.activate = activate;
    descriptor.run = run;
    descriptor.cleanup = cleanup;
}

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index)
{
    return index == 0 ? &descriptor : NULL;
}
