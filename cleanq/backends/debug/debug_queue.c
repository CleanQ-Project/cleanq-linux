/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sched.h>
#include <assert.h>

#include <cleanq.h>
#include <cleanq_backend.h>
#include <backends/debug_queue.h>
#include <slab.h>


//#define DQ_ENABLE_HIST
#define HIST_SIZE 128
#define MAX_STR_SIZE 128


//#define DEBUG_ENABLED 1

/*****************************************************************
 * Debug printer:
 *****************************************************************/
extern char *__progname;

#if defined(DEBUG_ENABLED)
#    define DEBUG(x...)                                                                           \
        do {                                                                                      \
            printf("DEBUG: %s.%d:%s:%d: ", __progname, sched_getcpu(), __func__, __LINE__);       \
            printf(x);                                                                            \
        } while (0)

#else
#    define DEBUG(x...) ((void)0)
#endif


/*
 * This is a debugging interface for the device queue interface that
 * can be used with any existing queue. It can be stacked on top
 * to check for non valid buffer enqueues/deqeues that might happen.
 * A not valid enqueue of a buffer is when the endpoint that enqueues
 * the buffer does not own the buffer.
 *
 * We keep track of the owned buffers as a list of regions which each
 * contains a list of memory chunks.
 * Each chunk specifies a offset within the region and its length.
 *
 * When a region is registered, we add one memory chunk that describes
 * the whole region i.e. offset=0 length= length of region
 *
 * If a a buffer is enqueued, it has to be contained in one of these
 * memory chunks. The memory chunk is then altered according how
 * the buffer is contained in the chunk. If it is at the beginning or
 * end of the chunk, the offset/length of the chunk is changed accordingly
 * If the buffer is in the middle of the chunk, we split the memory chunk
 * into two new memory chunks that do not contain the buffer.
 *
 * If a buffer is dequeued the buffer is added to the existing memory
 * chunks if possible, otherwise a new memory chunk is added to the
 * list of chunks. If a buffer is dequeued that is in between two
 * memory chunks, the memory chunks are merged to one big chunk.
 * We might fail to find the region id in our list of regions. In this
 * case we add the region with the deqeued offset+length as a size.
 * We can be sure that this region exists since the cleanq library itself
 * does these checks if the region is known to the endpoint. This simply
 * means the debugging queue on top of the other queue does not have a
 * consistant view of the registered regions (but the queue below does)
 *
 * When a region is deregistered, the list of chunks has to only
 * contain a single chunk that descirbes the whole region. Otherwise
 * the call will fail since some of the buffers are still in use.
 *
 */

///< represents a memory element in a doubly linked list
struct memory_ele
{
    ///< the offset
    genoffset_t offset;

    ///< the length
    genoffset_t length;

    ///< the next memory element
    struct memory_ele *next;

    ///< the previous element
    struct memory_ele *prev;
};

///< represents a memory list
struct memory_list
{
    ///< the region id
    regionid_t rid;

    ///< the length of the region
    genoffset_t length;

    ///< this is a region the other side registered
    bool not_consistent;

    ///< the buffer elements
    struct memory_ele *buffers;

    ///< next element in the list
    struct memory_list *next;
};


///< represents an operation
struct operation
{
    ///< operation name
    char str[MAX_STR_SIZE];

    ///< the offset of the buffer
    genoffset_t offset;

    ///< the lenght of the buffer
    genoffset_t length;
};


///< defines a debug queue
struct cleanq_debugq
{
    ///< this is the generic part of the queue
    struct cleanq my_q;

    ///< this is the other queue, the debug queue wraps
    struct cleanq *q;

    ///< list of regions to track
    struct memory_list *regions;  // list of lists

    ///< slab allocator for tracking ownership
    struct slab_allocator alloc;

    ///< slab allocator for the lists
    struct slab_allocator alloc_list;


#ifdef DQ_ENABLE_HIST
    uint16_t hist_head;
    struct operation history[HIST_SIZE];
#endif
};

