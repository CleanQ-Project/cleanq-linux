/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached license file.
 * if you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <linux/errno.h>

#include <slab.h>

#define DEFAULT_REFIL_SIZE (16 * 1024)


// size of block header
#define SLAB_BLOCK_HDRSIZE (sizeof(void *))
// should be able to fit the header into the block
#define SLAB_REAL_BLOCKSIZE(blocksize)                                                            \
    (((blocksize) > SLAB_BLOCK_HDRSIZE) ? (blocksize) : SLAB_BLOCK_HDRSIZE)


///< this is the definition of block in the free list
struct block_head
{
    struct block_head *next;  ///< Pointer to next block in free list
};

///< this represents a slab head, a collection of slab blocks
struct slab_head
{
    ///< Next slab in the allocator
    struct slab_head *next;

    ///< Count of total and free blocks in this slab
    uint32_t total, free;

    ///< Pointer to free block list
    struct block_head *blocks;
};


/**
 * @brief initializes a slab allocator
 *
 * @param slabs         the slab allocator to be initialized
 * @param blocksize     the blocksize of the slabs
 * @param refill_func   function to be called when the slabs running out of memory
 */
void slab_init(struct slab_allocator *slabs, size_t blocksize, slab_refill_func_t refill_func)
{
    slabs->slabs = NULL;
    slabs->blocksize = SLAB_REAL_BLOCKSIZE(blocksize);
    slabs->refill_func = refill_func;
}


/**
 * @brief allocates a new block from the slab allocator
 *
 * @param slabs     the slab allocator to allocate from
 *
 * @returns pointer ot new memory or NULL
 */
void *slab_alloc(struct slab_allocator *slabs)
{
    /* find a slab with free blocks */
    struct slab_head *sh;
    for (sh = slabs->slabs; sh != NULL && sh->free == 0; sh = sh->next)
        ;

    if (sh == NULL) {
        /* out of memory. try refill function if we have one */
        if (!slabs->refill_func) {
            return NULL;
        } else {
            if (slabs->refill_func(slabs)) {
                printf("slab refill_func failed\n");
                return NULL;
            }
            for (sh = slabs->slabs; sh != NULL && sh->free == 0; sh = sh->next)
                ;
            if (sh == NULL) {
                return NULL;
            }
        }
    }

    /* dequeue top block from freelist */
    struct block_head *bh = sh->blocks;
    assert(bh != NULL);
    sh->blocks = bh->next;
    sh->free--;

    memset(bh, 0, slabs->blocksize);

    return bh;
}


/**
 * @brief frees a previously allocated block
 *
 * @param slabs     the slab allocator
 * @param block     the block to be freed
 *
 * NOTE: the block must be allocated from this allocator
 */
void slab_free(struct slab_allocator *slabs, void *block)
{
    if (block == NULL) {
        return;
    }

    struct block_head *bh = (struct block_head *)block;

    /* find matching slab */
    struct slab_head *sh;
    size_t blocksize = slabs->blocksize;
    for (sh = slabs->slabs; sh != NULL; sh = sh->next) {
        /* check if block falls inside this slab */
        uintptr_t slab_limit = (uintptr_t)sh + sizeof(struct slab_head) + blocksize * sh->total;
        if ((uintptr_t)bh > (uintptr_t)sh && (uintptr_t)bh < slab_limit) {
            break;
        }
    }
    assert(sh != NULL);

    /* re-enqueue in slab's free list */
    bh->next = sh->blocks;
    sh->blocks = bh;
    sh->free++;
    assert(sh->free <= sh->total);
}


/**
 * @brief default refill function for the slab allocator
 *
 * @param slabs     the slab allocator to be refilled
 *
 * @returns 0 on success, -ENOMEM on failure
 */
int slab_default_refill(struct slab_allocator *slabs)
{
    void *buf = malloc(DEFAULT_REFIL_SIZE);
    if (!buf) {
        return -ENOMEM;
    }

    slab_grow(slabs, buf, DEFAULT_REFIL_SIZE);

    return 0;
}


/**
 * @brief adds backing memory to the slab allocator
 *
 * @param slabs     the slab allocator to refill
 * @param buf       pointer to backing memory
 * @param buflen    size of the backing memory in bytes
 */
void slab_grow(struct slab_allocator *slabs, void *buf, size_t buflen)
{
    assert(buflen > sizeof(struct slab_head));

    /* setup slab_head structure at top of buffer */
    struct slab_head *head = (struct slab_head *)buf;
    buflen -= sizeof(struct slab_head);
    buf = (char *)buf + sizeof(struct slab_head);

    /* calculate number of blocks in buffer */
    size_t blocksize = slabs->blocksize;
    assert(buflen / blocksize <= UINT32_MAX);
    head->free = head->total = buflen / blocksize;
    assert(head->total > 0);

    /* enqueue blocks in freelist */
    struct block_head *bh = head->blocks = (struct block_head *)buf;
    for (uint32_t i = head->total; i > 1; i--) {
        buf = (char *)buf + blocksize;
        bh->next = (struct block_head *)buf;
        bh = (struct block_head *)buf;
    }
    bh->next = NULL;

    /* enqueue slab in list of slabs */
    head->next = slabs->slabs;
    slabs->slabs = head;
}
