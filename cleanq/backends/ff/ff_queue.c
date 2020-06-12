/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>


#include <cleanq/cleanq.h>
#include <cleanq/backends/ff_queue.h>
#include <cleanq_backend.h>


#include "ffq_impl.h"


/*
 * ================================================================================================
 * Debugging Facility
 * ================================================================================================
 */

///< enable or disable debugging of this backend
//#define FFQ_DEBUG_ENABLED 1

///< the name of this programm
extern char *__progname;

#if defined(FFQ_DEBUG_ENABLED)
#    define FFQ_DEBUG(x...)                                                                       \
        do {                                                                                      \
            printf("FFQ:%s:%s:%d: ", __progname, __func__, __LINE__);                             \
            printf(x);                                                                            \
        } while (0)
#else
#    define FFQ_DEBUG(x...) ((void)0)
#endif


/*
 * ================================================================================================
 * FFQ Type Definitions
 * ================================================================================================
 */

///< this is the default size of the one-directional queue in message slots
#define FFQ_DEFAULT_SIZE 64

///< the size of a FFQ one-directional channel
#define FFQ_CHAN_SIZE (FFQ_DEFAULT_SIZE * FFQ_MSG_BYTES)

///< the total size of the bi-directional FFQ
#define FFQ_MEM_SIZE (2 * FFQ_CHAN_SIZE)


///< defines a FFQ CleanQ backend
struct cleanq_ffq
{
    ///< generic cleanq part
    struct cleanq q;

    ///< transmit FFQ channel
    struct ffq_chan txq;

    ///< receive FFQ channel
    struct ffq_chan rxq;

    ///< name of the queue
    char *name;

    ///< backing memory for descriptors
    void *rxtx_mem;

    ///< size of the backing memory
    size_t memsize;
};


/*
 * ================================================================================================
 * Special Command Messages
 * ================================================================================================
 */

#define FFQ_CMD_REGISTER 1
#define FFQ_CMD_DEREGISTER 2

static errval_t handle_register_command(struct cleanq_ffq *q, struct capref cap, uint32_t rid)
{
    errval_t err;
    err = cleanq_add_region((struct cleanq *)q, cap, rid);
    if (err_is_fail(err)) {
        return err;
    }

    if (q->q.callbacks.reg) {
        return q->q.callbacks.reg(&q->q, cap, rid);
    }

    return CLEANQ_ERR_OK;
}

