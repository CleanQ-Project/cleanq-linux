/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached license file.
 * if you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
 */

#ifndef CLEAN_QUEUE_H_
#define CLEAN_QUEUE_H_ 1

#include <stddef.h>
#include <stdint.h>


/*
 * ================================================================================================
 * Type Definitions
 * ================================================================================================
 */


///< forward declaration of the generic cleanq
struct cleanq;

///< forward declaration of the region pool
struct region_pool;

///< the region identifier for registered regions
typedef uint32_t regionid_t;

///< the identifier of a buffer
typedef uint32_t bufferid_t;

///< the offset within a region
typedef uint64_t genoffset_t;


///< represents an allocated block of memory
struct capref
{
    ///< the virtual address of this block of memory
    void *vaddr;

    ///< the physical address of the block of memory
    uint64_t paddr;

    ///< the size of the block in bytes
    size_t len;
};


/*
 * ================================================================================================
 * CleanQ Buffer
 * ================================================================================================
 */


///< represents the flag for the last buffer in a chain
#define CLEANQ_FLAG_LAST (1UL << 30)

///< sets the buffer alignmen. This should be the size of a cache line
#define CLEANQ_BUFFER_ALIGNMENT 64

///< represents a cleanq buffer
struct __attribute__((aligned(CLEANQ_BUFFER_ALIGNMENT))) cleanq_buf
{
    ///< the offset into the region
    genoffset_t offset;

    ///< the length of the buffer
    genoffset_t length;

    ///< the offset in which the valid data starts from the start of the buffer
    genoffset_t valid_data;

    ///< the length of the valid data in the buffer
    genoffset_t valid_length;

    ///< the flags of the buffer
    uint64_t flags;

    ///< the region this buffer belongs to
    regionid_t rid;
};


/*
 * ================================================================================================
 * Errors
 * ================================================================================================
 */


///< the error types
typedef enum {
    CLEANQ_ERR_OK = 0,                 ///< operation succeeded
    CLEANQ_ERR_INIT_QUEUE,             ///< could not initalize the queue
    CLEANQ_ERR_BUFFER_ID,              ///< invalid buffer region
    CLEANQ_ERR_BUFFER_NOT_IN_REGION,   ///< supplied buffer was not in a region
    CLEANQ_ERR_BUFFER_ALREADY_IN_USE,  ///< the buffer is already in use
    CLEANQ_ERR_INVALID_BUFFER_ARGS,    ///< invalid buffer arguments
    CLEANQ_ERR_INVALID_REGION_ID,      ///< the region id was not valid
    CLEANQ_ERR_REGION_DESTROY,         ///< could not destroy the region
    CLEANQ_ERR_INVALID_REGION_ARGS,    ///< invalid region arguments
    CLEANQ_ERR_QUEUE_EMPTY,            ///< the queue was emtpy
    CLEANQ_ERR_QUEUE_FULL,             ///< the queue was full
    CLEANQ_ERR_BUFFER_NOT_IN_USE,      ///< the buffer was not in use
    CLEANQ_ERR_MALLOC_FAIL             ///< memory allocation faiiled
} errval_t;


/**
 * @brief checks if the error value represents a success
 *
 * @param err   the error value
 *
 * @return true if the represents success, false otherwise
 */
static inline int err_is_ok(errval_t err)
{
    return err == CLEANQ_ERR_OK;
}


/**
 * @brief checks if the error value represents a failure
 *
 * @param err   the error value
 *
 * @return true if the represents failure, false otherwise
 */
static inline int err_is_fail(errval_t err)
{
    return err != CLEANQ_ERR_OK;
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
errval_t cleanq_enqueue(struct cleanq *q, regionid_t region_id, genoffset_t offset,
                        genoffset_t lenght, genoffset_t valid_data, genoffset_t valid_lenght,
                        uint64_t misc_flags);


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
                        genoffset_t *langht, genoffset_t *valid_data, genoffset_t *valid_length,
                        uint64_t *misc_flags);


/**
 * @brief Send a notification about new buffers on the queue
 *
 * @param q      The queue to call the operation on
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_notify(struct cleanq *q);


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
errval_t cleanq_register(struct cleanq *q, struct capref cap, regionid_t *region_id);

/**
 * @brief Remove a memory region
 *
 * @param q              The queue to call the operation on
 * @param region_id      The region id to remove from the queues memory
 * @param cap            The capability to the removed memory
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_deregister(struct cleanq *q, regionid_t region_id, struct capref *cap);


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
errval_t cleanq_control(struct cleanq *q, uint64_t request, uint64_t value, uint64_t *result);


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
errval_t cleanq_destroy(struct cleanq *q);


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
void cleanq_set_state(struct cleanq *q, void *state);


/**
 * @brief obtains the state pointer of the queue
 *
 * @param q     the cleanq to obtain the state from
 *
 * @returns pointer to the previoiusly set state
 */
void *cleanq_get_state(struct cleanq *q);


#endif /* CLEAN_QUEUE_H_ */