static void dump_list(struct memory_list *region)
{
    struct memory_ele *ele = region->buffers;
    int index = 0;
    printf("================================================ \n");
    while (ele != NULL) {
        printf("Idx=%d offset=%lu length=%lu", index, ele->offset, ele->length);
        if (ele->prev != NULL) {
            printf(" prev->offset=%lu prev->length=%lu", ele->prev->offset, ele->prev->length);
        }
        printf(" \n");
        ele = ele->next;
        index++;
    }
    printf("================================================ \n");
}

#ifdef DQ_ENABLE_HIST
static void add_to_history(struct cleanq_debugq *q, genoffset_t offset, genoffset_t length, char *s)
{
    q->history[q->hist_head].offset = offset;
    q->history[q->hist_head].length = length;
    strncpy(q->history[q->hist_head].str, s, MAX_STR_SIZE);
    q->hist_head = (q->hist_head + 1) % HIST_SIZE;
}

static void dump_history(struct cleanq_debugq *q)
{
    for (int i = 0; i < HIST_SIZE; i++) {
        printf("offset=%lu length=%lu %s\n", q->history[q->hist_head].offset,
               q->history[q->hist_head].length, q->history[q->hist_head].str);

        q->hist_head = (q->hist_head + 1) % HIST_SIZE;
    }
}
#endif /* DQ_ENABLE_HIST */


// is b1 in bounds of b2?
static bool buffer_in_bounds(genoffset_t offset_b1, genoffset_t len_b1, genoffset_t offset_b2,
                             genoffset_t len_b2)
{
    return (offset_b1 >= offset_b2) && (len_b1 <= len_b2)
           && ((offset_b1 + len_b1) <= offset_b2 + len_b2);
}

// assumes that the buffer described by offset and length is contained
// in the buffer that is given as a struct
static void remove_split_buffer(struct cleanq_debugq *que, struct memory_list *region,
                                struct memory_ele *buffer, genoffset_t offset, genoffset_t length)
{
    // split the buffer
    // insert half before the buffer

    DEBUG("enqueue offset=%" PRIu64 " length=%" PRIu64 " buf->offset=%lu "
          "buf->length %lu \n",
          offset, length, buffer->offset, buffer->length);

    // check if buffer at beginning of region
    if (buffer->offset == offset) {
        buffer->offset += length;
        buffer->length -= length;

        if (buffer->length == 0) {
#ifdef DQ_ENABLE_HIST
            add_to_history(que, offset, length, "enq cut of beginning remove");
#endif
            DEBUG("enqueue remove buffer from list\n");
            // remove
            if (buffer->prev != NULL) {
                buffer->prev->next = buffer->next;
            } else {
                region->buffers = buffer->next;
            }

            if (buffer->next != NULL) {
                buffer->next->prev = buffer->prev;
            }
            slab_free(&que->alloc, buffer);
        } else {
#ifdef DQ_ENABLE_HIST
            add_to_history(que, offset, length, "enq cut of beginning");
#endif
        }

        DEBUG("enqueue first cut off begining results in offset=%" PRIu64 " "
              "length=%" PRIu64 "\n",
              buffer->offset, buffer->length);
        return;
    }

    // check if buffer at end of region
    if ((buffer->offset + buffer->length) == (offset + length)) {
        buffer->length -= length;

        if (buffer->length == 0) {
#ifdef DQ_ENABLE_HIST
            add_to_history(que, offset, length, "enq cut of end remove");
#endif
            // remove
            if (buffer->prev != NULL) {
                buffer->prev = buffer->next;
            }

            if (buffer->next != NULL) {
                buffer->next->prev = buffer->prev;
            }
            slab_free(&que->alloc, buffer);
        } else {
#ifdef DQ_ENABLE_HIST
            add_to_history(que, offset, length, "enq cut of end");
#endif
        }

        DEBUG("enqueue first cut off end results in offset=%" PRIu64 " "
              "length=%" PRIu64 "\n",
              buffer->offset, buffer->length);
        return;
    }

