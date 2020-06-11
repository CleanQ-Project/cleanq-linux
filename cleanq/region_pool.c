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
#include <stdio.h>
#include <time.h>
#include <slab.h>

#include "region_pool.h"
#include "dqi_debug.h"


///< defines the initial pool size
#define INIT_POOL_SIZE 16


/*
 * ================================================================================================
 * Type Definitions
 * ================================================================================================
 */


///< the region pool type
struct region_pool
{
    ///< IDs are inserted and may have to increase size at some point
    uint16_t size;

    ///< number of regions in pool
    uint16_t num_regions;

    ///< random offset where regions ids start from
    uint64_t region_offset;

    ///< if we have to serach for a slot, need an offset
    uint16_t last_offset;

    ///< region slab allocator
    struct slab_allocator region_alloc;

    ///< structure to store regions
    struct region **pool;
};


///< defines a region
struct region
{
    ///< ID of the region
    regionid_t id;

    ///< base address of the region
    uint64_t base_addr;

    ///< memory handle backing this memory region
    struct capref *cap;

    ///< Lenght of the memory region in bytes
    size_t len;
};




/**
 * @brief initialized a pool of regions
 *
 * @param pool          Return pointer to the region pool
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t region_pool_init(struct region_pool **pool)
{
    // Allocate pool struct itself including pointers to region
    (*pool) = (struct region_pool *)calloc(1, sizeof(struct region_pool));
    if (*pool == NULL) {
        DQI_DEBUG_REGION("Allocationg inital pool failed \n");
        return CLEANQ_ERR_MALLOC_FAIL;
    }

    (*pool)->num_regions = 0;

    srand(time(NULL));

    // Initialize region id offset
    (*pool)->region_offset = (rand() >> 12);
    (*pool)->size = INIT_POOL_SIZE;

    (*pool)->pool = (struct region **)calloc(INIT_POOL_SIZE, sizeof(struct region *));
    if ((*pool)->pool == NULL) {
        free(*pool);
        DQI_DEBUG_REGION("Allocationg inital pool failed \n");
        return CLEANQ_ERR_MALLOC_FAIL;
    }

    slab_init(&(*pool)->region_alloc, sizeof(struct region), slab_default_refill);

    DQI_DEBUG_REGION("Init region pool size=%d addr=%p\n", INIT_POOL_SIZE, *pool);
    return CLEANQ_ERR_OK;
}

/**

 * @brief freeing region pool
 *
 * @param pool          The region pool to free
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t region_pool_destroy(struct region_pool *pool)
{
    errval_t err;
    struct capref cap;
    // Check if there are any regions left
    if (pool->num_regions == 0) {
        free(pool->pool);
        free(pool);
        return CLEANQ_ERR_OK;
    } else {
        // There are regions left -> remove them
        for (int i = 0; i < pool->size; i++) {
            if ((void *)pool->pool[i] != NULL) {
                err = region_pool_remove_region(pool, pool->pool[i]->id, &cap);
                if (err_is_fail(err)) {
                    printf("Region pool has regions that are still used,"
                           " can not free them \n");
                    return err;
                }
            }
        }
        free(pool->pool);
        free(pool);
    }

    return CLEANQ_ERR_OK;
}

/**
 * @brief increase the region pool size by a factor of 2
 *
 * @param pool       the regin pool that has not enough region slots
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */


static errval_t region_pool_grow(struct region_pool *pool)
{
    struct region **tmp;

    uint16_t new_size = (pool->size) * 2;
    // Allocate new pool twice the size
    tmp = (struct region **)calloc(new_size, sizeof(struct region *));
    if (tmp == NULL) {
        DQI_DEBUG_REGION("Allocationg larger pool failed \n");
        return CLEANQ_ERR_MALLOC_FAIL;
    }

    // Copy all the pointers
    for (int i = 0; i < new_size; i++) {
        tmp[i] = NULL;
    }

    struct region *region;
    for (int i = 0; i < pool->size; i++) {
        region = pool->pool[i];
        uint16_t index = region->id & (new_size - 1);
        tmp[index] = pool->pool[i];
    }

    free(pool->pool);

    pool->pool = tmp;
    pool->size = new_size;
    pool->last_offset = 0;

    return CLEANQ_ERR_OK;
}

