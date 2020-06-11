/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached license file.
 * if you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <inttypes.h>

#include "bench.h"

///< this is the overhead of taking a timestamp
static cycles_t tsc_overhead;

///< flag to indicate this library is initalized
static uint8_t bench_is_initialized = 0;

////< flag indicating whether to use rdtsc or rdtscp
bool bench_rdtscp_flag = true;


/*
 * ================================================================================================
 * Benchmark Library Initialization
 * ================================================================================================
 */


/**
 * \brief Measure overhead of taking timestamp
 */
static void measure_tsc(void)
{
    uint64_t begin;
    uint64_t end;

    begin = bench_tsc();
    for (int i = 0; i < 1000; i++) {
        end = bench_tsc();
    }

    tsc_overhead = (end - begin) / 1000;
}

/**
 * @brief Initialize benchmarking library
 */
void bench_init(void)
{
    if (bench_is_initialized) {
        return;
    }

    /* Measure overhead of taking timestamps */
    measure_tsc();

    bench_is_initialized = 1;
}


/*
 * ================================================================================================
 * Reading Timestamp Counter Values
 * ================================================================================================
 */


/**
 * Return the measured tsc overhead
 */
cycles_t bench_tscoverhead(void)
{
    if (!bench_is_initialized) {
        bench_init();
    }
    return tsc_overhead;
}


/*
 * ================================================================================================
 * Analysis Functions
 * ================================================================================================
 */


/**
 * \brief Compute averages
 *
 * If certain datapoints should be ignored, they should be marked with
 * #BENCH_IGNORE_WATERMARK
 */
static cycles_t bench_avg(cycles_t *array, size_t len)
{
    cycles_t sum = 0;
    size_t count = 0;

    // Discarding some initial observations
    for (size_t i = len >> 3; i < len; i++) {
        sum += array[i];
        count++;
    }

    return sum / count;
}


/**
 * @brief computes the standard deviation s^2 of the sample data
 *
 * @param array         array of data to analyze
 * @param len           size of the array
 * @param correction    apply Bessel's correction (using N-1 instead of N)
 * @param ret_avg       returns the average of the sample
 * @param ret_stddev    returns the standard deviation squared of the sample
 */
static void bench_stddev(cycles_t *array, size_t len, uint8_t correction, cycles_t *ret_avg,
                         cycles_t *ret_stddev)
{
    cycles_t avg = bench_avg(array, len);

    cycles_t sum = 0;
    size_t count = 0;

    /// discard some initial observations
    for (size_t i = len >> 3; i < len; i++) {
        cycles_t subsum = array[i] - avg;
        sum += (subsum * subsum);
        count++;
    }

    cycles_t std_dev = 0;
    if (correction && count > 1) {
        std_dev = sum / (count - 1);
    } else {
        std_dev = sum / count;
    }

    if (ret_avg) {
        *ret_avg = avg;
    }

    if (ret_stddev) {
        *ret_stddev = std_dev;
    }
}


/*
 * ================================================================================================
 * Helper Functions
 * ================================================================================================
 */


/**
 * @brief obtains a single dimension of the result
 *
 * @param ctl           the benchmark control
 * @param dimension     the dimension
 *
 * @returns newliy allocated array with the data points along dimension
 */
static cycles_t *get_array(bench_ctl_t *ctl, size_t dimension)
{
    cycles_t *array = (cycles_t *)calloc(ctl->result_count, sizeof(cycles_t));
    assert(array != NULL);

    for (size_t i = 0; i < ctl->result_count; i++) {
        array[i] = *(ctl->data + (ctl->result_dimensions * i + dimension));
    }
    return array;
}


/**
 * @brief comparater function for sorting the cycles
 *
 * @param a     the first value
 * @param b     the second value
 *
 * @returns -1 if a < b, 0 if a == b, 1 if a > b
 */
static int cycles_comparator(const void *a, const void *b)
{
    if (*(cycles_t *)a == *(cycles_t *)b) {
        return 0;
    }

    if (*(cycles_t *)a < *(cycles_t *)b) {
        return -1;
    }

    return 1;
}


/*
 * ================================================================================================
 * Benchmark Control Functions
 * ================================================================================================
 */


