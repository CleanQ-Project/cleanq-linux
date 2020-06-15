/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <cleanq/cleanq.h>
#include <cleanq/backends/loopback_queue.h>
#include <cleanq_backend.h>



/*
 * LOOPBACK QEUEUE
 * ===============
 *
 * This backend defines a simple queue which reflects the elements written to the
 * queue back to the sender.
 */


///< defines the size of the looback queue
#define LOOPBACK_QUEUE_SIZE 64

///< defines the looback queue backend
struct cleanq_loopbackq
{
    ///< generic cleanq part
    struct cleanq q;

    ///< holds queue descriptors for the loopback queue
    struct cleanq_buf queue[LOOPBACK_QUEUE_SIZE];

    ///< the head pointer of the queue
    size_t head;

    ///< the tail pointer of the queue
    size_t tail;

    ///< the number of elements in the queue
    size_t num_ele;
};


/*
 * ================================================================================================
 * Queue Operations
 * ================================================================================================
 */


/*
 * ------------------------------------------------------------------------------------------------
 * Enqueue()
 * ------------------------------------------------------------------------------------------------
 */

static errval_t loopback_enqueue(struct cleanq *q, regionid_t rid, genoffset_t offset,
                                 genoffset_t length, genoffset_t valid_data,
                                 genoffset_t valid_length, uint64_t flags)
{
    assert(q);

    struct cleanq_loopbackq *lq = (struct cleanq_loopbackq *)q;

    if (lq->num_ele == LOOPBACK_QUEUE_SIZE) {
        return CLEANQ_ERR_QUEUE_FULL;
    }

    lq->queue[lq->head].offset = offset;
    lq->queue[lq->head].length = length;
    lq->queue[lq->head].valid_data = valid_data;
    lq->queue[lq->head].valid_length = valid_length;
    lq->queue[lq->head].flags = flags;
    lq->queue[lq->head].rid = rid;

    lq->head = (lq->head + 1) % LOOPBACK_QUEUE_SIZE;
    lq->num_ele++;

    __builtin_prefetch(&lq->queue[lq->head]);

    return CLEANQ_ERR_OK;
}

/*
 * ------------------------------------------------------------------------------------------------
 * Dequeue()
 * ------------------------------------------------------------------------------------------------
 */

static errval_t loopback_dequeue(struct cleanq *q, regionid_t *rid, genoffset_t *offset,
                                 genoffset_t *length, genoffset_t *valid_data,
                                 genoffset_t *valid_length, uint64_t *flags)
{
    assert(q);

    struct cleanq_loopbackq *lq = (struct cleanq_loopbackq *)q;
    if (lq->num_ele == 0) {
        return CLEANQ_ERR_QUEUE_EMPTY;
    }

    *offset = lq->queue[lq->tail].offset;
    *length = lq->queue[lq->tail].length;
    *valid_data = lq->queue[lq->tail].valid_data;
    *valid_length = lq->queue[lq->tail].valid_length;
    *flags = lq->queue[lq->tail].flags;
    *rid = lq->queue[lq->tail].rid;

    lq->tail = (lq->tail + 1) % LOOPBACK_QUEUE_SIZE;
    lq->num_ele--;

    __builtin_prefetch(&lq->queue[lq->tail]);

    return CLEANQ_ERR_OK;
}

/*
 * ------------------------------------------------------------------------------------------------
 * Notify()
 * ------------------------------------------------------------------------------------------------
 */

static errval_t loopback_notify(struct cleanq *q)
{
    (void)(q);

    return CLEANQ_ERR_OK;
}

/*
 * ------------------------------------------------------------------------------------------------
 * Register()
 * ------------------------------------------------------------------------------------------------
 */

static errval_t loopback_register(struct cleanq *q, struct capref cap, regionid_t region_id)
{
    (void)(q);
    (void)(cap);
    (void)(region_id);

    return CLEANQ_ERR_OK;
}

/*
 * ------------------------------------------------------------------------------------------------
 * Deregister()
 * ------------------------------------------------------------------------------------------------
 */

static errval_t loopback_deregister(struct cleanq *q, regionid_t region_id)
{
    (void)(q);
    (void)(region_id);

    return CLEANQ_ERR_OK;
}

/*
 * ------------------------------------------------------------------------------------------------
 * Control()
 * ------------------------------------------------------------------------------------------------
 */

static errval_t loopback_control(struct cleanq *q, uint64_t request, uint64_t value,
                                 uint64_t *result)
{
    (void)(q);
    (void)(request);
    (void)(value);
    (void)(result);

    // TODO Might have some options for loopback device?
    return CLEANQ_ERR_OK;
}


/*
 * ================================================================================================
 * Queue Destruction
 * ================================================================================================
 */


/**
 * @brief destroys a created loopback queue
 *
 * @param q    the loopback queue to be created
 */
static errval_t loopback_destroy(struct cleanq *q)
{
    assert(q);

    /* free the queue */
    free((struct cleanq_loopbackq *)q);

    return CLEANQ_ERR_OK;
}


/*
 * ================================================================================================
 * Queue Creation
 * ================================================================================================
 */


/**
 * @brief creates a new loopback queue
 *
 * @param q     returned pointer to the newly created queue
 *
 * @returns CLEANQ_ERR_OK - on success
 *          CLEANQ_ERR_MALLOC_FAIL - if the queue could not be initialized
 */
errval_t loopback_queue_create(struct cleanq_loopbackq **q)
{
    assert(q);

    struct cleanq_loopbackq *lq = (struct cleanq_loopbackq *)calloc(1, sizeof(struct cleanq_loopbackq));
    if (lq == NULL) {
        return CLEANQ_ERR_MALLOC_FAIL;
    }

    /* initialize generic part */
    errval_t err;
    err = cleanq_init(&lq->q);
    if (err_is_fail(err)) {
        free(lq);
        return err;
    }

    /* initialize the pointers */
    lq->head = 0;
    lq->tail = 0;
    lq->num_ele = 0;

    /* setting the function pointers */
    lq->q.f.enq = loopback_enqueue;
    lq->q.f.deq = loopback_dequeue;
    lq->q.f.reg = loopback_register;
    lq->q.f.dereg = loopback_deregister;
    lq->q.f.ctrl = loopback_control;
    lq->q.f.notify = loopback_notify;
    lq->q.f.destroy = loopback_destroy;

    /* setting the return pointer */
    *q = lq;

    return CLEANQ_ERR_OK;
}
