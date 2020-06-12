/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>

#include <cleanq.h>
#include <cleanq_backend.h>
#include <backends/ipc_queue.h>


/*
 * ================================================================================================
 * Debugging Facility
 * ================================================================================================
 */

//#define IPCQ_DEBUG_ENABLED 1

extern char *__progname;

#if defined(IPCQ_DEBUG_ENABLED)
#    define IPCQ_DEBUG(x...)                                                                      \
        do {                                                                                      \
            printf("IPCQ:%s:%s:%d: ", __progname, __func__, __LINE__);                            \
            printf(x);                                                                            \
        } while (0)
#else
#    define IPCQ_DEBUG(x...) ((void)0)
#endif


/*
 * ================================================================================================
 * IPCQ Type Definitions
 * ================================================================================================
 */

///< this is the size of the one-directional queue in message slotes
#define IPCQ_DEFAULT_SIZE 64

///< this is the alignment of descriptors
#define IPCQ_DESCRIPTOR_ALIGNMENT 64

///< the size of a IPCQ message
#define IPCQ_MESSAGE_SIZE 64

///< size of a one-direction IPCQ channel
#define IPCQ_CHAN_SIZE (IPCQ_DEFAULT_SIZE * IPCQ_MESSAGE_SIZE)

///< total memory for the backing descriptor queues
#define IPCQ_MEM_SIZE (2 * IPCQ_CHAN_SIZE)


///< defines an IPC queue descriptor
struct __attribute__((aligned(IPCQ_DESCRIPTOR_ALIGNMENT))) ipcq_desc
{
    ///< sequence ID (flow control)
    uint64_t seq;

    ///< region ID
    regionid_t rid;

    ///< padding
    uint8_t pad[4];

    ////< offset into the memory region
    genoffset_t offset;

    ///< length of the buffer
    genoffset_t length;

    ///< start of valid data
    genoffset_t valid_data;

    ///< length of valid data
    genoffset_t valid_length;

    ///< the flags
    uint64_t flags;

    ///< command
    uint64_t cmd;
};

///< sequence numbers
union __attribute__((aligned(IPCQ_DESCRIPTOR_ALIGNMENT))) ipcq_seqnum
{
    ///< the squence number value
    volatile size_t value;

    ///< padding to IPCQ_MESSAGE_SIZE
    uint8_t pad[IPCQ_MESSAGE_SIZE];
};


struct cleanq_ipcq
{
    ///< general cleanq type
    struct cleanq q;

    ///< the name of this queue
    char *name;

    ///< the number of slots in the descriptor ring
    size_t slots;

    ///< receive descriptors
    struct ipcq_desc *rx_descs;

    ///< the receive sequence number for flow control
    uint64_t rx_seq;

    ///< the receive sequence acknowledgements
    union ipcq_seqnum *rx_seq_ack;

    ///< transmit descriptors
    struct ipcq_desc *tx_descs;

    ///< the transmit sequence number for flow control
    uint64_t tx_seq;

    ///< the transmit sequence acknowledgements
    union ipcq_seqnum *tx_seq_ack;

    ///< pointer to the backing memory for the rx/tx descriptors
    void *rxtx_mem;

    ///< the memory size of the backing memory
    size_t memsize;
};


/*
 * ================================================================================================
 * Special Command Messages
 * ================================================================================================
 */


#define IPCQ_CMD_REGISTER 1
#define IPCQ_CMD_DEREGISTER 2


static errval_t handle_register_command(struct cleanq_ipcq *q, struct capref cap, uint32_t rid)
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

static errval_t handle_register_decommand(struct cleanq_ipcq *q, regionid_t rid)
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
 * TX Path
 * ================================================================================================
 */


/**
 * @brief returns a pointer to the head descriptor
 *
 * @param q     the IPC queue
 *
 * @returns pointer to the head desc
 */
static inline struct ipcq_desc *ipcq_get_tx_desc_head(struct cleanq_ipcq *q)
{
    return &q->tx_descs[q->tx_seq % q->slots];
}


/**
 * @brief checks if the queue can be written
 *
 * @param q     the ipcq to check
 *
 * @returns TRUE if a message can be written, FALSE otherwise
 */
static inline bool ipcq_can_send(struct cleanq_ipcq *q)
{
    return (q->tx_seq - q->tx_seq_ack->value) < q->slots;
}


/**
 * @brief sends a message over the IPCQ
 *
 * @param q             the ipc queue
 * @param region_id     region identifier
 * @param offset        offset into the region
 * @param length        total length of the buffer
 * @param valid_data    offset of valid data
 * @param valid_length  length of valid data
 * @param misc_flags    flags to transmitt
 * @param cmd           the command
 *
 * @returns CLEANQ_ERR_OK on success, CLEANQ_ERR_QUEUE_FULL if queue was full
 */
