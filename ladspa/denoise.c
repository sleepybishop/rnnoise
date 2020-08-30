#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mmapring.h>
#include <rnnoise.h>

#include "ladspa.h"

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

/* The port numbers for the plugin */
typedef enum {
  DENOISER_CONTROL0 = 0,
  DENOISER_INPUT0,
  DENOISER_OUTPUT0,
  DENOISER_INPUT1,
  DENOISER_OUTPUT1,
  DENOISER_PORT_COUNT
} port_id;

typedef struct {
  DenoiseState *st;
  mmapring_t *input_ring[2];
  mmapring_t *output_ring[2];
  LADSPA_Data *vad_threshold;
  LADSPA_Data *input_buffer[2];
  LADSPA_Data *output_buffer[2];
} Denoiser;

/* Construct a new plugin instance. */
static LADSPA_Handle instantiateDenoiser(const LADSPA_Descriptor *Descriptor,
                                         unsigned long SampleRate) {
  return calloc(1, sizeof(Denoiser));
}

static void activateDenoiser(LADSPA_Handle Instance) {
  Denoiser *ctx = (Denoiser *)Instance;
  ctx->st = rnnoise_create(NULL);
  ctx->input_ring[0] = mmapring_new(65536);
  ctx->output_ring[0] = mmapring_new(65536);
  ctx->input_ring[1] = mmapring_new(65536);
  ctx->output_ring[1] = mmapring_new(65536);
}

static void deactivateDenoiser(LADSPA_Handle Instance) {
  Denoiser *ctx = (Denoiser *)Instance;
  rnnoise_destroy(ctx->st);
  mmapring_free(ctx->input_ring[0]);
  mmapring_free(ctx->output_ring[0]);
  mmapring_free(ctx->input_ring[1]);
  mmapring_free(ctx->output_ring[1]);
}

/* Connect a port to a data location. */
static void connectPortToDenoiser(LADSPA_Handle Instance, unsigned long Port,
                                  LADSPA_Data *DataLocation) {

  Denoiser *ctx = (Denoiser *)Instance;
  switch (Port) {
  case DENOISER_CONTROL0:
    ctx->vad_threshold = DataLocation;
    break;
  case DENOISER_INPUT0:
    ctx->input_buffer[0] = DataLocation;
    break;
  case DENOISER_OUTPUT0:
    ctx->output_buffer[0] = DataLocation;
    break;
  case DENOISER_INPUT1:
    ctx->input_buffer[1] = DataLocation;
    break;
  case DENOISER_OUTPUT1:
    ctx->output_buffer[1] = DataLocation;
    break;
  }
}

static void processChannel(Denoiser *ctx, unsigned chan,
                           unsigned long SampleCount) {
  int frame_bytes = sizeof(LADSPA_Data) * 480;
  int grace_frames = 0;
  /*
   * implement grace period
   */
  for (int i = 0; i < SampleCount; i++) {
    ctx->input_buffer[chan][i] *= INT16_MAX;
  }
  int offered = mmapring_offer(ctx->input_ring[chan],
                               (unsigned char *)ctx->input_buffer[chan],
                               SampleCount * sizeof(LADSPA_Data));
  while (mmapring_used(ctx->input_ring[chan]) >= frame_bytes) {
    LADSPA_Data *tmp =
        (LADSPA_Data *)mmapring_poll(ctx->input_ring[chan], frame_bytes);
    assert(tmp != NULL);
    float vad_prob = rnnoise_process_frame(ctx->st, tmp, tmp);
    if (((100 * vad_prob) < *ctx->vad_threshold) && grace_frames == 0) {
      memset(tmp, 0, frame_bytes);
      grace_frames--;
    } else {
      grace_frames = 1;
    }
    mmapring_offer(ctx->output_ring[chan], (unsigned char *)tmp, frame_bytes);
  }
  int out_samples = MIN(SampleCount, mmapring_used(ctx->output_ring[chan]) /
                                         sizeof(LADSPA_Data));
  LADSPA_Data *tmp = (LADSPA_Data *)mmapring_poll(
      ctx->output_ring[chan], out_samples * sizeof(LADSPA_Data));
  assert(tmp != NULL);
  for (int i = 0; i < SampleCount - out_samples; i++) {
    ctx->output_buffer[chan][i] = 0;
  }
  for (int i = (SampleCount - out_samples), j = 0; i < SampleCount; i++, j++) {
    ctx->output_buffer[chan][i] = tmp[j] / INT16_MAX;
  }
}