    // now if this did not work need to split buffer that contains the
    // enqueued buffer into two buffers (might also result only in one)

    // inset half before buffer
    genoffset_t old_len = buffer->length;

    buffer->length = offset - buffer->offset;

    struct memory_ele *after = NULL;
    after = (struct memory_ele *)slab_alloc(&que->alloc);
    assert(after != NULL);

    memset(after, 0, sizeof(*after));
    after->offset = buffer->offset + buffer->length + length;
    after->length = old_len - buffer->length - length;

    // insert after buffer
    after->prev = buffer;
    after->next = buffer->next;

    if (buffer->next != NULL) {
        buffer->next->prev = after;
    }

    buffer->next = after;

#ifdef DQ_ENABLE_HIST
    add_to_history(que, offset, length, "enq split buffer");
#endif

    DEBUG("Split buffer length=%lu to "
          "offset=%" PRIu64 " length=%" PRIu64 " and "
          "offset=%lu length=%lu \n",
          old_len, buffer->offset, buffer->length, after->offset, after->length);
}

/*
 * Inserts a buffer either before or after an existing buffer into the queue
 * Checks for merges of prev/next buffer in the queue
 */
static void insert_merge_buffer(struct cleanq_debugq *que, struct memory_list *region,
                                struct memory_ele *buffer, genoffset_t offset, genoffset_t length)
{
    assert(buffer != NULL);
    assert(region != NULL);

