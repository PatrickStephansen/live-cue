#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif
#include <sndfile.h>

#include "lv2/lv2plug.in/ns/ext/log/log.h"
#include "lv2/lv2plug.in/ns/ext/log/logger.h"
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define LIVE_CUE_URI "http://github.com/PatrickStephansen/live-cue"
// -90db noise floor is a conservative estimate
#define DB_CO(g) ((g) > -90f ? powf(10f, (g)*0.05f) : 0f)
#define CO_DB(g) (g > 0.000031623f ? 20f * log10(g) : -90f)
#define SAMPLE_CHANNELS 2

typedef enum {
	OUTPUT_GAIN = 0,
	TRIGGER_THRESHOLD = 1,
	TRIGGER_INPUT = 2,
	LEFT_OUTPUT = 3,
	RIGHT_OUTPUT = 4
} PortIndex;

// read this from user-defined preset in future
static const char *playlist[2] = {"/home/patrick/Music/Mad God samples/Green Guardian SAMPLE.wav", "/home/patrick/Music/Mad God samples/The Cursed One and The First Flame SAMPLE.wav"};

typedef struct
{
	// Info about sample from sndfile
	SF_INFO info;
	// Sample data in float
	float *data;
	// Path of file
	char *path;
	// Length of path
	uint32_t path_len;
} Sample;

typedef struct
{
	LV2_Log_Log *log;
	LV2_Log_Logger logger;
	Sample *active_sample;
	Sample **all_samples;
	int sample_count;
	const float *output_gain;
	const float *threshold;
	float *input;
	float *left_output;
	float *right_output;
	// Current position in run()
	uint32_t frame_offset;

	// Playback state
	sf_count_t frame;
	bool play;
} LiveCue;

static Sample *
load_sample(LiveCue *self, const char *path)
{
	const size_t path_len = strlen(path);

	lv2_log_trace(&self->logger, "Loading sample %s\n", path);

	Sample *const sample = (Sample *)malloc(sizeof(Sample));
	SF_INFO *const info = &sample->info;
	SNDFILE *const sndfile = sf_open(path, SFM_READ, info);

	if (!sndfile || !info->frames || info->channels != SAMPLE_CHANNELS)
	{
		lv2_log_error(&self->logger, "Failed to open sample '%s'\n", path);
		free(sample);
		return NULL;
	}

	// Read data
	float *const data = calloc(SAMPLE_CHANNELS * info->frames, sizeof(float));

	if (!data)
	{
		lv2_log_error(&self->logger, "Failed to allocate memory for sample\n");
		return NULL;
	}
	sf_seek(sndfile, 0ul, SEEK_SET);

	sf_read_float(sndfile, data, info->frames * SAMPLE_CHANNELS);
	sf_close(sndfile);

	// Fill sample struct and return it
	sample->data = data;
	sample->path = (char *)malloc(path_len + 1);
	sample->path_len = (uint32_t)path_len;
	memcpy(sample->path, path, path_len + 1);
	lv2_log_trace(&self->logger, "sample loaded\n");

	return sample;
}

static void
free_sample(LiveCue *self, Sample *sample)
{
	if (sample)
	{
		lv2_log_trace(&self->logger, "Freeing %s\n", sample->path);
		free(sample->path);
		free(sample->data);
		free(sample);
	}
}

static LV2_Handle
instantiate(
	const LV2_Descriptor *descriptor,
	double rate,
	const char *bundle_path,
	const LV2_Feature *const *features)
{
	LiveCue *self = (LiveCue *)malloc(sizeof(LiveCue));

	// Get host features
	for (int i = 0; features[i]; ++i)
	{
		if (!strcmp(features[i]->URI, LV2_LOG__log))
		{
			self->log = (LV2_Log_Log *)features[i]->data;
		}
	}
	lv2_log_logger_init(&self->logger, NULL, self->log);

	int playlist_length = sizeof(playlist) / sizeof(playlist[0]);
	lv2_log_trace(&self->logger, "tracks in playlist: %d\n", playlist_length);
	self->all_samples = (Sample **)malloc(sizeof(Sample *) * playlist_length);
	for (int i = 0; i < playlist_length; i++)
	{
		self->all_samples[i] = load_sample(self, playlist[i]);
	}
	self->active_sample = self->all_samples[0];

	self->play = false;
	self->frame = 0;

	return (LV2_Handle)self;

fail:
	free(self);
	return 0;
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
	LiveCue *self = (LiveCue *)instance;

	const float *const input = self->input;
	sf_count_t start_frame = 0;
	sf_count_t pos = 0;
	float *const left_output = self->left_output;
	float *const right_output = self->right_output;

	for (pos = 0; pos < n_samples; ++pos)
	{
		float inputGain = CO_DB(abs(input[pos]));
		if (inputGain > *self->threshold)
		{
			lv2_log_trace(&self->logger, "hit detected: %.6f db\n", inputGain);
			lv2_log_trace(&self->logger, "threshold: %.6f db\n", *self->threshold);
			start_frame = pos;
			self->play = !self->play;
			pos = 0;
			self->frame = 0;
			break;
		}
	}

	// Render the sample (possibly already in progress)
	if (self->play)
	{
		uint32_t f = self->frame;
		const uint32_t lf = self->active_sample->info.frames;

		for (pos = 0; pos < start_frame; ++pos)
		{
			left_output[pos] = 0;
			right_output[pos] = 0;
		}

		for (; pos < n_samples && f < lf; ++pos, ++f)
		{
			float coef = DB_CO(*self->output_gain);
			left_output[pos] = self->active_sample->data[f * SAMPLE_CHANNELS] * coef;
			right_output[pos] = self->active_sample->data[f * SAMPLE_CHANNELS + 1] * coef;
		}

		self->frame = f;

		if (f == lf)
		{
			self->play = false;
		}
	}

	// Add zeros to end if sample not long enough (or not playing)
	for (; pos < n_samples; ++pos)
	{
		left_output[pos] = 0;
		right_output[pos] = 0;
	}
}

static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
	LiveCue *self = (LiveCue *)instance;
	free_sample(self, self->active_sample);
	free(self);
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