#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include "fbank.h"
#include "pocketfft.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

int round_up_to_nearest_power_of_two(int n) {
    n -= 1;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

void generate_povey_window(float *out, int N) {
    double N_f = (double)N;
    for (int i=0; i<N; i++){
        double n = (double)i;
        out[i] = (float)pow((0.5 - 0.5 * cos(n / N * 6.283185307)), 0.85);
    }
}

double inverse_mel_scale(double mel_freq) {
    return 700.0 * (exp(mel_freq / 1127.0) - 1.0);
}

double mel_scale(double freq) {
    return 1127.0 * log(1.0 + freq / 700.0);
}

void generate_banks(float *bins_mat, int num_bins, int num_fft_bins, int padded_window_size, int sample_freq, int mel_low_freq, int mel_high_freq){
    if(mel_high_freq == 0) mel_high_freq = sample_freq / 2;

    float fft_bin_width = (float)sample_freq / (float)padded_window_size;

    float mel_low = (float)mel_scale((double)mel_low_freq);
    float mel_high = (float)mel_scale((double)mel_high_freq);

    float mel_freq_delta = (mel_high - mel_low) / ((float)num_bins + 1.0f);

    for(int i=0; i<num_bins; i++){
        float left_mel = mel_low + (float)i * mel_freq_delta;
        float center_mel = left_mel + mel_freq_delta;
        float right_mel = center_mel + mel_freq_delta;

        for(int j=0; j<num_fft_bins; j++){
            float freq = fft_bin_width * (float)j;
            float mel = (float)mel_scale((double)freq);
            
            //printf("freq %.2f, mel %.2f,  %.2f %.2f %.2f\n", freq, mel, left_mel, center_mel, right_mel);

            float weight = 0.0f;
            if((mel > left_mel) && (mel < right_mel)) {
                if(mel <= center_mel){
                    weight = (mel - left_mel) / (center_mel - left_mel);
                } else {
                    weight = (right_mel - mel) / (right_mel - center_mel);
                }
            }
            bins_mat[i * num_fft_bins + j] = weight;
        }
    }
}


struct OnlineFBank_i {
    FBankOptions opts;

    int window_shift;
    int window_size;
    int padded_window_size;
    int num_fft_bins;

    float *window;
    float *mel_bins;

    float *temp_segments;
    size_t temp_segments_y;
    size_t temp_segments_count;
    size_t temp_segment_head; // y, write
    size_t temp_segment_tail; // y, read
    size_t temp_segment_avail;


    float *prev_leftover;
    size_t prev_leftover_count;
};

OnlineFBank make_fbank(FBankOptions opts) {
    assert(opts.snip_edges); // not sure how to implement non-snip-edges at this time

    OnlineFBank fbank = (OnlineFBank)calloc(1, sizeof(struct OnlineFBank_i));
    fbank->opts = opts;

    fbank->window_shift = opts.frame_shift_ms * opts.sample_freq / 1000;
    fbank->window_size = opts.frame_length_ms * opts.sample_freq / 1000;
    fbank->padded_window_size = opts.round_pow2 ? round_up_to_nearest_power_of_two(fbank->window_size) : fbank->window_size;
    fbank->num_fft_bins = fbank->padded_window_size / 2;

    fbank->window = (float*)calloc(fbank->padded_window_size, sizeof(float));
    generate_povey_window(fbank->window, fbank->padded_window_size);

    fbank->mel_bins = (float*)calloc(fbank->num_fft_bins * opts.num_bins, sizeof(float));
    memset(fbank->mel_bins, 0, fbank->num_fft_bins * opts.num_bins * sizeof(float));
    generate_banks(fbank->mel_bins, opts.num_bins, fbank->num_fft_bins,
        fbank->padded_window_size, opts.sample_freq, opts.mel_low, opts.mel_high);
    
    fbank->temp_segments_y = opts.pull_segment_count * 32;
    fbank->temp_segments_count = fbank->temp_segments_y * fbank->num_fft_bins;
    fbank->temp_segments = (float*)calloc(fbank->temp_segments_count, sizeof(float));

    fbank->temp_segment_tail = 0;
    fbank->temp_segment_head = 0;
    fbank->temp_segment_avail = 0;


    fbank->prev_leftover = (float*)calloc(fbank->padded_window_size * 2, sizeof(float));
    fbank->prev_leftover_count = 0;
}

void fbank_accept_waveform(OnlineFBank fbank, float *wave, size_t wave_count) {
    double data[fbank->padded_window_size];
    double ret[fbank->padded_window_size + 1];
    rfft_plan plan = make_rfft_plan(fbank->padded_window_size);

    //for(ssize_t i=0; i<(wave_count/fbank->window_shift); i++) {
    for(ssize_t i=0;; i++) {
        if((fbank->temp_segment_avail + 1) > fbank->temp_segments_y){
            printf("%d Out of space! Please call fbank_pull_segments. Can't eat wave\n", i);
            return;
        }

        //printf("leftover %d\n", fbank->prev_leftover_count);
        ssize_t start_idx = i * fbank->window_shift - fbank->prev_leftover_count;
        ssize_t end_idx = start_idx + fbank->padded_window_size;

        if(end_idx > wave_count){
            //printf("End idx %llu %llu %llu\n", end_idx, i, (wave_count / fbank->window_shift));
            memcpy(fbank->prev_leftover, &wave[start_idx], (wave_count - start_idx) * sizeof(float));
            fbank->prev_leftover_count = wave_count - start_idx;
            return;
        }

        for(int j=0; j<fbank->padded_window_size; j++){
            ssize_t wave_idx = start_idx + j;
            if(wave_idx < 0){
                ssize_t ll_idx = fbank->prev_leftover_count + wave_idx;
                data[j] = fbank->prev_leftover[ll_idx] * fbank->window[j];
            } else {
                data[j] = wave[start_idx + j] * fbank->window[j];
            }
        }
        
        double *dptr = data;
        double *rptr = ret;
        memcpy((char *)(rptr+1), dptr, fbank->padded_window_size * sizeof(double));

        int res = rfft_forward(plan, rptr+1, 1.0);
        if(res != 0){
            printf("Fail %d", res);
            break;
        }

        rptr[0] = ret[1];
        rptr[1] = 0.0;

        float *out = &fbank->temp_segments[fbank->temp_segment_head * fbank->opts.num_bins];
        memset(out, 0, fbank->opts.num_bins * sizeof(float));
        
        for(int mel=0; mel<fbank->opts.num_bins; mel++){
            for(int fft=0; fft<fbank->num_fft_bins; fft++){
                float real = (float)(rptr[fft * 2]);
                float imaginary = (float)(rptr[fft * 2 + 1]);

                out[mel] += (real * real + imaginary * imaginary) * fbank->mel_bins[mel * fbank->num_fft_bins + fft];
            }
        }

        for(int mel=0; mel<fbank->opts.num_bins; mel++){
            const float kEps = 1.1920928955078125e-07f;
            out[mel] = (float)log((double)MAX(kEps, out[mel]));
        }
        
        fbank->temp_segment_head++;
        fbank->temp_segment_avail++;

        fbank->temp_segment_head = fbank->temp_segment_head % fbank->temp_segments_y;
    }

    fbank->prev_leftover_count = 0;
}

bool fbank_pull_segments(OnlineFBank fbank, float *output, size_t output_count) {
    assert(output_count == fbank->opts.pull_segment_count * fbank->opts.num_bins * sizeof(float));

    if(fbank->temp_segment_avail < fbank->opts.pull_segment_count) {
        return false;
    }

    for(int i=0; i<fbank->opts.pull_segment_count; i++){
        int curr_idx = (fbank->temp_segment_tail + i) % fbank->temp_segments_y;
        memcpy(
            &output[i * fbank->opts.num_bins],
            &fbank->temp_segments[curr_idx * fbank->opts.num_bins],
            fbank->opts.num_bins * sizeof(float)
        );
    }

    fbank->temp_segment_tail += fbank->opts.pull_segment_step;
    fbank->temp_segment_tail = fbank->temp_segment_tail % fbank->temp_segments_y;
    fbank->temp_segment_avail -= fbank->opts.pull_segment_step;

    return true;
}

void free_fbank(OnlineFBank fbank) {
    // TODO
}