    if (offset >= buffer->offset + buffer->length) {  // insert after
        // buffer is on lower boundary
        //
        if (buffer->offset + length == offset) {
            buffer->length += length;
            DEBUG("dequeue merge after "
                  "offset=%" PRIu64 " length=%" PRIu64 " to offset=%" PRIu64 " "
                  "length=%" PRIu64 "\n",
                  offset, length, buffer->offset, buffer->length);
            // check other boundary for merge
            if (buffer->next != NULL && (buffer->offset + buffer->length == buffer->next->offset)) {
                buffer->length += buffer->next->length;
                struct memory_ele *next = buffer->next;
                if (buffer->next->next != NULL) {
                    buffer->next = buffer->next->next;
                    buffer->next->next->prev = buffer;
                }

                DEBUG("dequeue merge after more offset=%" PRIu64 " "
                      "length=%" PRIu64 " to offset=%" PRIu64 " length=%" PRIu64 " \n ",
                      next->offset, next->length, buffer->offset, buffer->length);
#ifdef DQ_ENABLE_HIST
                add_to_history(que, offset, length,
                               "deq insert after"
                               " on lower boundary and merge");
#endif
                slab_free(&que->alloc, next);
            } else {
#ifdef DQ_ENABLE_HIST
                add_to_history(que, offset, length, "deq insert after on lower boundary");
#endif
            }
        } else {
            // check higher boundary
            if (buffer->next != NULL && buffer->next->offset == offset + length) {
                buffer->next->offset = offset;
                buffer->next->length += length;

                DEBUG("dequeue merge after more offset=%" PRIu64 " "
                      "length=%" PRIu64 " to offset=%" PRIu64 " length=%" PRIu64 " \n ",
                      offset, length, buffer->next->offset, buffer->next->length);

#ifdef DQ_ENABLE_HIST
                add_to_history(que, offset, length,
                               "deq insert after"
                               " on higer boundary");
#endif
            } else {
                // buffer->next can be null and the newly inserted buffer
                // is the inserted at the end -> check boundary
                if (buffer->next == NULL && buffer->offset + buffer->length == offset) {
                    buffer->length += length;

#ifdef DQ_ENABLE_HIST
                    add_to_history(que, offset, length,
                                   "deq insert after"
                                   " on higer boundary end");
#endif
                    DEBUG("dequeue insert after merged offset=%" PRIu64 " "
                          "length=%" PRIu64 " "
                          "to offset=%" PRIu64 " length=%" PRIu64 " \n",
                          offset, length, buffer->offset, buffer->length);
                } else {
                    // insert in between
                    struct memory_ele *ele = (struct memory_ele *)slab_alloc(&que->alloc);
                    assert(ele != NULL);

                    ele->offset = offset;
                    ele->length = length;
                    ele->next = buffer->next;
                    ele->prev = buffer;

                    if (buffer->next != NULL) {
                        buffer->next->prev = ele;
                    }

                    buffer->next = ele;

#ifdef DQ_ENABLE_HIST
                    add_to_history(que, offset, length,
                                   "deq insert after"
                                   " in between");
#endif
                    DEBUG("dequeue insert after offset=%" PRIu64 " length=%" PRIu64 " "
                          "after offset=%" PRIu64 " length=%" PRIu64 " \n",
                          offset, length, buffer->offset, buffer->length);
                }
            }
        }
    } else {  // insert before buffer
        // buffer is on lower boundary
        if (buffer->offset == offset + length) {
            buffer->length += length;
            buffer->offset = offset;

            // check other boundary
            if (buffer->prev != NULL
                && (buffer->prev->offset + buffer->prev->length == buffer->offset)) {
                struct memory_ele *prev = buffer->prev;
                prev->length += buffer->length;
                prev->next = buffer->next;

                if (buffer->next != NULL) {
                    buffer->next->prev = prev;
                }

                slab_free(&que->alloc, buffer);

#ifdef DQ_ENABLE_HIST
                add_to_history(que, offset, length,
                               "deq insert buffer"
                               " before lower boundary merge");
#endif
                DEBUG("dequeue merge before more offset=%" PRIu64 " "
                      "length=%" PRIu64 " to offset=%" PRIu64 " length=%" PRIu64 " \n ",
                      offset, length, prev->offset, prev->length);
            } else {
#ifdef DQ_ENABLE_HIST
                add_to_history(que, offset, length,
                               "deq insert buffer"
                               " before lower boundary");
#endif
            }
        } else {
            // check lower boundary
            if (buffer->prev != NULL && (buffer->prev->offset + buffer->prev->length == offset)) {
                if (length == 0) {
                    printf("Length is 0 \n");
                    buffer->prev->length += 2048;
                }

                buffer->prev->length += length;

#ifdef DQ_ENABLE_HIST
                add_to_history(que, offset, length,
                               "deq insert buffer"
                               " before prev lower boundary merge");
#endif
                DEBUG("dequeue merge before more offset=%" PRIu64 " "
                      "length=%" PRIu64 " to offset=%" PRIu64 " length=%" PRIu64 " \n ",
                      offset, length, buffer->prev->offset, buffer->prev->length);
            } else {  // insert in between

                // insert in between
                struct memory_ele *ele = (struct memory_ele *)slab_alloc(&que->alloc);
                assert(ele != NULL);

                memset(ele, 0, sizeof(*ele));
                ele->offset = offset;
                ele->length = length;
                ele->next = buffer;
                ele->prev = buffer->prev;

                if (buffer->prev != NULL) {
                    buffer->prev->next = ele;
                } else {
                    region->buffers = ele;
                }

                buffer->prev = ele;

#ifdef DQ_ENABLE_HIST
                add_to_history(que, offset, length,
                               "deq insert buffer"
                               " before in between");
#endif
                DEBUG("dequeue insert before offset=%" PRIu64 " length=%" PRIu64 " "
                      "next is offset=%" PRIu64 " length=%" PRIu64 " \n",
                      offset, length, buffer->offset, buffer->length);
            }
        }
    }
}


static errval_t find_region(struct cleanq_debugq *que, struct memory_list **list, regionid_t rid)
{
    // find region
    struct memory_list *region = que->regions;

    while (region != NULL) {
        if (region->rid == rid) {
            break;
        }
        region = region->next;
    }

    // check if we found the region
    if (region == NULL) {
        return CLEANQ_ERR_INVALID_REGION_ID;
    }

    *list = region;
    return CLEANQ_ERR_OK;
}


