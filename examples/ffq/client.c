/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached license file.
 * if you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <inttypes.h>

#include <cleanq/cleanq.h>
#include <cleanq/backends/ff_queue.h>


#define BUF_SIZE 2048
#define NUM_BUFS 64  // IPC queue has size 63
#define MEMORY_SIZE BUF_SIZE *NUM_BUFS

static struct capref memory;
static regionid_t regid;


int main(int argc, char *argv[])
{
    errval_t err;

    const char *qname = "/cleanq-echo-ffq";
    if (argc == 2) {
        qname = argv[1];
    }

    printf("Example FFQ Clien started\n");
    printf("Queuename: %s\n", qname);

    // Allocate memory
    memory.vaddr = malloc(MEMORY_SIZE);
    memory.paddr = (uint64_t)memory.vaddr;
    memory.len = MEMORY_SIZE;

    struct cleanq_ffq *ffq_queue;
    err = cleanq_ffq_create(&ffq_queue, "/cleanq-echo-ffq", false);
    if (err_is_fail(err)) {
        printf("CLIENT: failed to create the queue.\n");
    }

    struct cleanq *q = (struct cleanq *)ffq_queue;

    /* register memory */
    err = cleanq_register(q, memory, &regid);
    if (err_is_fail(err)) {
        printf("CLIENT: Registering memory to cleanq failed using q: %s\n", qname);
        exit(1);
    }

    regionid_t regid_ret;
    genoffset_t offset, length, valid_data, valid_length;
    uint64_t flags;


    for (size_t i = 0; i < 10; i++) {
        usleep(500);

        offset = 0;
        length = BUF_SIZE;
        valid_data = 0;
        valid_length = BUF_SIZE;

        printf("CLIENT: enqueueing %d [%" PRIx64 "..%" PRIx64 "]\n", regid, offset,
               offset + length - 1);
        err = cleanq_enqueue(q, regid, offset, length, valid_data, valid_length, 0);
        if (err_is_fail(err)) {
            if (err == CLEANQ_ERR_QUEUE_FULL) {
                continue;
            } else {
                printf("Dequeue error %d\n", err);
                exit(1);
            }
        }

        printf("CLIENT: dequeue buffer...\n");

        do {
            err = cleanq_dequeue(q, &regid_ret, &offset, &length, &valid_data, &valid_length,
                                 &flags);
            if (err_is_fail(err)) {
                if (err == CLEANQ_ERR_QUEUE_EMPTY) {
                    continue;
                } else {
                    printf("Enqueue error %d\n", err);
                    exit(1);
                }
            }
        } while (err_is_fail(err));
    }


    /* deregister again */
    err = cleanq_deregister(q, regid, &memory);
    if (err_is_fail(err)) {
        printf("Deregistering memory from cleanq failed %s\n", qname);
        exit(1);
    }
}
