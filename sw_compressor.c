#include <ladspa.h>
#include <math.h>
#include <stdlib.h>

#define BUNDLE_ID 1924

static LADSPA_Data control_defaults[6] = { 10.0f, -18.0f, 6.0f, 60.0f, 0.0f, 6.0f };

static inline float db_to_mul(const LADSPA_Data db)
{
	return powf(10.0f, db / 20.0f);
}

static inline float mul_to_db(const LADSPA_Data mul)
{
	return 20.0f * log10f(mul);
}

static inline float gain_coefficient(const LADSPA_Data sample_rate, const LADSPA_Data time_sec)
{
	return expf(-1.0f / (sample_rate * time_sec));
}

typedef struct {
	LADSPA_Data sample_rate;

	LADSPA_Data *params[6];

	LADSPA_Data envelope[2];

	unsigned long ch;
	unsigned long idx;
	unsigned long cap;
	LADSPA_Data *dl[2];
	LADSPA_Data *in[2];
	LADSPA_Data *out[2];
} Compressor;

static LADSPA_Handle instantiate(const LADSPA_Descriptor *descriptor, unsigned long sample_rate)
{
	Compressor *cd = calloc(1, sizeof(Compressor));
	if (!cd)
		return NULL;
	cd->sample_rate = (LADSPA_Data)sample_rate;
	cd->ch = descriptor->PortCount == 8 ? 1 : 2;
	for (unsigned long p = 0; p < 6; p++)
		cd->params[p] = &control_defaults[p];
	cd->cap = sample_rate;
	cd->dl[0] = calloc(cd->cap, sizeof(LADSPA_Data));
	if (cd->ch > 1)
		cd->dl[1] = calloc(cd->cap, sizeof(LADSPA_Data));
	if (!cd->dl[0] || (cd->ch > 1 && !cd->dl[1])) {
		free(cd->dl[0]);
		free(cd->dl[1]);
		free(cd);
		return NULL;
	}
	return cd;
}

static void cleanup(LADSPA_Handle instance)
{
	Compressor *cd = instance;
	for (unsigned long c = 0; c < cd->ch; c++)
		free(cd->dl[c]);
	free(cd);
}

static void deactivate(LADSPA_Handle instance)
{
	(void)instance;
}

static void activate(LADSPA_Handle instance)
{
	Compressor *cd = instance;
	cd->envelope[0] = 0.0f;
	cd->envelope[1] = 0.0f;
}

static void connect_port(LADSPA_Handle instance, unsigned long port, LADSPA_Data *data)
{
	Compressor *cd = instance;
	if (port < 6) {
		cd->params[port] = data ? data : &control_defaults[port];
		return;
	}
	port -= 6;
	if (port < cd->ch)
		cd->in[port] = data;
	else
		cd->out[port - cd->ch] = data;
}

static void run(LADSPA_Handle instance, unsigned long count)
{
	Compressor *cd = instance;
	const unsigned long ch = cd->ch;
	const LADSPA_Data attack_gain = gain_coefficient(cd->sample_rate, *cd->params[2] / 1000.0f);
	const LADSPA_Data release_gain = gain_coefficient(cd->sample_rate, *cd->params[3] / 1000.0f);
	const LADSPA_Data output_gain = db_to_mul(*cd->params[4]);
	const LADSPA_Data slope = 1.0f - (1.0f / *cd->params[0]);
	const LADSPA_Data threshold = *cd->params[1];
	unsigned long delay =
		(unsigned long)(*cd->params[5] * cd->sample_rate / 1000.0f + 0.5f);
	if (delay > cd->cap)
		delay = cd->cap;

	unsigned long idx = cd->idx;
	for (unsigned long i = 0; i < count; i++) {
		const unsigned long ri = idx >= delay ? idx - delay : idx + cd->cap - delay;

		for (unsigned long c = 0; c < ch; c++)
			cd->dl[c][idx] = cd->in[c][i];

		LADSPA_Data env = 0.0f;
		for (unsigned long c = 0; c < ch; c++) {
			const LADSPA_Data ch_in = fabsf(cd->in[c][i]);
			if (cd->envelope[c] < ch_in)
				cd->envelope[c] = ch_in + attack_gain * (cd->envelope[c] - ch_in);
			else
				cd->envelope[c] = ch_in + release_gain * (cd->envelope[c] - ch_in);
			if (cd->envelope[c] > env)
				env = cd->envelope[c];
		}

		LADSPA_Data gain = 1.0f;
		if (env > 0.0f) {
			const LADSPA_Data cand = slope * (threshold - mul_to_db(env));
			if (cand < 0.0f)
				gain = db_to_mul(cand);
		}

		const LADSPA_Data g = gain * output_gain;
		for (unsigned long c = 0; c < ch; c++)
			cd->out[c][i] = cd->dl[c][ri] * g;

		if (++idx == cd->cap)
			idx = 0;
	}
	cd->idx = idx;
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
	"Ratio (:1)",
	"Threshold (dB)",
	"Attack (ms)",
	"Release (ms)",
	"Output Gain (dB)",
	"Lookahead (ms)",
	"In_L",
	"In_R",
	"Out_L",
	"Out_R",
};

static const LADSPA_PortRangeHint port_range_hints[] = {
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MIDDLE, 1.0f, 32.0f },
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_LOW, -60.0f, 0.0f },
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MINIMUM, 1.0f, 500.0f },
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE, 1.0f, 1000.0f },
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE, -32.0f, 32.0f },
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
	"Ratio (:1)",
	"Threshold (dB)",
	"Attack (ms)",
	"Release (ms)",
	"Output Gain (dB)",
	"Lookahead (ms)",
	"In",
	"Out",
};



static const LADSPA_Descriptor descriptor = {
	1924,
	"sw_compressor",
	LADSPA_PROPERTY_HARD_RT_CAPABLE,
	"Compressor",
	"OBS Studio compressor, ported to LADSPA",
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
	1926,
	"sw_compressor_mono",
	LADSPA_PROPERTY_HARD_RT_CAPABLE,
	"Compressor (Mono)",
	"OBS Studio compressor ported to LADSPA with the addition of Lookahead by somewhatfrog",
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