/*
 * ================================================================================================
 * Datapath functions
 * ================================================================================================
 */


/**
 * @brief Enqueue a descriptor (as seperate fields) into the descriptor queue
 *
 * @param q                     The descriptor queue
 * @param region_id             Region id of the enqueued buffer
 * @param offset                Offset into the region where the buffer resides
 * @param length                Length of the buffer
 * @param valid_data            Offset into the region where the valid data of the buffer resides
 * @param valid_length          Length of the valid data of the buffer
 * @param misc_flags            Miscellaneous flags
 *
 * @returns error if queue is full or CLEANQ_ERR_OK on success
 */
static errval_t debug_enqueue(struct cleanq *q, regionid_t rid, genoffset_t offset,
                              genoffset_t length, genoffset_t valid_data, genoffset_t valid_length,
                              uint64_t flags)
{
    assert(length > 0);
    DEBUG("enqueue offset %" PRIu64 " \n", offset);
    errval_t err;
    struct cleanq_debugq *que = (struct cleanq_debugq *)q;

    // find region
    struct memory_list *region = NULL;

    err = find_region(que, &region, rid);
    if (err_is_fail(err)) {
        return err;
    }

    // check_consistency(region);

    // find the buffer
    struct memory_ele *buffer = region->buffers;

    if (region->buffers == NULL) {
        return CLEANQ_ERR_BUFFER_ALREADY_IN_USE;
    }

    // the only buffer
    if (buffer->next == NULL) {
        if (buffer_in_bounds(offset, length, buffer->offset, buffer->length)) {
            err = que->q->f.enq(que->q, rid, offset, length, valid_data, valid_length, flags);
            if (err_is_fail(err)) {
                return err;
            }

            remove_split_buffer(que, region, buffer, offset, length);
            return CLEANQ_ERR_OK;
        } else {
            printf("Bounds check failed only buffer offset=%lu length=%lu "
                   " buf->offset=%lu buf->len=%lu\n",
                   offset, length, buffer->offset, buffer->length);
#ifdef DQ_ENABLE_HIST
            dump_history(que);
#endif
            dump_list(region);
            return CLEANQ_ERR_INVALID_BUFFER_ARGS;
        }
    }


    // more than one buffer
    while (buffer != NULL) {
        if (buffer_in_bounds(offset, length, buffer->offset, buffer->length)) {
            err = que->q->f.enq(que->q, rid, offset, length, valid_data, valid_length, flags);
            if (err_is_fail(err)) {
                return err;
            }

            remove_split_buffer(que, region, buffer, offset, length);
            return CLEANQ_ERR_OK;
        }
        buffer = buffer->next;
    }

    printf("Did not find region offset=%ld length=%ld \n", offset, length);
#ifdef DQ_ENABLE_HIST
    dump_history(que);
#endif
    dump_list(region);

    return CLEANQ_ERR_INVALID_BUFFER_ARGS;
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
static errval_t debug_dequeue(struct cleanq *q, regionid_t *rid, genoffset_t *offset,
                              genoffset_t *length, genoffset_t *valid_data,
                              genoffset_t *valid_length, uint64_t *flags)
{
    errval_t err;
    struct cleanq_debugq *que = (struct cleanq_debugq *)q;
    assert(que->q->f.deq != NULL);
    err = que->q->f.deq(que->q, rid, offset, length, valid_data, valid_length, flags);
    if (err_is_fail(err)) {
        return err;
    }
    DEBUG("dequeued offset=%lu \n", *offset);

    struct memory_list *region = NULL;

    err = find_region(que, &region, *rid);
    if (err_is_fail(err)) {
        // region ids are checked bythe cleanq library, if we do not find
        // the region id when dequeueing here we do not have a consistant
        // view of two endpoints
        //
        // Add region
        if (que->regions == NULL) {
            printf("Adding region frirst %lu len \n", *offset + *length);

            que->regions = (struct memory_list *)slab_alloc(&que->alloc_list);
            assert(que->regions != NULL);

            que->regions->rid = *rid;
            que->regions->not_consistent = true;
            // region is at least offset + length
            que->regions->length = *offset + *length;
            que->regions->next = NULL;
            // add the whole regions as a buffer
            que->regions->buffers = (struct memory_ele *)slab_alloc(&que->alloc);
            assert(que->regions->buffers != NULL);

            memset(que->regions->buffers, 0, sizeof(*que->regions->buffers));
            que->regions->buffers->offset = 0;
            que->regions->buffers->length = *offset + *length;
            que->regions->buffers->next = NULL;
            return CLEANQ_ERR_OK;
        }

        struct memory_list *ele = que->regions;
        while (ele->next != NULL) {
            ele = ele->next;
        }

        printf("Adding region second %lu len \n", *offset + *length);
        // add the reigon
        ele->next = (struct memory_list *)slab_alloc(&que->alloc_list);
        assert(ele->next != NULL);

        memset(que->regions->buffers, 0, sizeof(ele->next));
        ele = ele->next;

        ele->rid = *rid;
        ele->next = NULL;
        ele->not_consistent = true;
        ele->length = *offset + *length;
        // add the whole regions as a buffer
        ele->buffers = (struct memory_ele *)slab_alloc(&que->alloc);
        assert(ele->buffers != NULL);

        memset(ele->buffers, 0, sizeof(*ele->buffers));
        ele->buffers->offset = 0;
        ele->buffers->length = *offset + *length;
        ele->buffers->next = NULL;
        return CLEANQ_ERR_OK;
    }

    if (region->not_consistent) {
        if ((*offset + *length) > region->length) {
            region->length = *offset + *length;
        }
    }

    // check_consistency(region);

    // find the buffer
    struct memory_ele *buffer = region->buffers;
    if (buffer == NULL) {
        region->buffers = (struct memory_ele *)slab_alloc(&que->alloc);
        assert(region->buffers != NULL);

        region->buffers->offset = *offset;
        region->buffers->length = *length;
        region->buffers->next = NULL;
        region->buffers->prev = NULL;
        return CLEANQ_ERR_OK;
    }

    if (buffer->next == NULL) {
        if (!buffer_in_bounds(*offset, *length, buffer->offset, buffer->length)) {
            insert_merge_buffer(que, region, buffer, *offset, *length);
            return CLEANQ_ERR_OK;
        } else {
            return CLEANQ_ERR_BUFFER_NOT_IN_USE;
        }
    }


    while (buffer->next != NULL) {
        if (*offset >= buffer->offset) {
            buffer = buffer->next;
        } else {
            if (!buffer_in_bounds(*offset, *length, buffer->offset, buffer->length)) {
                insert_merge_buffer(que, region, buffer, *offset, *length);
                return CLEANQ_ERR_OK;
            } else {
                return CLEANQ_ERR_BUFFER_NOT_IN_USE;
            }
        }
    }

    // insert after the last buffer
    if (!buffer_in_bounds(*offset, *length, buffer->offset, buffer->length)) {
        insert_merge_buffer(que, region, buffer, *offset, *length);
        return CLEANQ_ERR_OK;
    }

    return CLEANQ_ERR_BUFFER_NOT_IN_USE;
}


/**
 * @brief Send a notification about new buffers on the queue
 *
 * @param q      The queue to call the operation on
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
static errval_t debug_notify(struct cleanq *q)
{
    DEBUG("notify \n");
    struct cleanq_debugq *que = (struct cleanq_debugq *)q;
    return que->q->f.notify(que->q);
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
static errval_t debug_control(struct cleanq *q, uint64_t cmd, uint64_t value, uint64_t *result)
{
    DEBUG("control \n");
    struct cleanq_debugq *que = (struct cleanq_debugq *)q;
    return que->q->f.ctrl(que->q, cmd, value, result);
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
static errval_t debug_register(struct cleanq *q, struct capref cap, regionid_t rid)
{
    errval_t err;
    struct cleanq_debugq *que = (struct cleanq_debugq *)q;
    DEBUG("Register \n");

    // queue of regions is empty
    if (que->regions == NULL) {
        // add the reigon
        err = que->q->f.reg(que->q, cap, rid);
        if (err_is_fail(err)) {
            return err;
        }

        que->regions = (struct memory_list *)slab_alloc(&que->alloc_list);
        assert(que->regions != NULL);

        que->regions->rid = rid;
        que->regions->length = cap.len;
        que->regions->not_consistent = false;
        que->regions->next = NULL;
        // add the whole regions as a buffer
        que->regions->buffers = (struct memory_ele *)slab_alloc(&que->alloc);
        assert(que->regions->buffers != NULL);

        memset(que->regions->buffers, 0, sizeof(*que->regions->buffers));
        que->regions->buffers->offset = 0;
        que->regions->buffers->length = cap.len;
        que->regions->buffers->next = NULL;
        DEBUG("Register rid=%" PRIu32 " size=%" PRIu64 " \n", rid, id.bytes);
        return CLEANQ_ERR_OK;
    }

    struct memory_list *ele = que->regions;
    while (ele->next != NULL) {
        ele = ele->next;
    }

    err = que->q->f.reg(que->q, cap, rid);
    if (err_is_fail(err)) {
        return err;
    }

    // add the reigon
    ele->next = (struct memory_list *)slab_alloc(&que->alloc_list);
    assert(ele->next != NULL);

    ele = ele->next;
    ele->rid = rid;
    ele->next = NULL;
    ele->length = cap.len;
    ele->not_consistent = false;
    // add the whole regions as a buffer
    ele->buffers = (struct memory_ele *)slab_alloc(&que->alloc);
    assert(ele->buffers != NULL);

    memset(ele->buffers, 0, sizeof(*ele->buffers));
    ele->buffers->offset = 0;
    ele->buffers->length = cap.len;
    ele->buffers->next = NULL;
    DEBUG("Register rid=%" PRIu32 " size=%" PRIu64 " \n", rid, id.bytes);

    return CLEANQ_ERR_OK;
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
static errval_t debug_deregister(struct cleanq *q, regionid_t rid)
{
    DEBUG("Deregister \n");
    struct cleanq_debugq *que = (struct cleanq_debugq *)q;
    errval_t err;

    struct memory_list *ele = que->regions;
    if (ele == NULL) {
        return CLEANQ_ERR_INVALID_REGION_ID;
    }

    // remove head
    if (ele->rid == rid) {
        // there should only be a single element in the list
        // i.e. the whole region
        if (ele->buffers->offset == 0 && ele->buffers->length == ele->length
            && ele->buffers->next == NULL) {
            err = que->q->f.dereg(que->q, rid);
            if (err_is_fail(err)) {
                return err;
            }
            que->regions = ele->next;

            DEBUG("removed region rid=%" PRIu32 " size=%" PRIu64 " \n", rid, ele->length);

            slab_free(&que->alloc, ele->buffers);
            slab_free(&que->alloc_list, ele);

            return CLEANQ_ERR_OK;
        } else {
            DEBUG("Destroy error rid=%d offset=%" PRIu64 " length=%" PRIu64 " "
                  "should be offset=0 length=%" PRIu64 "\n",
                  ele->rid, ele->buffers->offset, ele->buffers->length, ele->length);
            dump_list(ele);
            return CLEANQ_ERR_REGION_DESTROY;
        }
    }

    while (ele->next != NULL) {
        if (ele->next->rid == rid) {
            if (ele->next->buffers->offset == 0 && ele->next->buffers->length == ele->next->length
                && ele->next->buffers->next == NULL) {
                err = que->q->f.dereg(que->q, rid);
                if (err_is_fail(err)) {
                    return err;
                }
                // remove from queue
                struct memory_list *next = ele->next;
                ele->next = ele->next->next;

                DEBUG("removed region rid=%" PRIu32 " size=%" PRIu64 " \n", rid, next->length);

                slab_free(&que->alloc, next->buffers);
                slab_free(&que->alloc_list, next);

                return CLEANQ_ERR_OK;
            } else {
                DEBUG("Destroy error rid=%d offset=%" PRIu64 " length=%" PRIu64 " "
                      "should be offset=0 length=%" PRIu64 "\n",
                      ele->next->rid, ele->next->buffers->offset, ele->next->buffers->length,
                      ele->next->length);

                dump_list(ele);
                return CLEANQ_ERR_REGION_DESTROY;
            }
        } else {
            ele = ele->next;
        }
    }


    return CLEANQ_ERR_INVALID_REGION_ID;
}


/*
 * ================================================================================================
 * Queue Destruction and Creation
 * ================================================================================================
 */


/**
 * @brief Destroys a descriptor queue and frees its resources
 *
 * @param queue The descriptor queue
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
static errval_t debug_destroy(struct cleanq *cleanq)
{
    (void)(cleanq);
    // TODO cleanup
    return CLEANQ_ERR_OK;
}

/**
 * @brief initialized a the IPCQ backend
 *
 * @param q         Return pointer to the descriptor queue
 * @param name      Name of the memory use for sending messages
 * @param clear     Write 0 to memory
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_debugq_create(struct cleanq_debugq **q, struct cleanq *other_q)
{
    errval_t err;
    struct cleanq_debugq *que;
    que = (struct cleanq_debugq *)calloc(1, sizeof(struct cleanq_debugq));
    assert(que);

    slab_init(&que->alloc, sizeof(struct memory_ele), slab_default_refill);

    slab_init(&que->alloc_list, sizeof(struct memory_list), slab_default_refill);

    que->q = other_q;
    err = cleanq_init(&que->my_q);
    if (err_is_fail(err)) {
        return err;
    }

    que->my_q.f.reg = debug_register;
    que->my_q.f.dereg = debug_deregister;
    que->my_q.f.ctrl = debug_control;
    que->my_q.f.notify = debug_notify;
    que->my_q.f.enq = debug_enqueue;
    que->my_q.f.deq = debug_dequeue;
    que->my_q.f.destroy = debug_destroy;
    *q = que;
    return CLEANQ_ERR_OK;
}


/*
 * ================================================================================================
 * Debugging Functions
 * ================================================================================================
 */


/**
 * @brief dumps the information about a memory region
 *
 * @param q     the debug queue
 * @param rid   the memory region to be dumped
 */
void cleanq_debugq_dump_region(struct cleanq_debugq *que, regionid_t rid)
{
    errval_t err;
    // find region
    struct memory_list *region = NULL;

    err = find_region(que, &region, rid);
    if (err_is_fail(err)) {
        printf("did not find region to dump\n");
    }

    dump_list(region);
    return;
}


/**
 * @brief dumps the history of the debug queue
 *
 * @param q     the debug queue
 *
 */
void cleanq_debugq_dump_history(struct cleanq_debugq *q)
{
    (void)(q);
#ifdef DQ_ENABLE_HIST
    dump_history(q);
#endif
}


/**
 * @brief Adding region to debug queue. When using two endpoints
 *        Only the lowest layer is consistent with the regions
 *        To mache the debugging layer consistent, this method adds the region
 *        to the known regions so that the ckecks when dequeueing work
 *
 * @param q                     Return pointer to the descriptor queue
 * @param cap                   cap to the region
 * @param rid                  the regionid of the region
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_debugq_add_region(struct cleanq_debugq *q, struct capref cap, regionid_t rid)
{
    return cleanq_add_region((struct cleanq *)q, cap, rid);
}


/**
 * @brief Removing region from debug queue
 *
 * @param q                    Return pointer to the descriptor queue
 * @param rid                  the regionid of the region
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_debugq_remove_region(struct cleanq_debugq *q, regionid_t rid)
{
    return cleanq_remove_region((struct cleanq *)q, rid);
}
