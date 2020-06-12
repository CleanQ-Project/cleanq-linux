/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <cleanq/cleanq.h>

#include <cleanq_backend.h>
#include <region_pool.h>


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
errval_t cleanq_init(struct cleanq *q)
{
    return region_pool_init(&(q->pool));
}


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
errval_t cleanq_add_region(struct cleanq *q, struct capref cap, regionid_t rid)
{
    return region_pool_add_region_with_id(q->pool, cap, rid);
}


/**
 * @brief removes a region from the queue
 *
 * @param q      the queue to remove the region from
 * @param rid    the region id to be removed
 *
 * @returns CLEANQ_ERR_OK on success, error value on failure
 */
errval_t cleanq_remove_region(struct cleanq *q, regionid_t rid)
{
    struct capref cap;
    return region_pool_remove_region(q->pool, rid, &cap);
}
