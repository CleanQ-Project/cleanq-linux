/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <assert.h>
#include <string.h>

#include <cleanq/cleanq.h>

#include <cleanq_backend.h>
#include <region_pool.h>
#include <debug.h>


/*
 * ================================================================================================
 * Benchmarking Integration
 * ================================================================================================
 */


#ifdef BENCH_CLEANQ
#    include <bench.h>
#    define NUM_ROUNDS 100000

/* memory for storing measurement values */
static cycles_t enq[NUM_ROUNDS];
static cycles_t deq[NUM_ROUNDS];
static cycles_t reg[NUM_ROUNDS];
static cycles_t dereg[NUM_ROUNDS];

/* benchmarking control */
static bench_ctl_t ctl_enq;
static bench_ctl_t ctl_deq;
static bench_ctl_t ctl_reg;
static bench_ctl_t ctl_dereg;

/* benchmarking init flag */
static bool bench_init_done;

static void add_bench_entry(bench_ctl_t *ctl, cycles_t diff, const char *prefix)
{
    if (!bench_init_done) {
        memset(enq, 0, sizeof(enq));
        memset(deq, 0, sizeof(deq));
        memset(reg, 0, sizeof(reg));
        memset(dereg, 0, sizeof(dereg));

        memset(&ctl_enq, 0, sizeof(ctl_enq));
        memset(&ctl_deq, 0, sizeof(ctl_deq));
        memset(&ctl_reg, 0, sizeof(ctl_reg));
        memset(&ctl_dereg, 0, sizeof(ctl_dereg));

        ctl_enq.result_dimensions = 1;
        ctl_enq.min_runs = NUM_ROUNDS;
        ctl_enq.data = enq;

        ctl_deq.result_dimensions = 1;
        ctl_deq.min_runs = NUM_ROUNDS;
        ctl_deq.data = deq;

        ctl_reg.result_dimensions = 1;
        ctl_reg.min_runs = NUM_ROUNDS;
        ctl_reg.data = reg;

        ctl_dereg.result_dimensions = 1;
        ctl_dereg.min_runs = NUM_ROUNDS;
        ctl_dereg.data = dereg;

        bench_init_done = true;
    }

    bench_ctl_add_run(ctl, &diff);

    if (ctl->result_count == NUM_ROUNDS) {
        bench_ctl_dump_analysis(ctl, 1, prefix, 1);
        ctl->result_count = 0;
        memset(ctl->data, 0, sizeof(cycles_t) * NUM_ROUNDS);
    }
}

#    define BENCH_START() cycles_t bench_cleanq_start = bench_tsc();
#    define BENCH_END(stat)                                                                       \
        cycles_t bench_cleanq_end = bench_tsc();                                                  \
        add_bench_entry(stat, bench_cleanq_end - bench_cleanq_start, __FUNCTION__)

#else
#    define BENCH_START()
#    define BENCH_END(stat)
#endif


/*
 * ================================================================================================
 * Datapath functions
 * ================================================================================================
 */


/**
 * @brief enqueue a buffer into the queue
 *
 * @param q             The queue to call the operation on
 * @param region_id     Id of the memory region the buffer belongs to
 * @param offset        Offset into the region i.e. where the buffer starts that is enqueued
 * @param lenght        Lenght of the enqueued buffer
 * @param valid_data    Offset into the buffer where the valid data of this buffer starts
 * @param valid_length  Length of the valid data of this buffer
 * @param misc_flags    Any other argument that makes sense to the queue
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_enqueue(struct cleanq *q, regionid_t region_id, genoffset_t offset,
                        genoffset_t length, genoffset_t valid_data, genoffset_t valid_length,
                        uint64_t misc_flags)
{
    assert(q);

    errval_t err;

    // check if the buffer to enqueue is valid
    if (!region_pool_buffer_check_bounds(q->pool, region_id, offset, length, valid_data,
                                         valid_length)) {
        return CLEANQ_ERR_INVALID_BUFFER_ARGS;
    }

    BENCH_START();
    err = q->f.enq(q, region_id, offset, length, valid_data, valid_length, misc_flags);
    BENCH_END(&ctl_enq);

    DQI_DEBUG("Enqueue q=%p rid=%d, offset=%lu, lenght=%lu\n", q, region_id, offset, valid_length);

    return err;
}


/**
 * @brief dequeue a buffer from the queue
 *
 * @param q             The queue to call the operation on
 * @param region_id     Return pointer to the id of the memory region the buffer belongs to
 * @param region_offset Return pointer to the offset into the region where this buffer starts.
 * @param lenght        Return pointer to the lenght of the dequeue buffer
 * @param valid_data    Return pointer to where the valid data of this buffer starts
 * @param valid_length  Return pointer to the length of the valid data of this buffer
 * @param misc_flags    Return value from other endpoint
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_dequeue(struct cleanq *q, regionid_t *region_id, genoffset_t *offset,
                        genoffset_t *length, genoffset_t *valid_data, genoffset_t *valid_length,
                        uint64_t *misc_flags)
{
    errval_t err;

    assert(q);
    assert(region_id);
    assert(offset);
    assert(length);
    assert(valid_length);
    assert(valid_data);

    BENCH_START();
    err = q->f.deq(q, region_id, offset, length, valid_data, valid_length, misc_flags);
    if (err_is_fail(err)) {
        return err;
    }
    BENCH_END(&ctl_deq);

    // check if the dequeue buffer is valid
    if (!region_pool_buffer_check_bounds(q->pool, *region_id, *offset, *length, *valid_data,
                                         *valid_length)) {
        return CLEANQ_ERR_INVALID_BUFFER_ARGS;
    }

    DQI_DEBUG("Dequeue q=%p rid=%u, offset=%lu \n", q, *region_id, *offset);

    return CLEANQ_ERR_OK;
}


/**
 * @brief Send a notification about new buffers on the queue
 *
 * @param q      The queue to call the operation on
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_notify(struct cleanq *q)
{
    assert(q);

    return q->f.notify(q);
}


/*
 * ================================================================================================
 * Memory Registration and Deregistration
 * ================================================================================================
 */