static inline errval_t ipcq_enqueue_internal(struct cleanq_ipcq *q, regionid_t region_id,
                                             genoffset_t offset, genoffset_t length,
                                             genoffset_t valid_data, genoffset_t valid_length,
                                             uint64_t misc_flags, uint64_t cmd)
{
    assert(q);

    if (!ipcq_can_send(q)) {
        return CLEANQ_ERR_QUEUE_FULL;
    }

    struct ipcq_desc *head = ipcq_get_tx_desc_head(q);

    /* write the descriptor */
    head->rid = region_id;
    head->offset = offset;
    head->length = length;
    head->valid_data = valid_data;
    head->valid_length = valid_length;
    head->flags = misc_flags;
    head->cmd = cmd;

    /* barrier */
    __sync_synchronize();

    /* write the sequence pointer */
    head->seq = q->tx_seq;

    /* bump local tx sequence number */
    q->tx_seq++;

    IPCQ_DEBUG("tx_seq=%lu tx_seq_ack=%lu rx_seq_ack=%lu \n", q->tx_seq, q->tx_seq_ack->value,
               q->rx_seq_ack->value);

    return CLEANQ_ERR_OK;
}


/*
 * ================================================================================================
 * RX Path
 * ================================================================================================
 */


/**
 * @brief returns a pointer to the tail descriptor
 *
 * @param q     the IPC queue
 *
 * @returns pointer to the tail desc
 */
static inline struct ipcq_desc *ipcq_get_rx_desc_tail(struct cleanq_ipcq *q)
{
    return &q->rx_descs[q->rx_seq % q->slots];
}


/**
 * @brief checks if there is a message to be received
 *
 * @param q     the IPC queue to check
 *
 * @returns TRUE if there is a message, FALSE otherwise *
 */
