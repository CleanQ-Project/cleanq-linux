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
#include <time.h>
#include <sched.h>
#include <assert.h>
#include <inttypes.h>

#include <cleanq/cleanq.h>
#include <cleanq/backends/ff_queue.h>


int main(int argc, char *argv[])
{
    (void)(argc);
    (void)(argv);

    errval_t err;

    const char *qname = "/cleanq-echo-ffq";
    if (argc == 2) {
        qname = argv[1];
    }

    printf("Example FFQ Server started\n");
    printf("Queuename: %s\n", qname);

    struct cleanq_ffq *ffq_queue;
    err = cleanq_ffq_create(&ffq_queue, "/cleanq-echo-ffq", true);
    if (err_is_fail(err)) {
        printf("SERVER: failed to create the queue.\n");
    }

    regionid_t regid_ret;
    genoffset_t offset, length, valid_data, valid_length;
    uint64_t flags;

    struct cleanq *q = (struct cleanq *)ffq_queue;

    printf("Starting echo\n");

    while (true) {
        err = cleanq_dequeue(q, &regid_ret, &offset, &length, &valid_data, &valid_length, &flags);
        if (err_is_fail(err)) {
            if (err == CLEANQ_ERR_QUEUE_EMPTY) {
                continue;
            } else {
                printf("Dequeue error %d\n", err);
                exit(1);
            }
        }

        printf("SERVER: dequeued %d [%" PRIx64 "..%" PRIx64 "]\n", regid_ret, offset,
               offset + length - 1);

        do {
            err = cleanq_enqueue(q, regid_ret, offset, length, valid_data, valid_length, flags);
            if (err_is_fail(err)) {
                if (err == CLEANQ_ERR_QUEUE_FULL) {
                    continue;
                } else {
                    printf("Enqueue error %d\n", err);
                    exit(1);
                }
            }
        } while (err_is_fail(err));
    }
}