/**
 * @brief Add a memory region that can be used as buffers to the queue
 *
 * @param q              The queue to call the operation on
 * @param cap            A Capability for some memory
 * @param region_id      Return pointer to a region id that is assigned
 *                       to the memory
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_register(struct cleanq *q, struct capref cap, regionid_t *region_id)
{
    assert(q);

    errval_t err;
    err = region_pool_add_region(q->pool, cap, region_id);
    if (err_is_fail(err)) {
        return err;
    }

    DQI_DEBUG("register q=%p, cap=%p, regionid=%d \n", (void *)q, (void *)&cap, *region_id);

    BENCH_START();
    err = q->f.reg(q, cap, *region_id);
    BENCH_END(&ctl_reg);

    return err;
}


/**
 * @brief Remove a memory region
 *
 * @param q              The queue to call the operation on
 * @param region_id      The region id to remove from the queues memory
 * @param cap            The capability to the removed memory
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_deregister(struct cleanq *q, regionid_t region_id, struct capref *cap)
{
    assert(q);

    errval_t err;
    err = region_pool_remove_region(q->pool, region_id, cap);
    if (err_is_fail(err)) {
        return err;
    }

    DQI_DEBUG("deregister q=%p, cap=%p, regionid=%d \n", (void *)q, (void *)cap, region_id);

    BENCH_START();
    err = q->f.dereg(q, region_id);
    BENCH_END(&ctl_dereg);

    return err;
}


/*
 * ================================================================================================
 * Control Path
 * ================================================================================================
 */


/**
 * @brief Send a control message to the queue
 *
 * @param q          The queue to call the operation on
 * @param request    The type of the control message*
 * @param value      The value for the request
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_control(struct cleanq *q, uint64_t request, uint64_t value, uint64_t *result)
{
    assert(q);

    return q->f.ctrl(q, request, value, result);
}


/*
 * ================================================================================================
 * Queue Destruction and Creation
 * ================================================================================================
 */


/**
 * @brief destroys the device queue
 *
 * @param q     The queue state to free (and the device queue to be shut down)
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_destroy(struct cleanq *q)
{
    assert(q);

    /* freeing up the state of the library */
    errval_t err;
    err = region_pool_destroy(q->pool);
    if (err_is_fail(err)) {
        return err;
    }

    /* calling the backend specific cleanup function */
    return q->f.destroy(q);
}


/* NOTE: creation must be done using a specific backend */


/*
 * ================================================================================================
 * Setting/getting state
 * ================================================================================================
 */


/**
 * @brief sets the state pointer of a queue
 *
 * @param q         the cleanq to set the state
 * @param state     the state to set
 */
void cleanq_set_state(struct cleanq *q, void *state)
{
    assert(q);

    q->state = state;
}


/**
 * @brief obtains the state pointer of the queue
 *
 * @param q     the cleanq to obtain the state from
 *
 * @returns pointer to the previoiusly set state
 */
void *cleanq_get_state(struct cleanq *q)
{
    assert(q);

    return q->state;
}


/*
 * ================================================================================================
 * Setting/getting user state
 * ================================================================================================
 */


/**
 * @brief sets the callback function for the register operation
 *
 * @param q     the cleanq queue state
 * @param cb    callback function to be called
 */
void cleanq_set_register_callback(struct cleanq *q, cleanq_register_callback_t cb)
{
    assert(q);

    q->callbacks.reg = cb;
}


/**
 * @brief sets the callback function for the deregister operation
 *
 * @param q     the cleanq queue state
 * @param cb    callback function to be called
 */
void cleanq_set_deregister_callback(struct cleanq *q, cleanq_deregister_callback_t cb)
{
    assert(q);

    q->callbacks.dereg = cb;
}
