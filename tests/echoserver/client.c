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

#include <cleanq/cleanq.h>
#include <cleanq/backends/ff_queue.h>
#include <cleanq/backends/loopback_queue.h>
#include <cleanq/backends/debug_queue.h>


//#define DEBUG(x...) printf("devif_test: " x)
#define DEBUG(x...)                                                                               \
    do {                                                                                          \
    } while (0)

#define BUF_SIZE 2048
#define NUM_BUFS 64  // IPC queue has size 63
#define MEMORY_SIZE BUF_SIZE *NUM_BUFS

static struct capref memory;
static regionid_t regid;
static uint64_t phys;

static volatile uint32_t num_tx = 0;
static volatile uint32_t num_rx = 0;

struct direct_state
{
    struct list_ele *first;
    struct list_ele *last;
};

struct list_ele
{
    regionid_t rid;
    bufferid_t bid;
    uint64_t addr;
    size_t len;
    uint64_t flags;

    struct list_ele *next;
};

static struct cleanq_ffq *ffq;
static struct cleanq_debugq *dbgq;
static struct cleanq_loopbackq *lbq;
static struct cleanq *que;

static volatile bool enq[NUM_BUFS];

#define BASE_PAGE_SIZE 4096

#define NUM_REGIONS 32

#define NUM_ROUNDS 1000000

#ifdef BENCH_CLEANQ

#    include <bench.h>

static bench_ctl_t *ctl_en;
static bench_ctl_t *ctl_de;
static bench_ctl_t *ctl_reg;
static bench_ctl_t *ctl_dereg;


//#define NUM_ROUNDS 100


