#ifndef _APRIL_API
#define _APRIL_API

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AprilASRModel_i;
struct AprilASRSession_i;

typedef struct AprilASRModel_i * AprilASRModel;
typedef struct AprilASRSession_i * AprilASRSession;

// Must be called once before calling any other functions
void aam_api_init(void);

// Creates a model given a path. May return NULL if loading failed.
AprilASRModel aam_create_model(const char *model_path);

// Get the name/desc/lang of the model. The pointers are valid for the
// lifetime of the model (i.e. until aam_free is called on the model)
const char *aam_get_name(AprilASRModel model);
const char *aam_get_description(AprilASRModel model);
const char *aam_get_language(AprilASRModel model);

// Get the sample rate of model in Hz. For example, may return 16000
size_t aam_get_sample_rate(AprilASRModel model);

// Caller must ensure all sessions backed by model are freed before model
// is freed
void aam_free(AprilASRModel model);



// Unique identifier for a speaker. For example, it may be a hash of the
// speaker's name. This may be provided to `aas_create_session` for saving
// and restoring state.
typedef struct AprilSpeakerID {
    uint8_t data[16];
} AprilSpeakerID;

typedef enum AprilResultType {
    APRIL_RESULT_UNKNOWN = 0,

    // Specifies that the result is only partial, and a future call will
    // contain much of the same text but updated.
    APRIL_RESULT_RECOGNITION_PARTIAL,

    // Specifies that the result is final. Future calls will start from
    // empty and will not contain any of the given text.
    APRIL_RESULT_RECOGNITION_FINAL
} AprilResultType;

typedef struct AprilToken {
    // Null-terminated string. The string contains its own formatting,
    // for example it may start with a space to denote a new word, or
    // not start with a space to denote the next part of a word.
    const char *token;

    // Log probability of this being the correct token
    float logprob;
} AprilToken;

// Pointers are guaranteed to be valid only for the duration of the call.
// (void* userdata, AprilResultType result, size_t count, const AprilToken *tokens);
// count may be 0, and if so then tokens may be NULL.
typedef void(*AprilRecognitionResultHandler)(void*, AprilResultType, size_t, const AprilToken*);

typedef struct AprilConfig {
    // If all 0, will be ignored
    AprilSpeakerID speaker;

    // The handler that will be called as events occur.
    // This may be called from a different thread.
    AprilRecognitionResultHandler handler;
    void *userdata;

    // If set to true, will attempt to process audio in realtime and avoid
    // lagging behind. This may result in reduced accuracy.
    bool realtime; // TODO: stdbool.h requirement
} AprilConfig;

// Creates a session with a given model. A model may have many sessions
// associated with it.
AprilASRSession aas_create_session(AprilASRModel model, AprilConfig config);

// Feed PCM16 audio data to the session, must be single-channel and sampled
// to the sample rate given in `aam_get_sample_rate`.
// Note `short_count` is the number of shorts, not bytes!
void aas_feed_pcm16(AprilASRSession session, short *pcm16, size_t short_count);

// Processes any unprocessed samples and produces a final result.
void aas_flush(AprilASRSession session);

// Frees the session, this must be called for all sessions before freeing
// the model. Saves state to a file if AprilSpeakerID was supplied.
void aas_free(AprilASRSession session);

#ifdef __cplusplus
}
#endif

#endif
