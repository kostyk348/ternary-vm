/*
 * bench_vm.c -- host benchmark for the C core include/ternary_vm.h, with an
 * optional side-by-side measurement of the MPTC primitive layer.
 *
 * What is measured (and it is printed verbatim, so the report can cite it):
 *   1. VM throughput: the real program images from programs/ (assembled by
 *      vm/asm.py, frozen by tools/emit_bytecode.py), executed R times each;
 *      reported as nanoseconds per executed VM instruction.
 *   2. Tryte primitive add in the value domain (what the VM's ADD does):
 *      acc = clamp(acc + step).
 *   3. Tryte primitive add in the packed-code domain (encode + add + decode),
 *      the closest analogue of MPTC's packed ternary_add8.
 *   4. MPTC (when built with -DHAVE_MPTC -I<MPTC>/include):
 *      ternary_add8, ternary_to_int8, ternary_from_int8, ternary_dot8.
 *
 * Build (no external dependencies):
 *   gcc -std=c99 -O2 -Wall -Wextra -Iinclude -Iexamples/c bench/bench_vm.c -o bench_vm
 * Build with the MPTC comparison:
 *   gcc -std=c99 -O2 -Wall -Wextra -Iinclude -Iexamples/c -I../MPTC/include \
 *       -DHAVE_MPTC=1 bench/bench_vm.c -o bench_vm
 *
 * Host-only tool (uses clock_gettime); the library itself stays pure C99.
 */
#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ternary_vm.h"
#include "programs_generated.h"

#ifdef HAVE_MPTC
#include "mptc/ternary.h"
#endif

/* Iteration counts: small enough to keep CI fast, large enough for signal. */
#ifndef VM_RUNS
#define VM_RUNS 20000
#endif
#ifndef PRIM_ITER
#define PRIM_ITER 2000000
#endif

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static volatile int g_sink = 0;

/* ------------------------------------------------------------------ */
/*  1. VM instruction throughput on the repository programs            */
/* ------------------------------------------------------------------ */
static void bench_vm_programs(void)
{
    uint16_t p;
    for (p = 0; p < TVM_DEMO_PROGRAM_COUNT; ++p) {
        const tvm_demo_program_t *d = &tvm_demo_programs[p];
        tvm_t vm;
        int r, i, failures = 0;
        unsigned long steps = 0;
        double t0, t1, ns;

        t0 = now_seconds();
        for (r = 0; r < VM_RUNS; ++r) {
            tvm_init(&vm, d->code, d->code_len);
            if (tvm_run(&vm, 0) != TVM_OK) failures += 1;
            if (vm.out_len != d->expect_len) failures += 1;
            for (i = 0; i < (int)d->expect_len && i < (int)vm.out_len; ++i)
                if (vm.out[i] != d->expect[i]) failures += 1;
            g_sink += (int)vm.reg[0];
            steps += vm.steps;
        }
        t1 = now_seconds();
        ns = (t1 - t0) * 1e9 / (double)steps;
        printf("BENCH vm_program name=%s runs=%d steps=%lu seconds=%.6f "
               "ns_per_instruction=%.2f failures=%d\n",
               d->name, VM_RUNS, steps, t1 - t0, ns, failures);
    }
}

/* ------------------------------------------------------------------ */
/*  2./3. Tryte primitives                                             */
/* ------------------------------------------------------------------ */
static void bench_tryte_primitives(void)
{
    /* volatile: without it GCC folds the whole saturating loop away. */
    volatile tvm_tryte_t acc = 0;
    volatile tvm_code_t packed = 0;
    int i;
    double t0, t1;

    /* Value-domain saturated add -- exactly what TVM_OP_ADD does. */
    t0 = now_seconds();
    for (i = 0; i < PRIM_ITER; ++i) {
        acc = tvm_clamp((int32_t)acc + (tvm_tryte_t)((i % 2001) - 1000));
    }
    t1 = now_seconds();
    printf("BENCH tryte_add_value n=%d seconds=%.6f ns_per_op=%.3f checksum=%d\n",
           PRIM_ITER, t1 - t0, (t1 - t0) * 1e9 / PRIM_ITER, (int)acc);
    g_sink += (int)acc;

    /* Packed-code-domain saturated add: encode + add + decode. */
    t0 = now_seconds();
    for (i = 0; i < PRIM_ITER; ++i) {
        tvm_tryte_t a = tvm_decode(packed);
        a = tvm_clamp((int32_t)a + (tvm_tryte_t)((i % 2001) - 1000));
        packed = tvm_encode(a);
    }
    t1 = now_seconds();
    printf("BENCH tryte_add_packed n=%d seconds=%.6f ns_per_op=%.3f checksum=%d\n",
           PRIM_ITER, t1 - t0, (t1 - t0) * 1e9 / PRIM_ITER, (int)tvm_decode(packed));
    g_sink += (int)tvm_decode(packed);
}