/**
 * Initialize a benchmark control instance.
 *
 * @param dimensions Number of values each run produces
 * @param min_runs   Minimal number of runs to be executed
 *
 * @return Control handle, to be passed on subsequent calls to bench_ctl_functions.
 */
bench_ctl_t *bench_ctl_init(size_t dimensions, size_t min_runs)
{
    bench_ctl_t *ctl;

    ctl = (bench_ctl_t *)calloc(1, sizeof(*ctl));
    if (ctl == NULL) {
        return NULL;
    }

    ctl->result_dimensions = dimensions;
    ctl->min_runs = min_runs;

    ctl->data = (cycles_t *)calloc(min_runs * dimensions, sizeof(*ctl->data));
    if (ctl->data == NULL) {
        free(ctl);
        return NULL;
    }

    return ctl;
}


/**
 * @brief Frees all resources associated with this benchmark control instance.
 *
 * @param ctl       the bench control handle
 *
 * Should be called after the benchmark is done and the results are dumped.
 */
void bench_ctl_destroy(bench_ctl_t *ctl)
{
    if (ctl == NULL) {
        return;
    }

    if (ctl->data) {
        free(ctl->data);
    }

    free(ctl);
}


/**
 * @brief Add results from one run of the benchmark.
 *
 * @param ctl       the bench control handle
 * @param result    Pointer to the 'dimensions' values of this run
 *
 * @return true if this was the last run necessary, false if more runs are needed.
 */
bool bench_ctl_add_run(bench_ctl_t *ctl, cycles_t *result)
{
    cycles_t *dst;

    if (ctl->result_count == ctl->min_runs) {
        return true;
    }

    dst = ctl->data + ctl->result_count * ctl->result_dimensions;
    memcpy(dst, result, sizeof(dst) * ctl->result_dimensions);

    ctl->result_count++;

    return ctl->result_count == ctl->min_runs;
}


/**
 * @brief dumps the benchmarking stats to stdout
 *
 * @param ctl           the bench control handle
 * @param dimension     the dimension to print
 * @param prefix        prefix used to print
 * @param tscperus      cyclers per micro-second
 */
void bench_ctl_dump_analysis(bench_ctl_t *ctl, size_t dimension, const char *prefix,
                             cycles_t tscperus)
{
    size_t len = ctl->result_count;
    cycles_t *array = get_array(ctl, dimension);
    if (array == NULL) {
        printf("Failed to allocate memory for doing benchmark analysis\n");
        return;
    }

    cycles_t avg, std_dev;
    bench_stddev(array, len, 0, &avg, &std_dev);

    qsort(array, len, sizeof(cycles_t), cycles_comparator);

    size_t max99 = (size_t)((0.99 * len) + 0.5);
    printf("run [%" PRIu64 "], med_pos[%" PRIu64 "], min_pos[%" PRIu64 "], "
           "P99[%" PRIu64 "], max[%" PRIu64 "]\n",
           (uint64_t)len, (uint64_t)(len / 2), (uint64_t)0, (uint64_t)(max99 - 1),
           (uint64_t)(len - 1));

    printf("run [%" PRIu64 "], avg[%" PRIu64 "], med[%" PRIu64 "], min[%" PRIu64 "], "
           "P99[%" PRIu64 "], max[%" PRIu64 "], stdev[%" PRIu64 "]\n",
           (uint64_t)len, (uint64_t)avg, (uint64_t)array[len / 2], (uint64_t)array[0],
           (uint64_t)array[max99 - 1], (uint64_t)array[len - 1], (uint64_t)std_dev);

    printf("run [%" PRIu64 "], avg[%f], med[%f], min[%f], "
           "P99[%f], max[%f], stdev[%f]\n",
           (uint64_t)len, avg / (float)tscperus, (array[len / 2] / (float)tscperus),
           (array[0] / (float)tscperus), (array[max99 - 1] / (float)tscperus),
           (array[len - 1] / (float)tscperus), std_dev / (float)tscperus);

    printf("%s, %" PRIu64 " %f %f %f %f\n", prefix, (uint64_t)len,
           (array[len / 2] / (float)tscperus), (array[0] / (float)tscperus),
           (array[max99 - 1] / (float)tscperus), (array[len - 1] / (float)tscperus));

    free(array);
}
