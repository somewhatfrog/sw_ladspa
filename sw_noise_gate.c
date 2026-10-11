#include <ladspa.h>
#include <math.h>
#include <stdlib.h>

#define BUNDLE_ID 1923

static inline float db_to_mul(const LADSPA_Data db)
{
	return powf(10.0f, db / 20.0f);
}

typedef struct {
	LADSPA_Data sample_rate;

	LADSPA_Data *params[6];

	LADSPA_Data is_open;
	LADSPA_Data attenuation;
	LADSPA_Data level;
	LADSPA_Data held_time;

	unsigned long ch;
	unsigned long idx;
	unsigned long cap;

	LADSPA_Data *dl[2];
	LADSPA_Data *in[2];
	LADSPA_Data *out[2];
} NoiseGate;

static LADSPA_Data control_defaults[6] = { -32.0f, -26.0f, 25.0f, 200.0f, 150.0f, 25.0f };

static LADSPA_Handle instantiate(const LADSPA_Descriptor *descriptor, unsigned long sample_rate)
{
	NoiseGate *ng = calloc(1, sizeof(NoiseGate));
	if (!ng)
		return NULL;
	ng->sample_rate = (LADSPA_Data)sample_rate;
	ng->ch = descriptor->PortCount == 8 ? 1 : 2;
	for (unsigned long p = 0; p < 6; p++)
		ng->params[p] = &control_defaults[p];
	ng->cap = sample_rate;
	ng->dl[0] = calloc(ng->cap, sizeof(LADSPA_Data));
	if (ng->ch > 1)
		ng->dl[1] = calloc(ng->cap, sizeof(LADSPA_Data));
	if (!ng->dl[0] || (ng->ch > 1 && !ng->dl[1])) {
		free(ng->dl[0]);
		free(ng->dl[1]);
		free(ng);
		return NULL;
	}
	return ng;
}

static void cleanup(LADSPA_Handle instance)
{
	NoiseGate *ng = instance;
	for (unsigned long c = 0; c < ng->ch; c++)
		free(ng->dl[c]);
	free(ng);
}

static void activate(LADSPA_Handle instance)
{
	NoiseGate *ng = instance;
	ng->is_open = 0.0f;
	ng->attenuation = 0.0f;
	ng->level = 0.0f;
	ng->held_time = 0.0f;
}

static void deactivate(LADSPA_Handle instance)
{
	(void)instance;
}

static void connect_port(LADSPA_Handle instance, unsigned long port, LADSPA_Data *data)
{
	NoiseGate *ng = instance;
	if (port < 6) {
		ng->params[port] = data ? data : &control_defaults[port];
		return;
	}
	port -= 6;
	if (port < ng->ch)
		ng->in[port] = data;
	else
		ng->out[port - ng->ch] = data;
}

static void run(LADSPA_Handle instance, unsigned long count)
{
	NoiseGate *ng = instance;
	const LADSPA_Data open_threshold = db_to_mul(*ng->params[1]);
	const LADSPA_Data close_threshold = db_to_mul(*ng->params[0]);
	const LADSPA_Data sample_rate_i = 1.0f / ng->sample_rate;
	const LADSPA_Data attack_rate = 1.0f / ((*ng->params[2] / 1000.0f) * ng->sample_rate);
	const LADSPA_Data release_rate = 1.0f / ((*ng->params[4] / 1000.0f) * ng->sample_rate);
	const LADSPA_Data decay_rate =
		(open_threshold - close_threshold) / ((1.0f / 75.0f) * ng->sample_rate);
	const LADSPA_Data hold_time = *ng->params[3] / 1000.0f;
	unsigned long delay =
		(unsigned long)(*ng->params[5] * ng->sample_rate / 1000.0f + 0.5f);
	if (delay > ng->cap)
		delay = ng->cap;

	unsigned long idx = ng->idx;
	for (unsigned long i = 0; i < count; i++) {
		const unsigned long ri = idx >= delay ? idx - delay : idx + ng->cap - delay;

		LADSPA_Data cur_level = 0.0f;
		for (unsigned long c = 0; c < ng->ch; c++) {
			ng->dl[c][idx] = ng->in[c][i];
			const LADSPA_Data lv = fabsf(ng->in[c][i]);
			if (lv > cur_level)
				cur_level = lv;
		}

		if (cur_level > open_threshold && !ng->is_open)
			ng->is_open = 1.0f;
		if (ng->level < close_threshold && ng->is_open) {
			ng->held_time = 0.0f;
			ng->is_open = 0.0f;
		}

		ng->level = fmaxf(ng->level, cur_level) - decay_rate;

		if (ng->is_open) {
			ng->attenuation = fminf(1.0f, ng->attenuation + attack_rate);
		} else {
			ng->held_time += sample_rate_i;
			if (ng->held_time > hold_time)
				ng->attenuation = fmaxf(0.0f, ng->attenuation - release_rate);
		}

		for (unsigned long c = 0; c < ng->ch; c++)
			ng->out[c][i] = ng->dl[c][ri] * ng->attenuation;

		if (++idx == ng->cap)
			idx = 0;
	}
	ng->idx = idx;
}

static const LADSPA_PortDescriptor port_descriptors[] = {
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
};

static const char *port_names[] = {
	"Close Threshold (dB)",
	"Open Threshold (dB)",
	"Attack (ms)",
	"Hold (ms)",
	"Release (ms)",
	"Lookahead (ms)",
	"In_L",
	"In_R",
	"Out_L",
	"Out_R",
};

static const LADSPA_PortRangeHint port_range_hints[] = {
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_LOW, -96.0f, 0.0f },
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_HIGH, -96.0f, 0.0f },
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE, 0.0f, 1000.0f },
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE, 0.0f, 1000.0f },
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE, 0.0f, 1000.0f },
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MINIMUM, 0.0f, 1000.0f },
	{ 0, 0.0f, 0.0f },
	{ 0, 0.0f, 0.0f },
	{ 0, 0.0f, 0.0f },
	{ 0, 0.0f, 0.0f },
};

static const LADSPA_PortDescriptor mono_port_descriptors[] = {
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
};

static const char *mono_port_names[] = {
	"Close Threshold (dB)",
	"Open Threshold (dB)",
	"Attack (ms)",
	"Hold (ms)",
	"Release (ms)",
	"Lookahead (ms)",
	"In",
	"Out",
};



static const LADSPA_Descriptor descriptor = {
	1923,
	"sw_noise_gate",
	LADSPA_PROPERTY_HARD_RT_CAPABLE,
	"Noise Gate",
	"OBS Studio noise gate, ported to LADSPA",
	"GPL-2.0",
	10,
	port_descriptors,
	port_names,
	port_range_hints,
	NULL,
	instantiate,
	connect_port,
	activate,
	run,
	NULL,
	NULL,
	deactivate,
	cleanup,
};

static const LADSPA_Descriptor mono_descriptor = {
	1925,
	"sw_noise_gate_mono",
	LADSPA_PROPERTY_HARD_RT_CAPABLE,
	"Noise Gate (Mono)",
	"OBS Studio noise gate ported to LADSPA with the addition of Lookahead by somewhatfrog",
	"GPL-2.0",
	8,
	mono_port_descriptors,
	mono_port_names,
	port_range_hints,
	NULL,
	instantiate,
	connect_port,
	activate,
	run,
	NULL,
	NULL,
	deactivate,
	cleanup,
};

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index)
{
	switch (index) {
	case 0:
		return &descriptor;
	case 1:
		return &mono_descriptor;
	default:
		return NULL;
	}
}