#ifdef HAVE_MPTC
static void bench_mptc(void)
{
    trit8_t acc = ternary_from_int8(0);
    trit8_t x = ternary_from_int8(7);
    int i;
    double t0, t1;
    q15_t inputs[8];
    trit8_t weights = ternary_from_int8(85); /* trits in {-1,0,+1} */

    for (i = 0; i < 8; ++i) inputs[i] = (q15_t)(i * 1234);

    t0 = now_seconds();
    for (i = 0; i < PRIM_ITER; ++i) {
        acc = ternary_from_int8((i % 2001) - 1000);
        (void)acc;
    }
    t1 = now_seconds();
    printf("BENCH mptc_from_int8 n=%d seconds=%.6f ns_per_op=%.3f checksum=%d\n",
           PRIM_ITER, t1 - t0, (t1 - t0) * 1e9 / PRIM_ITER, (int)acc);

    t0 = now_seconds();
    for (i = 0; i < PRIM_ITER; ++i) {
        g_sink += ternary_to_int8(x);
    }
    t1 = now_seconds();
    printf("BENCH mptc_to_int8 n=%d seconds=%.6f ns_per_op=%.3f checksum=%d\n",
           PRIM_ITER, t1 - t0, (t1 - t0) * 1e9 / PRIM_ITER, g_sink);

    /* The closest analogue of the packed add above. */
    acc = ternary_from_int8(0);
    t0 = now_seconds();
    for (i = 0; i < PRIM_ITER; ++i) {
        acc = ternary_add8(acc, ternary_from_int8((i % 2001) - 1000));
    }
    t1 = now_seconds();
    printf("BENCH mptc_ternary_add8 n=%d seconds=%.6f ns_per_op=%.3f checksum=%d\n",
           PRIM_ITER, t1 - t0, (t1 - t0) * 1e9 / PRIM_ITER, ternary_to_int8(acc));

    t0 = now_seconds();
    for (i = 0; i < PRIM_ITER; ++i) {
        g_sink += ternary_dot8(weights, inputs);
    }
    t1 = now_seconds();
    printf("BENCH mptc_ternary_dot8 n=%d seconds=%.6f ns_per_op=%.3f checksum=%d\n",
           PRIM_ITER, t1 - t0, (t1 - t0) * 1e9 / PRIM_ITER, g_sink);
}
#endif

int main(void)
{
    double t0, t1;

    printf("# ternary-vm C core benchmark (bench/bench_vm.c)\n");
    printf("# compiler: " 
#if defined(__GNUC__)
           "GCC " __VERSION__
#elif defined(__clang__)
           "Clang " __VERSION__
#else
           "unknown"
#endif
           "\n");
    printf("# build: VM_RUNS=%d PRIM_ITER=%d mptc=%s\n",
           VM_RUNS, PRIM_ITER,
#ifdef HAVE_MPTC
           "yes"
#else
           "no"
#endif
    );

    t0 = now_seconds();
    bench_vm_programs();
    bench_tryte_primitives();
#ifdef HAVE_MPTC
    bench_mptc();
#else
    printf("# MPTC comparison skipped: rebuild with -DHAVE_MPTC -I<MPTC>/include\n");
#endif
    t1 = now_seconds();

    printf("# total bench wall time: %.3f s (sink=%d)\n", t1 - t0, g_sink);
    return 0;
}