static errval_t handle_deregister_command(struct cleanq_ffq *q, regionid_t rid)
{
    errval_t err;
    err = cleanq_remove_region((struct cleanq *)q, rid);
    if (err_is_fail(err)) {
        return err;
    }

    if (q->q.callbacks.dereg) {
        return q->q.callbacks.dereg(&q->q, rid);
    }

    return CLEANQ_ERR_OK;
}


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
static errval_t ff_enqueue(struct cleanq *queue, regionid_t region_id, genoffset_t offset,
                           genoffset_t length, genoffset_t valid_data, genoffset_t valid_length,
                           uint64_t misc_flags)
{
    struct cleanq_ffq *q = (struct cleanq_ffq *)queue;
    bool sent = ffq_impl_send(&q->txq, region_id, offset, length, valid_data, valid_length,
                              misc_flags);
    return sent ? CLEANQ_ERR_QUEUE_FULL : CLEANQ_ERR_OK;
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
static errval_t ff_dequeue(struct cleanq *queue, regionid_t *region_id, genoffset_t *offset,
                           genoffset_t *length, genoffset_t *valid_data, genoffset_t *valid_length,
                           uint64_t *misc_flags)
{
    errval_t err;

    struct cleanq_ffq *q = (struct cleanq_ffq *)queue;

    bool received = ffq_impl_recv(&q->rxq, (uint64_t *)region_id, offset, length, valid_data,
                                  valid_length, misc_flags);
    if (!received) {
        return CLEANQ_ERR_QUEUE_EMPTY;
    }

    /* we don't support any flags for now */
    if (*misc_flags == 0) {
        return CLEANQ_ERR_OK;
    }

    /* handle the case where the flags are register / deregister commands */
    if (*misc_flags == FFQ_CMD_REGISTER) {
        struct capref cap;
        cap.len = (size_t)*length;
        cap.vaddr = (void *)*offset;
        cap.paddr = (uint64_t)*valid_data;
        err = handle_register_command(q, cap, *region_id);
    } else {
        err = handle_deregister_command(q, *region_id);
    }

    /* TODO: should reply to the other side that operatin has failed */
    if (err_is_fail(err)) {
        printf("TODO: report failed outcome to other side\n");
    }

    /* we handled the commands now retry to receive again */
    return ff_dequeue(queue, region_id, offset, length, valid_data, valid_length, misc_flags);
}


/**
 * @brief Send a notification about new buffers on the queue
 *
 * @param q      The queue to call the operation on
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
static errval_t ff_notify(struct cleanq *q)
{
    (void)(q);
    return CLEANQ_ERR_OK;
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
static errval_t ff_register(struct cleanq *q, struct capref cap, regionid_t rid)
{
    /* we for the registration, we send a register command to the other side */
    return ff_enqueue(q, rid, (genoffset_t)cap.vaddr, (genoffset_t)cap.len, (genoffset_t)cap.paddr,
                      0, FFQ_CMD_REGISTER);
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
static errval_t ff_deregister(struct cleanq *q, regionid_t rid)
{
    /* we for the de-registration, we send a register command to the other side */
    return ff_enqueue(q, rid, 0, 0, 0, 0, FFQ_CMD_DEREGISTER);
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
errval_t ff_control(struct cleanq *q, uint64_t request, uint64_t value, uint64_t *result)
{
    (void)(q);
    (void)(request);
    (void)(value);
    (void)(result);
    return CLEANQ_ERR_OK;
}


/*
 * ================================================================================================
 * Queue Destruction and Creation
 * ================================================================================================
 */


/**
 * @brief destroys the queue
 *
 * @param q     The queue state to free (and the queue to be shut down)
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t ff_destroy(struct cleanq *q)
{
    struct cleanq_ffq *ffq = (struct cleanq_ffq *)q;

    errval_t err = CLEANQ_ERR_OK;

    if (ffq->rxtx_mem && munmap(ffq->rxtx_mem, ffq->memsize) == -1) {
        printf("WARNING: FFQ destroy failed. (munmap)\n");
    }

    if (ffq->name && shm_unlink(ffq->name) == -1) {
        printf("WARNING: FFQ destroy failed. (shm_unlink)\n");
    }

    free(ffq->name);
    free(q);

    return err;
}


/**
 * @brief initialized a the ffq backend
 *
 * @param q         Return pointer to the descriptor queue
 * @param name      Name of the memory use for sending messages
 * @param clear     Write 0 to memory
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_ffq_create(struct cleanq_ffq **q, const char *qname, bool clear)
{
    errval_t err;
    struct cleanq_ffq *newq;

    FFQ_DEBUG("create start\n");

    newq = (struct cleanq_ffq *)calloc(sizeof(struct cleanq_ffq), 1);
    if (newq == NULL) {
        return CLEANQ_ERR_MALLOC_FAIL;
    }

    newq->name = strdup(qname);
    if (newq->name == NULL) {
        goto cleanup1;
    }

    bool creator = true;

    /* try to create the memobj with exclusive first */
    int fd = shm_open(newq->name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd == -1) {
        /* we're not the creator of the queue */
        creator = false;
        clear = false;

        /* try again without the exclusive flag */
        fd = shm_open(newq->name, O_RDWR | O_CREAT, 0600);
        if (fd == -1) {
            goto cleanup2;
        }
    }

    if (creator && ftruncate(fd, FFQ_MEM_SIZE)) {
        goto cleanup3;
    }

    FFQ_DEBUG("Mapping queue frame\n");
    void *buf = mmap(NULL, FFQ_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) {
        err = CLEANQ_ERR_MALLOC_FAIL;
        goto cleanup3;
    }

    if (clear) {
        memset(buf, 0, FFQ_MEM_SIZE);
    }

    newq->rxtx_mem = buf;
    newq->memsize = FFQ_MEM_SIZE;


    /* initialize the ffq rx/tx channels */
    buf = creator ? newq->rxtx_mem : newq->rxtx_mem + FFQ_CHAN_SIZE;
    ffq_impl_init_rx(&newq->rxq, buf, FFQ_DEFAULT_SIZE);

    buf = creator ? newq->rxtx_mem + FFQ_CHAN_SIZE: newq->rxtx_mem;
    ffq_impl_init_tx(&newq->txq, buf, FFQ_DEFAULT_SIZE);

    /* initializing  the generic cleanq part */
    err = cleanq_init(&newq->q);
    if (err_is_fail(err)) {
        goto cleanup4;
    }

    /* setting the function pointers */
    newq->q.f.enq = ff_enqueue;
    newq->q.f.deq = ff_dequeue;
    newq->q.f.reg = ff_register;
    newq->q.f.dereg = ff_deregister;
    newq->q.f.notify = ff_notify;
    newq->q.f.ctrl = ff_control;
    newq->q.f.destroy = ff_destroy;

    *q = newq;

    FFQ_DEBUG("create end %p \n", *q);

    return CLEANQ_ERR_OK;

cleanup4:
    munmap(newq->rxtx_mem, newq->memsize);
cleanup3:
    shm_unlink(newq->name);
cleanup2:
    free(newq->name);
cleanup1:
    free(newq);

    return CLEANQ_ERR_INIT_QUEUE;
}