static void dump_results(char *prefix, bool destroy)
// static void dump_results_console(char* prefix, bool destroy)
{
    char buffer[512];

    // first 10 % is warmup
    if (ctl_en == NULL) {
        return;
    }

    FILE *f = fopen(prefix, "w+");
    assert(f != NULL);

    for (int i = NUM_ROUNDS / 10; i < NUM_ROUNDS; i++) {
#    ifdef BENCH_DEVQ
        sprintf(buffer, ";%s_devq;enqueue;%lu;dequeue;%lu;register;%lu;deregister;%lu \n",
#    else
        sprintf(buffer, ";%s;enqueue;%lu;dequeue;%lu;register;%lu;deregister;%lu \n",
#    endif
                prefix, ctl_en->data[i], ctl_de->data[i], ctl_reg->data[i], ctl_dereg->data[i]);
        printf("%s", buffer);
        fprintf(f, "%s", buffer);
    }

    if (destroy) {
        bench_ctl_destroy(ctl_dereg);
        bench_ctl_destroy(ctl_reg);
        bench_ctl_destroy(ctl_de);
        bench_ctl_destroy(ctl_en);
    }
}
#else
static void dump_results(char *prefix, bool destroy)
{
    (void)(prefix);
    (void)(destroy);
}
#endif

static void test_register_randomized(struct cleanq *queue)
{
    errval_t err;
    struct capref regions[NUM_REGIONS];
    regionid_t rids[NUM_REGIONS];
    bool is_reg[NUM_REGIONS];

    #ifdef BENCH_CLEANQ

    ctl_reg = (bench_ctl_t *)calloc(1, sizeof(*ctl_reg));
    ctl_reg->mode = BENCH_MODE_FIXEDRUNS;
    ctl_reg->result_dimensions = 1;
    ctl_reg->min_runs = NUM_ROUNDS;
    ctl_reg->data = (cycles_t *)calloc(ctl_reg->min_runs * ctl_reg->result_dimensions,
                                       sizeof(*ctl_de->data));

    ctl_dereg = (bench_ctl_t *)calloc(1, sizeof(*ctl_dereg));
    ctl_dereg->mode = BENCH_MODE_FIXEDRUNS;
    ctl_dereg->result_dimensions = 1;
    ctl_dereg->min_runs = NUM_ROUNDS;
    ctl_dereg->data = (cycles_t *)calloc(ctl_dereg->min_runs * ctl_dereg->result_dimensions,
                                         sizeof(*ctl_dereg->data));

#endif

    for (int i = 0; i < NUM_REGIONS; i++) {
        regions[i].vaddr = malloc(BASE_PAGE_SIZE);
        // TODO do i needs this?
        regions[i].paddr = (uint64_t)regions[i].vaddr;
        regions[i].len = BASE_PAGE_SIZE;
        assert(regions[i].vaddr);
        is_reg[i] = false;
    }

    srand(time(NULL));
    int idx = 0;
    struct capref ret;

#ifdef BENCH_CLEANQ
    cycles_t res1;
    cycles_t res2;
    cycles_t start_reg = 0, end_reg = 0;
    cycles_t start_dereg = 0, end_dereg = 0;
#endif

    for (int i = 0; i < NUM_ROUNDS; i++) {
        idx = rand() % NUM_REGIONS;

        if ((i % 32) == 0) {
            usleep(50);
        }
        if (!is_reg[idx]) {
#ifdef BENCH_CLEANQ
            start_reg = rdtscp();
#endif
            err = cleanq_register(queue, regions[idx], &rids[idx]);
#ifdef BENCH_CLEANQ
            end_reg = rdtscp();
#endif
            if (err_is_fail(err)) {
                printf("Registering memory to cleanq failed %d\n", err);
                exit(1);
            }
#ifdef BENCH_CLEANQ
            res1 = end_reg - start_reg;
            bench_ctl_add_run(ctl_reg, &res1);
#endif
            is_reg[idx] = true;
        } else {
#ifdef BENCH_CLEANQ
            start_dereg = rdtscp();
#endif
            err = cleanq_deregister(queue, rids[idx], &ret);
#ifdef BENCH_CLEANQ
            end_dereg = rdtscp();
#endif
            if (err_is_fail(err)) {
                printf("Deregistering memory to cleanq failed %d\n", err);
                exit(1);
            }
#ifdef BENCH_CLEANQ
            res2 = end_dereg - start_dereg;
            bench_ctl_add_run(ctl_dereg, &res2);
#endif
            is_reg[idx] = false;
        }
    }
}

static void test_register_acc(struct cleanq *queue)
{
    errval_t err;
    struct capref regions[NUM_REGIONS];
    regionid_t rids[NUM_REGIONS];

#ifdef BENCH_CLEANQ
    ctl_reg = (bench_ctl_t *)calloc(1, sizeof(*ctl_reg));
    ctl_reg->mode = BENCH_MODE_FIXEDRUNS;
    ctl_reg->result_dimensions = 1;
    ctl_reg->min_runs = NUM_ROUNDS;
    ctl_reg->data = (cycles_t *)calloc(ctl_reg->min_runs * ctl_reg->result_dimensions,
                                       sizeof(*ctl_de->data));

    ctl_dereg = (bench_ctl_t *)calloc(1, sizeof(*ctl_dereg));
    ctl_dereg->mode = BENCH_MODE_FIXEDRUNS;
    ctl_dereg->result_dimensions = 1;
    ctl_dereg->min_runs = NUM_ROUNDS;
    ctl_dereg->data = (cycles_t *)calloc(ctl_dereg->min_runs * ctl_dereg->result_dimensions,
                                         sizeof(*ctl_dereg->data));

#endif

    for (int i = 0; i < NUM_REGIONS; i++) {
        regions[i].vaddr = malloc(BASE_PAGE_SIZE);
        // TODO do i needs this?
        regions[i].paddr = (uint64_t)regions[i].vaddr;
        regions[i].len = BASE_PAGE_SIZE;
        assert(regions[i].vaddr);
    }

    srand(time(NULL));
    struct capref ret;

#ifdef BENCH_CLEANQ
    cycles_t res1;
    cycles_t res2;
    cycles_t start_reg = 0, end_reg = 0;
    cycles_t start_dereg = 0, end_dereg = 0;
#endif
    for (int i = 0; i < NUM_ROUNDS; i++) {
#ifdef BENCH_CLEANQ
        start_reg = rdtscp();
#endif

        for (int j = 0; j < NUM_REGIONS; j++) {
            err = cleanq_register(queue, regions[j], &rids[j]);
            if (err_is_fail(err)) {
                printf("Registering memory to cleanq failed %d\n", err);
                exit(1);
            }
        }

#ifdef BENCH_CLEANQ
        end_reg = rdtscp();
        res1 = (end_reg - start_reg) / NUM_REGIONS;
        bench_ctl_add_run(ctl_reg, &res1);
#endif

        usleep(50);
        if ((i % (NUM_ROUNDS / 10)) == 0) {
            printf("Round %d \n", i);
        }

#ifdef BENCH_CLEANQ
        start_dereg = rdtscp();

#endif
        for (int j = 0; j < NUM_REGIONS; j++) {
            err = cleanq_deregister(queue, rids[j], &ret);
            if (err_is_fail(err)) {
                printf("Deregistering memory to cleanq failed \n");
                exit(1);
            }
        }
#ifdef BENCH_CLEANQ
        end_dereg = rdtscp();
        res2 = (end_dereg - start_dereg) / NUM_REGIONS;
        bench_ctl_add_run(ctl_dereg, &res2);
#endif
    }
}

static void test_register(struct cleanq *queue)
{
    errval_t err;

struct capref regions[NUM_REGIONS];
regionid_t rids[NUM_REGIONS];
#ifdef BENCH_CLEANQ





    ctl_reg = (bench_ctl_t *)calloc(1, sizeof(*ctl_reg));
    ctl_reg->mode = BENCH_MODE_FIXEDRUNS;
    ctl_reg->result_dimensions = 1;
    ctl_reg->min_runs = NUM_ROUNDS;
    ctl_reg->data = (cycles_t *)calloc(ctl_reg->min_runs * ctl_reg->result_dimensions,
                                       sizeof(*ctl_de->data));

    ctl_dereg = (bench_ctl_t *)calloc(1, sizeof(*ctl_dereg));
    ctl_dereg->mode = BENCH_MODE_FIXEDRUNS;
    ctl_dereg->result_dimensions = 1;
    ctl_dereg->min_runs = NUM_ROUNDS;
    ctl_dereg->data = (cycles_t *)calloc(ctl_dereg->min_runs * ctl_dereg->result_dimensions,
                                         sizeof(*ctl_dereg->data));

#endif

    for (int i = 0; i < NUM_REGIONS; i++) {
        regions[i].vaddr = malloc(BASE_PAGE_SIZE);
        // TODO do i needs this?
        regions[i].paddr = (uint64_t)regions[i].vaddr;
        regions[i].len = BASE_PAGE_SIZE;
        assert(regions[i].vaddr);
    }

    srand(time(NULL));
    int idx = 0;
    struct capref ret;

#ifdef BENCH_CLEANQ
    cycles_t res1;
    cycles_t res2;
    cycles_t start_reg = 0, end_reg = 0;
    cycles_t start_dereg = 0, end_dereg = 0;
#endif

    for (int i = 0; i < NUM_ROUNDS; i++) {
        idx = i % NUM_REGIONS;
#ifdef BENCH_CLEANQ
        start_reg = rdtscp();
#endif
        err = cleanq_register(queue, regions[idx], &rids[idx]);
#ifdef BENCH_CLEANQ
        end_reg = rdtscp();
#endif
        if (err_is_fail(err)) {
            printf("Registering memory to cleanq failed \n");
            exit(1);
        }
#ifdef BENCH_CLEANQ
        res1 = end_reg - start_reg;
        bench_ctl_add_run(ctl_reg, &res1);
#endif


        if ((i % (NUM_ROUNDS / 10)) == 0) {
            printf("Round %d \n", i);
        }

        usleep(50);

#ifdef BENCH_CLEANQ
        start_dereg = rdtscp();
#endif
        err = cleanq_deregister(queue, rids[idx], &ret);
#ifdef BENCH_CLEANQ
        end_dereg = rdtscp();
#endif
        if (err_is_fail(err)) {
            printf("Deregistering memory to cleanq failed \n");
            exit(1);
        }
#ifdef BENCH_CLEANQ
        res2 = end_dereg - start_dereg;
        bench_ctl_add_run(ctl_dereg, &res2);
#endif
    }
}

static void test_enqueue_dequeue(struct cleanq *queue)
{
    errval_t err;
    num_tx = 0;
    num_rx = 0;

    // enqueue from the beginning of the region
    for (int i = 0; i < NUM_BUFS / 2; i++) {
        err = cleanq_enqueue(queue, regid, i * BUF_SIZE, BUF_SIZE, 0, BUF_SIZE, 0);
        if (err_is_fail(err)) {
            if (err == CLEANQ_ERR_QUEUE_FULL) {
                i--;
            } else {
                printf("Enqueue failed \n");
                exit(1);
            }
        } else {
            num_tx++;
        }
    }


    regionid_t regid_ret;
    genoffset_t offset, length, valid_data, valid_length;
    uint64_t flags;
    // enqueue from the end of the region
    for (int i = 0; i < NUM_BUFS / 2; i++) {
        err = cleanq_dequeue(queue, &regid_ret, &offset, &length, &valid_data, &valid_length,
                             &flags);
        if (err_is_fail(err)) {
            if (err == CLEANQ_ERR_QUEUE_EMPTY) {
                i--;
            } else {
                printf("Dequeue failed \n");
                exit(1);
            }
        } else {
            num_rx++;
        }
    }
}


static void test_randomized_test(struct cleanq *queue)
{
    errval_t err;
    num_tx = 0;
    num_rx = 0;
    memset((void *)enq, 0, sizeof(bool) * NUM_BUFS);

#ifdef BENCH_CLEANQ
    ctl_en = (bench_ctl_t *)calloc(1, sizeof(*ctl_en));
    ctl_en->mode = BENCH_MODE_FIXEDRUNS;
    ctl_en->result_dimensions = 1;
    ctl_en->min_runs = NUM_ROUNDS;
    ctl_en->data = (cycles_t *)calloc(ctl_en->min_runs * ctl_en->result_dimensions,
                                      sizeof(*ctl_de->data));

    ctl_de = (bench_ctl_t *)calloc(1, sizeof(*ctl_en));
    ctl_de->mode = BENCH_MODE_FIXEDRUNS;
    ctl_de->result_dimensions = 1;
    ctl_de->min_runs = NUM_ROUNDS;
    ctl_de->data = (cycles_t *)calloc(ctl_de->min_runs * ctl_de->result_dimensions,
                                      sizeof(*ctl_de->data));
#endif

    for (int i = 0; i < NUM_BUFS; i++) {
        enq[i] = false;
    }

    srand(time(NULL));
    int idx = 0;

#ifdef BENCH_CLEANQ
    cycles_t res1;
    cycles_t res2;
    cycles_t start_en = 0, end_en = 0;
    cycles_t start_de = 0, end_de = 0;
#endif

    // enqueue from the beginning of the region
    for (int i = 0; i < NUM_ROUNDS; i++) {
        for (int j = 0; j < NUM_BUFS / 2; j++) {
            idx = rand() % NUM_BUFS;
            while (enq[idx]) {
                idx = rand() % NUM_BUFS;
            }

#ifdef BENCH_CLEANQ
            start_en = rdtscp();
#endif
            err = cleanq_enqueue(queue, regid, idx * BUF_SIZE, BUF_SIZE, 0, BUF_SIZE, 0);
#ifdef BENCH_CLEANQ
            end_en = rdtscp();
#endif
            if (err_is_fail(err)) {
                if (err == CLEANQ_ERR_QUEUE_FULL) {
                    j--;
                } else {
                    printf("Enqueue failed %d \n", err);
                    exit(1);
                }
            } else {
                enq[idx] = true;
                num_tx++;
#ifdef BENCH_CLEANQ
                res1 = end_en - start_en;
                bench_ctl_add_run(ctl_en, &res1);
#endif
            }
        }

        if ((i % (NUM_ROUNDS / 10)) == 0) {
            printf("Round %d \n", i);
        }

        regionid_t regid_ret;
        genoffset_t offset, length, valid_data, valid_length;
        uint64_t flags;
        // enqueue from the end of the region
        for (int j = 0; j < NUM_BUFS / 2; j++) {
#ifdef BENCH_CLEANQ
            start_de = rdtscp();
#endif
            err = cleanq_dequeue(queue, &regid_ret, &offset, &length, &valid_data, &valid_length,
                                 &flags);
#ifdef BENCH_CLEANQ
            end_de = rdtscp();
#endif
            if (err_is_fail(err)) {
                if (err == CLEANQ_ERR_QUEUE_EMPTY) {
                    j--;
                } else {
                    printf("Dequeue failed %d\n", err);
                    exit(1);
                }

            } else {
                enq[offset / BUF_SIZE] = false;
                num_rx++;
#ifdef BENCH_CLEANQ
                res2 = end_de - start_de;
                bench_ctl_add_run(ctl_de, &res2);
#endif
            }
        }
    }
}


static void test_randomized_acc_test(struct cleanq *queue)
{
    errval_t err;
    num_tx = 0;
    num_rx = 0;
    memset((void *)enq, 0, sizeof(bool) * NUM_BUFS);

#ifdef BENCH_CLEANQ
    ctl_en = (bench_ctl_t *)calloc(1, sizeof(*ctl_en));
    ctl_en->mode = BENCH_MODE_FIXEDRUNS;
    ctl_en->result_dimensions = 1;
    ctl_en->min_runs = NUM_ROUNDS;
    ctl_en->data = (cycles_t *)calloc(ctl_en->min_runs * ctl_en->result_dimensions,
                                      sizeof(*ctl_de->data));

    ctl_de = (bench_ctl_t *)calloc(1, sizeof(*ctl_en));
    ctl_de->mode = BENCH_MODE_FIXEDRUNS;
    ctl_de->result_dimensions = 1;
    ctl_de->min_runs = NUM_ROUNDS;
    ctl_de->data = (cycles_t *)calloc(ctl_de->min_runs * ctl_de->result_dimensions,
                                      sizeof(*ctl_de->data));

    cycles_t res1;
    cycles_t res2;
    cycles_t start_en = 0, end_en = 0;
    cycles_t start_de = 0, end_de = 0;
#endif

    // enqueue from the beginning of the region
    for (int i = 0; i < NUM_ROUNDS; i++) {
#ifdef BENCH_CLEANQ
        start_en = rdtscp();
#endif
        for (int j = 0; j < NUM_BUFS / 2; j++) {
            err = cleanq_enqueue(queue, regid, j * BUF_SIZE, BUF_SIZE, 0, BUF_SIZE, 0);
            assert(err_is_ok(err));
        }
#ifdef BENCH_CLEANQ
        end_en = rdtscp();
        res1 = (end_en - start_en) / (NUM_BUFS / 2);
        bench_ctl_add_run(ctl_en, &res1);
#endif

        usleep(50);
        if ((i % (NUM_ROUNDS / 10)) == 0) {
            printf("Round %d \n", i);
        }

        regionid_t regid_ret;
        genoffset_t offset, length, valid_data, valid_length;
        uint64_t flags;
        // enqueue from the end of the region
#ifdef BENCH_CLEANQ
        start_de = rdtscp();
#endif
        for (int j = 0; j < NUM_BUFS / 2; j++) {
            err = cleanq_dequeue(queue, &regid_ret, &offset, &length, &valid_data, &valid_length,
                                 &flags);
            assert(err_is_ok(err));
        }

#ifdef BENCH_CLEANQ
        end_de = rdtscp();
        res2 = (end_de - start_de) / (NUM_BUFS / 2);
        bench_ctl_add_run(ctl_de, &res2);
#endif
    }
}


errval_t run_test(struct cleanq *queue, char *q_name)
{
    errval_t err;

    err = cleanq_register(queue, memory, &regid);
    if (err_is_fail(err)) {
        printf("Registering memory to cleanq failed using q: %s\n", q_name);
        exit(1);
    }

    printf("Starting register/deregister test %s \n", q_name);
    test_register(queue);


    printf("Starting enqueue/dequeue test %s \n", q_name);
    test_enqueue_dequeue(queue);

    // TODO no results printed
    printf("Starting register/deregister randomized test %s \n", q_name);
    test_register_randomized(queue);

    printf("Starting enqueue/dequeue randomized test %s\n", q_name);
    test_randomized_test(queue);

    dump_results((char *)q_name, false);

    printf("Starting registet/dereqister accumulated randomized test %s\n", q_name);
    test_register_acc(queue);

    printf("Starting enqueue/dequeue accumulated randomized test %s\n", q_name);
    test_randomized_acc_test(queue);

    err = cleanq_deregister(queue, regid, &memory);
    if (err_is_fail(err)) {
        printf("Deregistering memory from cleanq failed %s\n", q_name);
        exit(1);
    }

    return CLEANQ_ERR_OK;
}


int main(int argc, char *argv[])
{
    errval_t err;

    (void)(argc);
    (void)(argv);

#ifdef BENCH_CLEANQ
    bench_init();
#endif

    // Allocate memory
    memory.vaddr = malloc(MEMORY_SIZE);
    memory.paddr = (uint64_t)memory.vaddr;
    memory.len = MEMORY_SIZE;

    phys = memory.paddr;


    printf("IPC queue test started \n");

    err = cleanq_ffq_create(&ffq, "/cleanq-echo-ffq", false);
    assert(err_is_ok(err));

    que = (struct cleanq *)ffq;

    err = run_test(que, (char *)"ffq");
    if (err_is_fail(err)) {
        printf("Test for ipc queue failed\n");
        exit(1);
    }

    dump_results((char *)"ffq_acc", true);

    printf("Descriptor queue test started \n");
    err = loopback_queue_create(&lbq);
    if (err_is_fail(err)) {
        printf("Allocating cleanq failed \n");
        exit(1);
    }

    que = (struct cleanq *)lbq;

    err = run_test(que, (char *)"loopback");
    if (err_is_fail(err)) {
        printf("Test for loopback queue failed\n");
        exit(1);
    }

    dump_results((char *)"loopback_acc", true);

    err = cleanq_debugq_create(&dbgq, (struct cleanq *)lbq);
    if (err_is_fail(err)) {
        printf("Creating debug queue failed\n");
        exit(1);
    }

    que = (struct cleanq *)dbgq;

    err = run_test(que, (char *)"debug_loopback");
    if (err_is_fail(err)) {
        printf("Test for loopback queue failed\n");
        exit(1);
    }

    dump_results((char *)"debug_loopback_acc", true);
}