/**
 * @brief add a memory region to the region pool
 *
 * @param pool          The pool to add the region to
 * @param cap           The cap of the region
 * @param region_id     Return pointer to the region id
 *                      that is assigned by the pool
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t region_pool_add_region(struct region_pool *pool, struct capref cap, regionid_t *region_id)
{
    errval_t err = CLEANQ_ERR_OK;
    struct region *region;

    // for now just loop over all entries
    for (int i = 0; i < pool->size; i++) {
        struct region *tmp;
        tmp = pool->pool[i];

        if (tmp == NULL) {
            continue;
        }

        // check if region is already registered
        if (tmp->base_addr == cap.paddr) {
            return CLEANQ_ERR_INVALID_REGION_ARGS;
        }

        /* if region if entierly before other region or
           entierly after region, otherwise there is an overlap
         */
        if (!((cap.paddr + cap.len <= tmp->base_addr) || (tmp->base_addr + tmp->len <= cap.paddr))) {
            return CLEANQ_ERR_INVALID_REGION_ARGS;
        }
    }


    // Check if pool size is large enough
    if (!(pool->num_regions < pool->size)) {
        DQI_DEBUG_REGION("Increasing pool size to %d \n", pool->size * 2);
        err = region_pool_grow(pool);
        if (err_is_fail(err)) {
            DQI_DEBUG_REGION("Increasing pool size failed\n");
            return err;
        }
    }

    pool->num_regions++;
    uint16_t offset = pool->last_offset;
    uint16_t index = 0;


    // find slot
    while (true) {
        index = (pool->region_offset + pool->num_regions + offset) & (pool->size - 1);
        DQI_DEBUG_REGION("Trying insert index %d \n", index);
        if (pool->pool[index] == NULL) {
            break;
        } else {
            offset++;
        }
    }

    pool->last_offset = offset;

    region = (struct region *)slab_alloc(&pool->region_alloc);
    if (region == NULL) {
        return CLEANQ_ERR_MALLOC_FAIL;
    }

    region->id = pool->region_offset + pool->num_regions + offset;
    region->cap = &cap;
    region->base_addr = cap.paddr;
    region->len = cap.len;

    // insert into pool
    pool->pool[region->id & (pool->size - 1)] = region;
    *region_id = region->id;
    DQI_DEBUG_REGION("Inserting region into pool at %d \n", region->id % pool->size);
    return err;
}

/**
 * @brief add a memory region to the region pool using an already
 *        existing id
 *
 * @param pool          The pool to add the region to
 * @param cap           The cap of the region
 * @param region_id     The region id to add to the pool
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t region_pool_add_region_with_id(struct region_pool *pool, struct capref cap,
                                        regionid_t region_id)
{
    errval_t err;
    // Check if pool size is large enough
    if (!(pool->num_regions < pool->size)) {
        DQI_DEBUG_REGION("Increasing pool size to %d \n", pool->size * 2);
        err = region_pool_grow(pool);
        if (err_is_fail(err)) {
            DQI_DEBUG_REGION("Increasing pool size failed\n");
            return err;
        }
    }

    struct region *region = pool->pool[region_id & (pool->size - 1)];
    if (region != NULL) {
        return CLEANQ_ERR_INVALID_REGION_ID;
    } else {
        region = (struct region *)slab_alloc(&pool->region_alloc);
        if (region == NULL) {
            return CLEANQ_ERR_MALLOC_FAIL;
        }

        region->id = region_id;
        region->cap = &cap;
        region->base_addr = cap.paddr;
        region->len = cap.len;

        pool->pool[region_id & (pool->size - 1)] = region;
    }

    pool->num_regions++;
    return CLEANQ_ERR_OK;
}

/**
 * @brief remove a memory region from the region pool
 *
 * @param pool          The pool to remove the region from
 * @param region_id     The id of the region to remove
 * @param cap           Return pointer to the cap of the removed region
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t region_pool_remove_region(struct region_pool *pool, regionid_t region_id,
                                   struct capref *cap)
{
    (void)(cap);
    // errval_t err;
    struct region *region;
    region = pool->pool[region_id & (pool->size - 1)];
    if (region == NULL) {
        return CLEANQ_ERR_INVALID_REGION_ID;
    }

    cap = region->cap;

    slab_free(&pool->region_alloc, region);
    pool->pool[region_id & (pool->size - 1)] = NULL;

    pool->num_regions--;
    return CLEANQ_ERR_OK;
}


/**
 * @brief get memory region from pool
 *
 * @param pool          The pool to get the region from
 * @param region_id     The id of the region to get
 * @param region        Return pointer to the region
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
/*
static errval_t region_pool_get_region(struct region_pool* pool,
                                       regionid_t region_id,
                                       struct region** region)
{
    *region = pool->pool[region_id % pool->size];
    if (*region == NULL) {
        return CLEANQ_ERR_INVALID_REGION_ID;
    }

    return CLEANQ_ERR_OK;
}
*/

/**
 * @brief check if buffer is valid
 *
 * @param pool          The pool to get the region from
 * @param region_id     The id of the region
 * @param offset        offset into the region
 * @param length        length of the buffer
 * @param valid_data    offset into the buffer
 * @param valid_length  length of the valid_data
 *
 * @returns true if the buffer is valid otherwise false
 */
bool region_pool_buffer_check_bounds(struct region_pool *pool, regionid_t region_id,
                                     genoffset_t offset, genoffset_t length,
                                     genoffset_t valid_data, genoffset_t valid_length)
{
    struct region *region;
    region = pool->pool[region_id & (pool->size - 1)];
    if (region == NULL) {
        return false;
    }

    // check validity of buffer within region
    // and check validity of valid data values
    if ((length + offset > region->len) || (valid_data + valid_length > length)) {
        return false;
    }

    return true;
}
