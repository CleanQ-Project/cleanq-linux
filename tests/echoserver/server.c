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

#include <cleanq/cleanq.h>
#include <cleanq/backends/ff_queue.h>

#define BENCH
//#define DEBUG(x...) printf("devif_test: " x)
#define DEBUG(x...)                                                                               \
    do {                                                                                          \
    } while (0)

#define BUF_SIZE 2048
#define NUM_BUFS 128
#define MEMORY_SIZE BUF_SIZE *NUM_BUFS

static struct cleanq_ffq *ffq_queue;
static struct cleanq *que;

int main(int argc, char *argv[])
{
    (void)(argc);
    (void)(argv);

    errval_t err;

    printf("IPC echo queue started\n");


    err = cleanq_ffq_create(&ffq_queue, "/cleanq-echo-ffq", true);
    assert(err_is_ok(err));

    que = (struct cleanq *)ffq_queue;

    regionid_t regid_ret;
    genoffset_t offset, length, valid_data, valid_length;
    uint64_t flags;
    printf("Starting echo\n");
    while (true) {
        err = cleanq_dequeue(que, &regid_ret, &offset, &length, &valid_data, &valid_length, &flags);
        if (err_is_fail(err)) {
            if (err == CLEANQ_ERR_QUEUE_EMPTY) {
                continue;
            } else {
                printf("Dequeue error %d\n", err);
                exit(1);
            }
        } else {
            err = cleanq_enqueue(que, regid_ret, offset, length, valid_data, valid_length, flags);
            if (err_is_fail(err)) {
                if (err == CLEANQ_ERR_QUEUE_FULL) {
                    continue;
                } else {
                    printf("Enqueue error %d\n", err);
                    exit(1);
                }
            }
        }
    }
}
