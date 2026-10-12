/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Scores every pair in samples/ and reports how well the two populations
 * separate: pairs from the same finger against pairs from different ones.
 *
 * Reads files only; the sensor is not involved. */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "goodix/match.h"

#define SAMPLE_DIR  "samples"
#define MAX_SAMPLES 128
#define BUCKETS     20

typedef struct {
    char            label[64];
    goodix_template tmpl;
} sample;

static sample  samples[MAX_SAMPLES];
static int     sample_count;

static int load_raw(const char *path, uint8_t *raw)
{
    FILE  *file = fopen(path, "rb");
    size_t len;

    if (file == NULL)
        return -1;

    len = fread(raw, 1, GOODIX_IMAGE_RAW_SIZE, file);
    fclose(file);

    return len == GOODIX_IMAGE_RAW_SIZE ? 0 : -1;
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

static int collect(void)
{
    static char  names[MAX_SAMPLES][256];
    static char *order[MAX_SAMPLES];

    DIR           *dir = opendir(SAMPLE_DIR);
    struct dirent *entry;
    int            found = 0;

    if (dir == NULL) {
        fprintf(stderr, "no %s/ directory; run build/sample first\n",
                SAMPLE_DIR);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL && found < MAX_SAMPLES) {
        size_t len = strlen(entry->d_name);

        /* Every capture is a pair; the finger frame names the sample. */
        if (len > 7 && strcmp(entry->d_name + len - 7, "-fp.bin") == 0) {
            snprintf(names[found], sizeof(names[found]), "%s", entry->d_name);
            order[found] = names[found];
            found++;
        }
    }
    closedir(dir);

    qsort(order, found, sizeof(order[0]), compare_names);

    for (int i = 0; i < found; i++) {
        uint8_t background[GOODIX_IMAGE_RAW_SIZE];
        uint8_t finger[GOODIX_IMAGE_RAW_SIZE];
        char    path[512];
        char    stem[256];
        char   *dash;

        snprintf(stem, sizeof(stem), "%s", order[i]);
        stem[strlen(stem) - 7] = '\0';          /* drop "-fp.bin" */

        snprintf(path, sizeof(path), SAMPLE_DIR "/%s-fp.bin", stem);
        if (load_raw(path, finger) < 0)
            continue;

        snprintf(path, sizeof(path), SAMPLE_DIR "/%s-bg.bin", stem);
        if (load_raw(path, background) < 0)
            continue;

        /* The label is everything before the sample number. */
        snprintf(samples[sample_count].label, sizeof(samples[0].label),
                 "%s", stem);
        dash = strrchr(samples[sample_count].label, '-');
        if (dash)
            *dash = '\0';

        goodix_match_prepare(background, finger, &samples[sample_count].tmpl);
        sample_count++;
    }

    return sample_count;
}

static void describe(const char *title, const double *scores, int count)
{
    double sum = 0.0;
    double min = 2.0;
    double max = -2.0;
    double mean, variance = 0.0;

    if (count == 0) {
        printf("%-16s none\n", title);
        return;
    }

    for (int i = 0; i < count; i++) {
        sum += scores[i];
        if (scores[i] < min) min = scores[i];
        if (scores[i] > max) max = scores[i];
    }
    mean = sum / count;

    for (int i = 0; i < count; i++)
        variance += (scores[i] - mean) * (scores[i] - mean);
    variance /= count;

    printf("%-16s %4d pairs   min %6.3f   mean %6.3f   max %6.3f   sd %.3f\n",
           title, count, min, mean, max, variance > 0 ? sqrt(variance) : 0.0);
}

static void histogram(const char *title, const double *scores, int count)
{
    int buckets[BUCKETS] = { 0 };
    int tallest = 1;

    for (int i = 0; i < count; i++) {
        int b = (int)(scores[i] * BUCKETS);

        if (b < 0) b = 0;
        if (b >= BUCKETS) b = BUCKETS - 1;
        buckets[b]++;
    }

    for (int b = 0; b < BUCKETS; b++)
        if (buckets[b] > tallest)
            tallest = buckets[b];

    printf("\n%s\n", title);
    for (int b = 0; b < BUCKETS; b++) {
        int width = buckets[b] * 50 / tallest;

        printf("  %.2f  %3d  ", (double)b / BUCKETS, buckets[b]);
        for (int i = 0; i < width; i++)
            putchar('#');
        putchar('\n');
    }
}

int main(void)
{
    static double genuine[MAX_SAMPLES * MAX_SAMPLES];
    static double impostor[MAX_SAMPLES * MAX_SAMPLES];
    int           genuine_count  = 0;
    int           impostor_count = 0;

    if (collect() <= 0)
        return 1;

    printf("loaded %d samples:", sample_count);
    for (int i = 0; i < sample_count; i++)
        if (i == 0 || strcmp(samples[i].label, samples[i - 1].label) != 0)
            printf(" %s", samples[i].label);
    printf("\n\ncomparing every pair, this takes a moment\n\n");

    for (int i = 0; i < sample_count; i++) {
        for (int j = i + 1; j < sample_count; j++) {
            double score = goodix_match_score(&samples[i].tmpl,
                                              &samples[j].tmpl);

            if (strcmp(samples[i].label, samples[j].label) == 0)
                genuine[genuine_count++] = score;
            else
                impostor[impostor_count++] = score;
        }
    }

    describe("same finger", genuine, genuine_count);
    describe("different", impostor, impostor_count);

    histogram("same finger", genuine, genuine_count);
    histogram("different fingers", impostor, impostor_count);

    printf("\n threshold    rejected own    accepted other\n");
    for (double t = 0.05; t <= 0.95; t += 0.05) {
        int rejected = 0;
        int accepted = 0;

        for (int i = 0; i < genuine_count; i++)
            if (genuine[i] < t)
                rejected++;
        for (int i = 0; i < impostor_count; i++)
            if (impostor[i] >= t)
                accepted++;

        printf("   %4.2f      %4d / %-4d      %4d / %-4d\n",
               t, rejected, genuine_count, accepted, impostor_count);
    }

    return 0;
}
