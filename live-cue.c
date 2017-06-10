#include <math.h>
#include <stdlib.h>
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define LIVE_CUE_URI "http://github.com/PatrickStephansen/live-cue"
#define DB_CO(g) ((g) > -90.0f ? powf(10.0f, (g)*0.05f) : 0.0f)

typedef enum {
	OUTPUT_GAIN = 0,
	TRIGGER_THRESHOLD = 1,
	TRIGGER_INPUT = 2,
	LEFT_OUTPUT = 3,
	RIGHT_OUTPUT = 4
} PortIndex;

typedef struct
{
	const float *output_gain;
	const float *threshold;
	float *input;
	float *left_output;
	float *right_output;
} LiveCue;

static LV2_Handle
instantiate(
	const LV2_Descriptor *descriptor,
	double rate,
	const char *bundle_path,
	const LV2_Feature *const *features)
{
	LiveCue *self = (LiveCue *)calloc(1, sizeof(LiveCue));

	return (LV2_Handle)self;
}

static void
connect_port(
	LV2_Handle instance,
	uint32_t port,
	void *data)
{
	LiveCue *self = (LiveCue *)instance;

	switch ((PortIndex)port)
	{
	case OUTPUT_GAIN:
		self->output_gain = (const float *)data;
		break;
	case TRIGGER_THRESHOLD:
		self->threshold = (const float *)data;
		break;
	case TRIGGER_INPUT:
		self->input = (float *)data;
		break;
	case LEFT_OUTPUT:
		self->left_output = (float *)data;
		break;
	case RIGHT_OUTPUT:
		self->right_output = (float *)data;
		break;
	}
}

static void
activate(LV2_Handle instance)
{
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
	const LiveCue *self = (const LiveCue *)instance;

	const float gain = *(self->output_gain);
	const float *const input = self->input;
	float *const left_output = self->left_output;
	float *const right_output = self->right_output;

	const float coef = DB_CO(gain);

	for (uint32_t pos = 0; pos < n_samples; pos++)
	{
		left_output[pos] = input[pos] * coef;
		right_output[pos] = input[pos] * coef;
	}
}

static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
	free(instance);
}

static const void *
extension_data(const char *uri)
{
	return NULL;
}

static const LV2_Descriptor descriptor = {
	LIVE_CUE_URI,
	instantiate,
	connect_port,
	activate,
	run,
	deactivate,
	cleanup,
	extension_data};

LV2_SYMBOL_EXPORT
const LV2_Descriptor *
lv2_descriptor(uint32_t index)
{
	switch (index)
	{
	case 0:
		return &descriptor;
	default:
		return NULL;
	}
}