static void runMonoDenoiser(LADSPA_Handle Instance, unsigned long SampleCount) {
  Denoiser *ctx = (Denoiser *)Instance;
  processChannel(ctx, 0, SampleCount);
}

static void runStereoDenoiser(LADSPA_Handle Instance,
                              unsigned long SampleCount) {
  Denoiser *ctx = (Denoiser *)Instance;
  processChannel(ctx, 0, SampleCount);
  processChannel(ctx, 1, SampleCount);
}

static void cleanupDenoiser(LADSPA_Handle Instance) { free(Instance); }

LADSPA_Descriptor *g_psMonoDescriptor = NULL;
LADSPA_Descriptor *g_psStereoDescriptor = NULL;

/* Called automatically when the plugin library is first loaded. */
static void __attribute__((constructor)) init() {

  char **pcPortNames;
  LADSPA_PortDescriptor *piPortDescriptors;
  LADSPA_PortRangeHint *psPortRangeHints;

  g_psMonoDescriptor =
      (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));
  g_psStereoDescriptor =
      (LADSPA_Descriptor *)calloc(1, sizeof(LADSPA_Descriptor));

  if (g_psMonoDescriptor) {

    g_psMonoDescriptor->UniqueID = 45671;
    g_psMonoDescriptor->Label = strdup("denoiser_mono");
    g_psMonoDescriptor->Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
    g_psMonoDescriptor->Name = strdup("Denoiser (mono)");
    g_psMonoDescriptor->Maker = strdup("Xiph");
    g_psMonoDescriptor->Copyright = strdup("BSD 3-Clause");
    g_psMonoDescriptor->PortCount = 3;
    piPortDescriptors =
        (LADSPA_PortDescriptor *)calloc(3, sizeof(LADSPA_PortDescriptor));
    g_psMonoDescriptor->PortDescriptors =
        (const LADSPA_PortDescriptor *)piPortDescriptors;
    piPortDescriptors[DENOISER_CONTROL0] =
        LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
    piPortDescriptors[DENOISER_INPUT0] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
    piPortDescriptors[DENOISER_OUTPUT0] =
        LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
    pcPortNames = (char **)calloc(3, sizeof(char *));
    g_psMonoDescriptor->PortNames = (const char **)pcPortNames;
    pcPortNames[DENOISER_CONTROL0] = strdup("VAD Threshold");
    pcPortNames[DENOISER_INPUT0] = strdup("Input");
    pcPortNames[DENOISER_OUTPUT0] = strdup("Output");
    psPortRangeHints =
        ((LADSPA_PortRangeHint *)calloc(3, sizeof(LADSPA_PortRangeHint)));
    g_psMonoDescriptor->PortRangeHints =
        (const LADSPA_PortRangeHint *)psPortRangeHints;
    psPortRangeHints[DENOISER_CONTROL0].HintDescriptor =
        (LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
         LADSPA_HINT_INTEGER | LADSPA_HINT_DEFAULT_MIDDLE);
    psPortRangeHints[DENOISER_CONTROL0].LowerBound = 0;
    psPortRangeHints[DENOISER_CONTROL0].UpperBound = 99;
    psPortRangeHints[DENOISER_INPUT0].HintDescriptor = 0;
    psPortRangeHints[DENOISER_OUTPUT0].HintDescriptor = 0;
    g_psMonoDescriptor->instantiate = instantiateDenoiser;
    g_psMonoDescriptor->connect_port = connectPortToDenoiser;
    g_psMonoDescriptor->activate = activateDenoiser;
    g_psMonoDescriptor->run = runMonoDenoiser;
    g_psMonoDescriptor->run_adding = NULL;
    g_psMonoDescriptor->set_run_adding_gain = NULL;
    g_psMonoDescriptor->deactivate = deactivateDenoiser;
    g_psMonoDescriptor->cleanup = cleanupDenoiser;
  }

  if (g_psStereoDescriptor) {

    g_psStereoDescriptor->UniqueID = 45672;
    g_psStereoDescriptor->Label = strdup("denoiser_stereo");
    g_psStereoDescriptor->Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
    g_psStereoDescriptor->Name = strdup("Denoiser (stereo)");
    g_psStereoDescriptor->Maker = strdup("Xiph");
    g_psStereoDescriptor->Copyright = strdup("BSD 3-Clause");
    g_psStereoDescriptor->PortCount = 5;
    piPortDescriptors =
        (LADSPA_PortDescriptor *)calloc(5, sizeof(LADSPA_PortDescriptor));
    g_psStereoDescriptor->PortDescriptors =
        (const LADSPA_PortDescriptor *)piPortDescriptors;
    piPortDescriptors[DENOISER_CONTROL0] =
        LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
    piPortDescriptors[DENOISER_INPUT0] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
    piPortDescriptors[DENOISER_OUTPUT0] =
        LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
    piPortDescriptors[DENOISER_INPUT1] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
    piPortDescriptors[DENOISER_OUTPUT1] =
        LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
    pcPortNames = (char **)calloc(5, sizeof(char *));
    g_psStereoDescriptor->PortNames = (const char **)pcPortNames;
    pcPortNames[DENOISER_CONTROL0] = strdup("VAD Threshold");
    pcPortNames[DENOISER_INPUT0] = strdup("Input (Left)");
    pcPortNames[DENOISER_OUTPUT0] = strdup("Output (Left)");
    pcPortNames[DENOISER_INPUT1] = strdup("Input (Right)");
    pcPortNames[DENOISER_OUTPUT1] = strdup("Output (Right)");
    psPortRangeHints =
        ((LADSPA_PortRangeHint *)calloc(5, sizeof(LADSPA_PortRangeHint)));
    g_psStereoDescriptor->PortRangeHints =
        (const LADSPA_PortRangeHint *)psPortRangeHints;
    psPortRangeHints[DENOISER_CONTROL0].HintDescriptor =
        (LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
         LADSPA_HINT_INTEGER | LADSPA_HINT_DEFAULT_MIDDLE);
    psPortRangeHints[DENOISER_CONTROL0].LowerBound = 0;
    psPortRangeHints[DENOISER_CONTROL0].UpperBound = 99;
    psPortRangeHints[DENOISER_INPUT0].HintDescriptor = 0;
    psPortRangeHints[DENOISER_OUTPUT0].HintDescriptor = 0;
    psPortRangeHints[DENOISER_INPUT1].HintDescriptor = 0;
    psPortRangeHints[DENOISER_OUTPUT1].HintDescriptor = 0;
    g_psStereoDescriptor->instantiate = instantiateDenoiser;
    g_psStereoDescriptor->connect_port = connectPortToDenoiser;
    g_psStereoDescriptor->activate = activateDenoiser;
    g_psStereoDescriptor->run = runStereoDenoiser;
    g_psStereoDescriptor->run_adding = NULL;
    g_psStereoDescriptor->set_run_adding_gain = NULL;
    g_psStereoDescriptor->deactivate = deactivateDenoiser;
    g_psStereoDescriptor->cleanup = cleanupDenoiser;
  }
}