static bool ipcq_can_recv(struct cleanq_ipcq *q)
{
    return (q->rx_seq <= ipcq_get_rx_desc_tail(q)->seq);
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
static errval_t ipcq_enqueue(struct cleanq *queue, regionid_t region_id, genoffset_t offset,
                             genoffset_t length, genoffset_t valid_data, genoffset_t valid_length,
                             uint64_t misc_flags)
{
    return ipcq_enqueue_internal((struct cleanq_ipcq *)queue, region_id, offset, length,
                                 valid_data, valid_length, misc_flags, 0);
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
static errval_t ipcq_dequeue(struct cleanq *queue, regionid_t *region_id, genoffset_t *offset,
                             genoffset_t *length, genoffset_t *valid_data,
                             genoffset_t *valid_length, uint64_t *misc_flags)
{
    errval_t err;

    struct cleanq_ipcq *q = (struct cleanq_ipcq *)queue;

    if (!ipcq_can_recv(q)) {
        return CLEANQ_ERR_QUEUE_EMPTY;
    }

    struct ipcq_desc *tail = ipcq_get_rx_desc_tail(q);

    /* normal case, no command */
    if (tail->cmd == 0) {
        *region_id = tail->rid;
        *offset = tail->offset;
        *length = tail->length;
        *valid_data = tail->valid_data;
        *valid_length = tail->valid_length;
        *misc_flags = tail->flags;

        IPCQ_DEBUG("rx_seq_ack=%lu tx_seq_ack=%lu \n", q->rx_seq_ack->value, q->tx_seq_ack->value);

        q->rx_seq++;
        q->rx_seq_ack->value = q->rx_seq;

        return CLEANQ_ERR_OK;
    }

    /* handle the case where the flags are register / deregister commands */
    if (tail->cmd == IPCQ_CMD_REGISTER) {
        struct capref cap;
        cap.len = tail->length;
        cap.vaddr = (void *)tail->offset;
        cap.paddr = (uint64_t)tail->valid_data;
        err = handle_register_command((struct cleanq_ipcq *)queue, cap, tail->rid);
    } else {
        err = handle_register_decommand((struct cleanq_ipcq *)queue, tail->rid);
    }

    /* TODO: should reply to the other side that operatin has failed */
    if (err_is_fail(err)) {
        printf("TODO: report failed outcome to other side\n");
    }

    q->rx_seq++;
    q->rx_seq_ack->value = q->rx_seq;

    IPCQ_DEBUG("rx_seq_ack=%lu tx_seq_ack=%lu reg/dereg\n", q->rx_seq_ack->value,
               q->tx_seq_ack->value);

    /* commands handled, receive again */
    return ipcq_dequeue(queue, region_id, offset, length, valid_data, valid_length, misc_flags);
}


/**
 * @brief Send a notification about new buffers on the queue
 *
 * @param q      The queue to call the operation on
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
static errval_t ipcq_notify(struct cleanq *q)
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
static errval_t ipcq_register(struct cleanq *q, struct capref cap, regionid_t rid)
{
    struct cleanq_ipcq *queue = (struct cleanq_ipcq *)q;

    /* busy wait until we can send a command message */
    while (!ipcq_can_send(queue))
        ;

    return ipcq_enqueue_internal(queue, rid, (genoffset_t)cap.vaddr, (genoffset_t)cap.len,
                                 (genoffset_t)cap.paddr, 0, 0, IPCQ_CMD_REGISTER);
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
static errval_t ipcq_deregister(struct cleanq *q, regionid_t rid)
{
    struct cleanq_ipcq *queue = (struct cleanq_ipcq *)q;

    /* busy wait until we can send a command message */
    while (!ipcq_can_send(queue))
        ;

    return ipcq_enqueue_internal(queue, rid, 0, 0, 0, 0, 0, IPCQ_CMD_DEREGISTER);
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
errval_t ipcq_control(struct cleanq *q, uint64_t request, uint64_t value, uint64_t *result)
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
 * @brief Destroys a descriptor queue and frees its resources
 *
 * @param queue The descriptor queue
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t ipcq_destroy(struct cleanq *queue)
{
    struct cleanq_ipcq *q = (struct cleanq_ipcq *)queue;

    if (q->rxtx_mem && munmap(q->rxtx_mem, q->memsize) == -1) {
        printf("WARNING: IPCQ destroy failed. (munmap)\n");
    }

    if (q->name && shm_unlink(q->name) == -1) {
        printf("WARNING: IPCQ destroy failed. (shm_unlink)\n");
    }

    free(q->name);
    free(q);

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
errval_t cleanq_ipcq_create(struct cleanq_ipcq **q, char *name, bool clear)
{
    errval_t err;
    struct cleanq_ipcq *newq;

    IPCQ_DEBUG("create start\n");

    newq = (struct cleanq_ipcq *)calloc(sizeof(struct cleanq_ipcq), 1);
    if (newq == NULL) {
        return CLEANQ_ERR_MALLOC_FAIL;
    }

    newq->name = strdup(name);
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

    if (creator && ftruncate(fd, IPCQ_MEM_SIZE)) {
        goto cleanup3;
    }


    IPCQ_DEBUG("Mapping queue frame\n");
    void *buf = mmap(NULL, IPCQ_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) {
        err = CLEANQ_ERR_MALLOC_FAIL;
        goto cleanup3;
    }

    if (clear) {
        memset(buf, 0, IPCQ_MEM_SIZE);
    }

    newq->rxtx_mem = buf;
    newq->memsize = IPCQ_MEM_SIZE;


    if (creator) {
        newq->tx_seq_ack = newq->rxtx_mem;
        newq->rx_seq_ack = newq->rxtx_mem + IPCQ_CHAN_SIZE;

        newq->tx_descs = newq->rxtx_mem + IPCQ_MESSAGE_SIZE;
        newq->rx_descs = newq->rxtx_mem + IPCQ_CHAN_SIZE + IPCQ_CHAN_SIZE;
    } else {
        newq->tx_seq_ack = newq->rxtx_mem + IPCQ_CHAN_SIZE;
        newq->rx_seq_ack = newq->rxtx_mem;

        newq->tx_descs = newq->rxtx_mem + IPCQ_CHAN_SIZE + IPCQ_MESSAGE_SIZE;
        newq->rx_descs = newq->rxtx_mem + IPCQ_MESSAGE_SIZE;
    }

    /* set the number of slots, this is the size - 1 to hold the tx|rx_seq_ack */
    newq->slots = IPCQ_DEFAULT_SIZE - 1;

    /* set the values of the sequence acknowledges */
    newq->tx_seq_ack->value = 0;
    newq->rx_seq_ack->value = 0;

    /* initialize the sequece numbers */
    newq->rx_seq = 1;
    newq->tx_seq = 1;

    /* initialize generic part */
    err = cleanq_init(&newq->q);
    if (err_is_fail(err)) {
        goto cleanup4;
    }

    /* setting the functions */
    newq->q.f.enq = ipcq_enqueue;
    newq->q.f.deq = ipcq_dequeue;
    newq->q.f.reg = ipcq_register;
    newq->q.f.dereg = ipcq_deregister;
    newq->q.f.notify = ipcq_notify;
    newq->q.f.ctrl = ipcq_control;

    *q = newq;

    IPCQ_DEBUG("create end %p \n", *q);

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
