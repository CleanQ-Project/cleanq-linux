/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached license file.
 * if you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
 */

#ifndef SLAB_ALLOCATOR_H_
#define SLAB_ALLOCATOR_H_

#include <stdint.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

///< forward delaration
struct slab_allocator;
struct slab_head;

///< this is the refill function
typedef int (*slab_refill_func_t)(struct slab_allocator *slabs);

///< this represents a slab allocator
struct slab_allocator
{
    struct slab_head *slabs;         ///< Pointer to list of slabs
    size_t blocksize;                ///< Size of blocks managed by this allocator
    slab_refill_func_t refill_func;  ///< Refill function
};


/**
 * @brief initializes a slab allocator
 *
 * @param slabs         the slab allocator to be initialized
 * @param blocksize     the blocksize of the slabs
 * @param refill_func   function to be called when the slabs running out of memory
 */
void slab_init(struct slab_allocator *slabs, size_t blocksize, slab_refill_func_t refill_func);


/**
 * @brief allocates a new block from the slab allocator
 *
 * @param slabs     the slab allocator to allocate from
 *
 * @returns pointer ot new memory or NULL
 */
void *slab_alloc(struct slab_allocator *slabs);


/**
 * @brief frees a previously allocated block
 *
 * @param slabs     the slab allocator
 * @param block     the block to be freed
 *
 * NOTE: the block must be allocated from this allocator
 */
void slab_free(struct slab_allocator *slabs, void *block);


/**
 * @brief default refill function for the slab allocator
 *
 * @param slabs     the slab allocator to be refilled
 *
 * @returns 0 on success, -ENOMEM on failure
 */
int slab_default_refill(struct slab_allocator *slabs);


/**
 * @brief adds backing memory to the slab allocator
 *
 * @param slabs     the slab allocator to refill
 * @param buf       pointer to backing memory
 * @param buflen    size of the backing memory in bytes
 */
void slab_grow(struct slab_allocator *slabs, void *buf, size_t buflen);


__END_DECLS

#endif  // SLAB_ALLOCATOR_H_
