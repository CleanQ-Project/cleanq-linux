/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached license file.
 * if you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
 */


#ifndef CLEANQ_BACKEND_H_
#define CLEANQ_BACKEND_H_ 1

#include <stdbool.h>

#include <cleanq/cleanq.h>


/*
 * ================================================================================================
 * Backend function type definitions - Must be implemented by every backend
 * ================================================================================================
 */


/*
 * ------------------------------------------------------------------------------------------------
 * Datapath functions
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief Directly enqueues something into a hardware queue. Only used by direct endpoints
 *
 * @param q             The device queue handle
 * @param region_id     The region id of the buffer
 * @param offset        Offset into the region where the buffer starts
 * @param length        Length of the buffer
 * @param valid_data    Offset into the region where the valid data of the buffer starts
 * @param valid_length  Length of the valid data in this buffer
 * @param misc_flags    Misc flags
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
typedef errval_t (*cleanq_enqueue_t)(struct cleanq *q, regionid_t region_id, genoffset_t offset,
                                     genoffset_t length, genoffset_t valid_offset,
                                     genoffset_t valid_length, uint64_t misc_flags);


/**
 * @brief Directly dequeus something from a hardware queue. Only used by direct endpoints
 *
 * @param q             The device queue handle
 * @param region_id     The region id of the buffer
 * @param offset        Return pointer to the offset into the region where the buffer starts
 * @param length        Return pointer to the length of the buffer
 * @param valid_offset  Return pointer to where the valid data of this buffer starts
 * @param valid_length  Return pointer to the length of the valid data of this buffer
 * @param misc_flags    Misc flags
 *
 * @returns error on failure if the queue is empty or CLEANQ_ERR_OK on success
 */
typedef errval_t (*cleanq_dequeue_t)(struct cleanq *q, regionid_t *region_id, genoffset_t *offset,
                                     genoffset_t *length, genoffset_t *valid_offset,
                                     genoffset_t *valid_length, uint64_t *misc_flags);


/**
 * @brief Notifies the device of new descriptors in the queue.
 *
 * @param q     The device queue
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 *
 * On a notificaton, the device can dequeue descritpors from the queue.
 * NOTE: Does nothing for direct queues since there is no other endpoint to notify!
 *       (i.e. it is the same process)
 */
typedef errval_t (*cleanq_notify_t)(struct cleanq *q);


/*
 * ------------------------------------------------------------------------------------------------
 * Memory Registration and Deregistration
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief Registers a memory region.
 *
 * @param q             The device queue handle
 * @param cap           The capability of the memory region
 * @param reigon_id     The region id
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 *
 * For direct queues this function has to handle the communication to the device driver since there
 * might also be a need to set up some local state for the direct queue that is device specific.
 */
typedef errval_t (*cleanq_register_t)(struct cleanq *q, struct capref cap, regionid_t region_id);


/**
 * @brief Deregisters a memory region. (Similar communication constraints as register)
 *
 * @param q             The device queue handle
 * @param reigon_id     The region id
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
typedef errval_t (*cleanq_deregister_t)(struct cleanq *q, regionid_t region_id);


/*
 * ------------------------------------------------------------------------------------------------
 * Memory Registration and Deregistration
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief handles a control message to the device (Similar communication constraints as register)
 *
 * @param q         The device queue handle
 * @param request   The request type
 * @param value     The value to the request
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
typedef errval_t (*cleanq_control_t)(struct cleanq *q, uint64_t request, uint64_t value,
                                     uint64_t *result);


/*
 * ------------------------------------------------------------------------------------------------
 * Queue Destruction and Creation
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief Destroys the queue give as an argument.
 *
 * @param q         The device queue
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 *
 * NOTE:  first the state of the library, then the queue specific part by calling a function pointer
 */
typedef errval_t (*cleanq_destroy_t)(struct cleanq *q);



/*
 * ================================================================================================
 * CleanQ Struct Definition
 * ================================================================================================
 */


struct cleanq
{
    ///< regio pool management
    struct region_pool *pool;

    ///< backend functions
    struct {
        ///< region registration()
        cleanq_register_t reg;

        ///< region deregistration()
        cleanq_deregister_t dereg;

        ///< queue control()
        cleanq_control_t ctrl;

        ///< queue notify()
        cleanq_notify_t notify;

        ///< buffer enqueue()
        cleanq_enqueue_t enq;

        ///< buffer dequeue()
        cleanq_dequeue_t deq;

        ///< queue destroy()
        cleanq_destroy_t destroy;
    } f;

    ///< use sate pointer
    void *state;

    ///< event callbacks
    struct {
        ///< event register()
        cleanq_register_callback_t reg;

        ///< event deregister()
        cleanq_deregister_callback_t dereg;
    } callbacks;
};


/*
 * ================================================================================================
 * CleanQ Initialization
 * ================================================================================================
 */


/**
 * @brief initializes the generic part of the queue
 *
 * @param q     the queue to initialize
 *
 * @returns CLEANQ_ERR_OK on sucecss or errof val on failure
 */
errval_t cleanq_init(struct cleanq *q);


/*
 * ================================================================================================
 * Adding and Removing Regions (Internal Functions)
 * ================================================================================================
 */


/**
 * @brief adds a region to the registered regions
 *
 * @param q     the queue to add the region to
 * @param cap   the memory region to add
 * @param rid   the region id of the added regin
 *
 */
errval_t cleanq_add_region(struct cleanq *q, struct capref cap, regionid_t rid);


/**
 * @brief removes a region from the queue
 *
 * @param q      the queue to remove the region from
 * @param rid    the region id to be removed
 *
 * @returns CLEANQ_ERR_OK on success, error value on failure
 */
errval_t cleanq_remove_region(struct cleanq *q, regionid_t rid);

#endif /* CLEANQ_BACKEND_H_ */
