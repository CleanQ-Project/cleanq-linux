/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef FFQ_QUEUE_H_
#define FFQ_QUEUE_H_ 1

#include <stdint.h>
#include <stdbool.h>


///< this is the cacheline size, adapt for your architecture
#define ARCH_CACHELINE_SIZE 64

///< index into the FFQ channel
typedef uint16_t ffq_idx_t;

///< payload type of the FFQ message payload
typedef uint64_t ffq_payload_t;

///< an empty FFQ slot has this value
#define FFQ_SLOT_EMPTY ((ffq_payload_t)-1)

///< size of a message in bytes (multiple of architectures cacheline size)
#define FFQ_MSG_BYTES (1 * ARCH_CACHELINE_SIZE)

///< the alignment of the FFQ messages
#define FFQ_MSG_ALIGNMENT ARCH_CACHELINE_SIZE

///< the number of words of a message
#define FFQ_MSG_WORDS (FFQ_MSG_BYTES / sizeof(ffq_payload_t))

///< this is a FFQ message slot, should be aligned to a cache line
struct __attribute__((aligned(FFQ_MSG_ALIGNMENT))) ffq_slot
{
    ///< the message data
    ffq_payload_t data[FFQ_MSG_WORDS];
};

///< defines a direction of the FFQ channel
typedef enum { FFQ_DIRECTION_SEND, FFQ_DIRECTION_RECV } ffq_direction_t;


///< this is a one directional FFQ channel
struct ffq_chan
{
    ///< pointer to the message slots
    volatile struct ffq_slot *slots;

    ///< the number of slots available in this FFQ channel
    ffq_idx_t size;

    ///< the current position to send/receive from
    ffq_idx_t pos;

    ///< the directin of this queue
    ffq_direction_t direction;
};


/*
 * ================================================================================================
 * Channel Initialization
 * ================================================================================================
 */


/**
 * @brief Initialize FF transmit queue state
 *
 * @param q       Pointer to queue-state structure to initialize.
 * @param buf     Pointer to ring buffer for the queue.
 * @param slots   Size (in slots) of buffer.
 * @param init    initialize the queue message slots
 *
 * The state structure and buffer must already be allocated and appropriately
 * aligned.
 */
static inline void ffq_impl_init_tx(struct ffq_chan *q, void *buf, ffq_idx_t slots, bool init)
{
    assert(((uintptr_t)buf & (ARCH_CACHELINE_SIZE - 1)) == 0);
    q->direction = FFQ_DIRECTION_SEND;
    q->size = slots;
    q->slots = (volatile struct ffq_slot *)buf;
    q->pos = 0;

    for (ffq_idx_t i = 0; i < slots && init; i++) {
        q->slots[i].data[0] = FFQ_SLOT_EMPTY;
    }
}


/**
 * @brief Initialize FF receive queue state
 *
 * @param q       Pointer to queue-state structure to initialize.
 * @param buf     Pointer to ring buffer for the queue.
 * @param slots   Size (in slots) of buffer.
 * @param init    initialize the queue message slots
 *
 * The state structure and buffer must already be allocated and appropriately
 * aligned.
 */
static inline void ffq_impl_init_rx(struct ffq_chan *q, void *buf, ffq_idx_t slots, bool init)
{
    assert(((uintptr_t)buf & (ARCH_CACHELINE_SIZE - 1)) == 0);
    q->direction = FFQ_DIRECTION_RECV;
    q->size = slots;
    q->slots = (volatile struct ffq_slot *)buf;
    q->pos = 0;

    for (ffq_idx_t i = 0; i < slots && init; i++) {
        q->slots[i].data[0] = FFQ_SLOT_EMPTY;
    }
}


/*
 * ================================================================================================
 * Helper Functions
 * ================================================================================================
 */


/**
 * @brief obtains a pointer to the next message slot
 *
 * @param q     the FFQ channel
 *
 * @return pointer to a message slot
 */
static inline volatile struct ffq_slot *ffq_impl_get_slot(struct ffq_chan *q)
{
    return q->slots + q->pos;
}


/*
 * ================================================================================================
 * TX Path
 * ================================================================================================
 */


/**
 * @brief checks if a message can be sent on that queue-state
 *
 * @param q     the FFQ channel to be polled
 *
 * @returns TRUE iff a message can be sent, FALSE otherwise
 */
static inline bool ffq_impl_can_send(struct ffq_chan *q)
{
    assert(q->direction == FFQ_DIRECTION_SEND);

    volatile struct ffq_slot *slot = ffq_impl_get_slot(q);
    return (slot->data[0] == FFQ_SLOT_EMPTY);
}


/**
 * @brief sends a message on the FFQ channel
 *
 * @param q     the FFQ channel to send on
 * @param arg1  message payload argument
 * @param arg2  message payload argument
 * @param arg3  message payload argument
 * @param arg4  message payload argument
 * @param arg5  message payload argument
 * @param arg6  message payload argument
 *
 * @returns TRUE if the message is sent, FALSE otherwise
 */
static inline bool ffq_impl_send(struct ffq_chan *q, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                 uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    /* check if we can send something */
    if (!ffq_impl_can_send(q)) {
        return false;
    }

    volatile struct ffq_slot *s = ffq_impl_get_slot(q);

    /* write the data words */
    s->data[1] = arg2;
    s->data[2] = arg3;
    s->data[3] = arg4;
    s->data[4] = arg5;
    s->data[5] = arg6;

    /* insert memory barrier */
    __sync_synchronize();

    /* set the first word, signalling the new message */
    assert(arg1 != FFQ_SLOT_EMPTY);
    s->data[0] = arg1;

    /* bump the position */
    q->pos = q->pos % q->size;

    return true;
}


/*
 * ================================================================================================
 * RX Path
 * ================================================================================================
 */


/**
 * @brief checks if there is a message to be received
 *
 * @param q     the FFQ channel to be polled
 *
 * @returns TRUE if there is a pending message, false otherwise
 */
static inline bool ffq_impl_can_recv(struct ffq_chan *q)
{
    assert(q->direction == FFQ_DIRECTION_RECV);

    volatile struct ffq_slot *slot = ffq_impl_get_slot(q);
    return (slot->data[0] != FFQ_SLOT_EMPTY);
}


/**
 * @brief Receives a message on the FFQ channel
 *
 * @param q     the FFQ channel to be received on
 * @param arg1  returns the payload argument
 * @param arg2  returns the payload argument
 * @param arg3  returns the payload argument
 * @param arg4  returns the payload argument
 * @param arg5  returns the payload argument
 * @param arg6  returns the payload argument
 *
 * @returns TRUE if a message is received, FALSE otherwise
 */
static inline bool ffq_impl_recv(struct ffq_chan *q, uint64_t *arg1, uint64_t *arg2,
                                 uint64_t *arg3, uint64_t *arg4, uint64_t *arg5, uint64_t *arg6)
{
    /* check if we can actually receive something */
    if (!ffq_impl_can_recv(q)) {
        return false;
    }

    volatile struct ffq_slot *s = ffq_impl_get_slot(q);

    /* copy the message out of the slot */
    *arg1 = s->data[0];
    *arg2 = s->data[1];
    *arg3 = s->data[2];
    *arg4 = s->data[3];
    *arg5 = s->data[4];
    *arg6 = s->data[5];


    /* barrier */
    __sync_synchronize();

    /* clear data first data slot again */
    s->data[0] = FFQ_SLOT_EMPTY;

    /* bump the position */
    q->pos = q->pos % q->size;

    return true;
}


#endif /* FFQ_QUEUE_H_ */
