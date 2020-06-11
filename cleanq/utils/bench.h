/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached license file.
 * if you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
 */

#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/cdefs.h>

///< provide a cycles_t implementation
typedef uint64_t cycles_t;

///< the bench control type definition
typedef struct bench_ctl
{
    ///< how many dimensions are in the result
    size_t result_dimensions;

    ///< the minimum runts we want to measure
    size_t min_runs;

    ///< the number of runs we measured so far
    size_t result_count;

    ///< pointer to the result
    cycles_t *data;
} bench_ctl_t;


/*
 * ================================================================================================
 * Reading Timestamp Counter Values
 * ================================================================================================
 */


///< flag for indicating whether we use rdtsc or rdtscp
extern bool bench_rdtscp_flag;


/**
 * @brief reads the cycle counter using rdtsc
 *
 * @returns the current timestamp counter
 */
static inline uint64_t rdtsc(void)
{
    uint32_t eax, edx;
    __asm volatile("rdtsc" : "=a"(eax), "=d"(edx)::"memory");
    return ((uint64_t)edx << 32) | eax;
}


/**
 * @brief reads the cycle counter using rdtscp
 *
 * @returns the current timestamp counter
 */
static inline uint64_t rdtscp(void)
{
    uint32_t eax, edx;
    __asm volatile("rdtscp" : "=a"(eax), "=d"(edx)::"ecx", "memory");
    return ((uint64_t)edx << 32) | eax;
}


/**
 * @brief reads the cycle counter using either rdtsc or rdtscp, depending on `bench_rdtscp_flag`
 *
 * @returns the current timestamp counter
 */
static inline cycles_t bench_tsc(void)
{
    if (bench_rdtscp_flag) {
        return rdtscp();
    }
    return rdtsc();
}


/**
 * @brief returns the overhead for reading the timestamp counter
 *
 * @returns TSC overhead
 */
cycles_t bench_tscoverhead(void);


/*
 * ================================================================================================
 * Benchmark Library Initialization
 * ================================================================================================
 */


/**
 * @brief Initialize benchmarking library
 */
void bench_init(void);


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
bench_ctl_t *bench_ctl_init(size_t dimensions, size_t min_runs);


/**
 * @brief Frees all resources associated with this benchmark control instance.
 *
 * @param ctl       the bench control handle
 *
 * Should be called after the benchmark is done and the results are dumped.
 */
void bench_ctl_destroy(bench_ctl_t *ctl);


/**
 * @brief Add results from one run of the benchmark.
 *
 * @param ctl       the bench control handle
 * @param result    Pointer to the 'dimensions' values of this run
 *
 * @return true if this was the last run necessary, false if more runs are needed.
 */
bool bench_ctl_add_run(bench_ctl_t *ctl, cycles_t *result);


/**
 * @brief dumps the benchmarking stats to stdout
 *
 * @param ctl           the bench control handle
 * @param dimension     the dimension to print
 * @param prefix        prefix used to print
 * @param tscperus      cyclers per micro-second
 */
void bench_ctl_dump_analysis(bench_ctl_t *ctl, size_t dimension, const char *prefix,
                             cycles_t tscperus);


#endif  // BENCH_H