static void deleteDescriptor(LADSPA_Descriptor *psDescriptor) {
  if (psDescriptor) {
    free((char *)psDescriptor->Label);
    free((char *)psDescriptor->Name);
    free((char *)psDescriptor->Maker);
    free((char *)psDescriptor->Copyright);
    free((LADSPA_PortDescriptor *)psDescriptor->PortDescriptors);
    for (int idx = 0; idx < psDescriptor->PortCount; idx++)
      free((char *)(psDescriptor->PortNames[idx]));
    free((char **)psDescriptor->PortNames);
    free((LADSPA_PortRangeHint *)psDescriptor->PortRangeHints);
    free(psDescriptor);
  }
}

/* Called automatically when the library is unloaded. */
static void __attribute__((destructor)) fini() {
  deleteDescriptor(g_psMonoDescriptor);
  deleteDescriptor(g_psStereoDescriptor);
}

/* Return a descriptor of the requested plugin type. There are two
   plugin types available in this library (mono and stereo). */
const LADSPA_Descriptor *ladspa_descriptor(unsigned long Index) {
  /* Return the requested descriptor or null if the index is out of
     range. */
  switch (Index) {
  case 0:
    return g_psMonoDescriptor;
  case 1:
    return g_psStereoDescriptor;
  default:
    return NULL;
  }
}
