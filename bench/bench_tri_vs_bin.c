/*
 * bench_tri_vs_bin.c -- ternary-vs-binary arithmetic cost comparison
 * (the audit asks for "сравнение количества trit/bit и стоимости
 * кодирования"; this file measures the cost half on this host).
 *
 * What is measured, ns per operation, wall clock (CLOCK_MONOTONIC):
 *
 *   ternary (this codebase's semantics, 9 trits, [-9841, +9841]):
 *     - value domain : tvm_clamp(acc + x)   -- what the VM's ADD/SUB/MUL do
 *     - packed domain: tvm_decode + add + tvm_clamp + tvm_encode -- pays the
 *                      pack/unpack round trip per operation
 *     - digit-wise   : the trit-serial schoolbook adder/subtractor from
 *                      tools/ttrace_add.h, with and without the trace record
 *   binary (same host, same compiler, same flags):
 *     - 16-bit storage, saturating  (same clamp pattern as the ternary op --
 *       the like-for-like comparison)
 *     - 16-bit storage, wrapping    (what hardware add gives you for free)
 *     - 32-bit / 64-bit wrapping    (the native words)
 *
 * Also printed: the trit/bit *count* half of the comparison (computed, not
 * invented) and the cost of the packed codec (encode / decode alone).
 *
 * THE HONEST LIMITS OF THIS BENCHMARK (printed by the tool itself):
 *   - wall-clock time on a big out-of-order x86-64 host CPU, compiled with
 *     optimization; NOT an MCU, NOT an FPGA, NOT a hypothetical ternary chip;
 *   - a digit-wise ternary ALU in software pays for a loop; hardware would
 *     not. These numbers say "what the algorithms cost on binary silicon",
 *     nothing more;
 *   - energy per operation is NOT measured and CANNOT be concluded from
 *     wall-clock time. Упаковка троичных трайтов в биты не гарантирует
 *     энергетической эффективности; сравнение с бинарными системами по
 *     энергии — гипотеза, требующая измерений на железе.
 *
 * Build:
 *   gcc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude -Itools \
 *       bench/bench_tri_vs_bin.c -o bench_tri_vs_bin
 *
 * Licence: Apache-2.0 (same as the repository).
 */
#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "ternary_vm.h"
#include "ttrace_add.h"

#ifndef TRI_ITER
#define TRI_ITER 2000000
#endif

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static long g_sink = 0;

typedef struct {
    const char *name;
    double ns;
    long checksum;
} result_t;

#define MAX_RESULTS 32
static result_t g_results[MAX_RESULTS];
static int g_n_results = 0;

static void record(const char *name, double seconds, long checksum)
{
    result_t *r = &g_results[g_n_results++];
    r->name = name;
    r->ns = seconds * 1e9 / (double)TRI_ITER;
    r->checksum = checksum;
    printf("BENCH tri_vs_bin name=%s n=%d seconds=%.6f ns_per_op=%.3f checksum=%ld\n",
           name, TRI_ITER, seconds, r->ns, checksum);
}

/* ------------------------------------------------------------------ */
/*  Bare digit-wise adder (no trace): the pure schoolbook algorithm.    */
/*  Same carry rule as tools/ttrace_add.h (verified there); the trace   */
/*  recording is left out so the number shows the algorithm, not the    */
/*  instrumentation. A one-time cross-check below pins them together.   */
/* ------------------------------------------------------------------ */

static int64_t bare_digit_add(int64_t a, int64_t b, int width, int8_t *final_carry)
{
    int8_t carry = 0;
    int i;
    int8_t da[BT_MAX_WIDTH], db[BT_MAX_WIDTH];
    (void)bt_digits(a, width, da);
    (void)bt_digits(b, width, db);
    for (i = 0; i < width; ++i) {
        int32_t s = (int32_t)da[i] + (int32_t)db[i] + (int32_t)carry;
        carry = (int8_t)bt_floor_div3(s + 1);
    }
    *final_carry = carry;
    return a + b;   /* the digit loop IS the computation; value is exact */
}

/* Binary saturating clamp with the SAME code shape as tvm_clamp, so the
 * 16-bit binary loop and the ternary value-domain loop compile to the same
 * machine code and the difference (if any) is measurement noise -- which is
 * itself the honest finding: the VM's "ternary" value-domain arithmetic IS
 * 16-bit binary arithmetic with a clamp. */
static int16_t bin_clamp(int32_t v)
{
    if (v > 9841) return 9841;
    if (v < -9841) return -9841;
    return (int16_t)v;
}

static void cross_check_bare_vs_header(void)
{
    /* the timed loop's adder must agree with the verified header version */
    const int64_t cand[] = { 0, 1, -1, 42, -42, 6561, -6562, 9841, -9841 };
    int i, j;
    for (i = 0; i < 9; ++i)
        for (j = 0; j < 9; ++j) {
            int8_t fc;
            bt_trace_t t = bt_add_trace(cand[i], cand[j], 9);
            (void)bare_digit_add(cand[i], cand[j], 9, &fc);
            if (fc != t.final_carry) {
                fprintf(stderr, "bare digit adder disagrees with ttrace_add.h "
                        "at %lld+%lld\n", (long long)cand[i], (long long)cand[j]);
                exit(1);
            }
        }
}

/* ------------------------------------------------------------------ */
/*  Measurement loops                                                   */
/* ------------------------------------------------------------------ */

static void bench_all(void)
{
    volatile int16_t acc16 = 0;
    volatile uint16_t acc16b = 0;
    volatile int32_t acc32 = 0;
    volatile int64_t acc64 = 0;
    volatile tvm_tryte_t tacc = 0;
    volatile tvm_code_t packed = TVM_TRYTE_BIAS;   /* packed 0 */
    volatile tvm_code_t enc_sink = 0;
    int i;
    double t0, t1;

    /* ---- add ---- */
    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i)
        tacc = tvm_clamp((int32_t)tacc + (tvm_tryte_t)((i % 2001) - 1000));
    t1 = now_seconds();
    record("tri_add_value_saturating", t1 - t0, (long)tacc);

    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i) {
        int32_t s = (int32_t)acc16 + (int16_t)((i % 2001) - 1000);
        acc16 = bin_clamp(s);   /* identical code shape to tvm_clamp */
    }
    t1 = now_seconds();
    record("bin_add_16bit_saturating", t1 - t0, (long)acc16);

    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i)
        acc16b = (uint16_t)((uint16_t)acc16b + (uint16_t)((i % 2001) - 1000));
    t1 = now_seconds();
    record("bin_add_16bit_wrapping", t1 - t0, (long)acc16b);

    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i)
        acc32 += (int32_t)((i % 2001) - 1000);
    t1 = now_seconds();
    record("bin_add_32bit_wrapping", t1 - t0, (long)acc32);

    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i)
        acc64 += (int64_t)((i % 2001) - 1000);
    t1 = now_seconds();
    record("bin_add_64bit_wrapping", t1 - t0, (long)acc64);

    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i) {
        tvm_tryte_t a = tvm_decode(packed);
        a = tvm_clamp((int32_t)a + (tvm_tryte_t)((i % 2001) - 1000));
        packed = tvm_encode(a);
    }
    t1 = now_seconds();
    record("tri_add_packed_decode_encode", t1 - t0, (long)tvm_decode(packed));

    {
        int8_t fc = 0;
        int64_t sum = 0;
        t0 = now_seconds();
        for (i = 0; i < TRI_ITER; ++i) {
            sum = bare_digit_add(sum, (i % 2001) - 1000, 9, &fc);
            sum += fc;   /* consume the carry: the whole chain is live */
        }
        t1 = now_seconds();
        record("tri_add_digitwise_no_trace", t1 - t0, (long)sum);
        g_sink += (long)sum;
    }

    {
        /* real dependency chain: acc <- traced_add(acc, x) + final carry;
         * acc stays small over this input pattern, so the compiler cannot
         * skip iterations and the trace record is materialised. */
        int64_t acc = 0;
        bt_trace_t t;
        t0 = now_seconds();
        for (i = 0; i < TRI_ITER; ++i) {
            t = bt_add_trace(acc, (i % 2001) - 1000, 9);
            acc = t.value + t.final_carry + t.overflow;
        }
        t1 = now_seconds();
        record("tri_add_digitwise_traced", t1 - t0, (long)acc);
        g_sink += (long)acc;
    }

    /* ---- sub ---- */
    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i)
        tacc = tvm_clamp((int32_t)tacc - (tvm_tryte_t)((i % 2001) - 1000));
    t1 = now_seconds();
    record("tri_sub_value_saturating", t1 - t0, (long)tacc);

    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i) {
        int32_t s = (int32_t)acc16 - (int16_t)((i % 2001) - 1000);
        acc16 = bin_clamp(s);
    }
    t1 = now_seconds();
    record("bin_sub_16bit_saturating", t1 - t0, (long)acc16);

    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i)
        acc32 -= (int32_t)((i % 2001) - 1000);
    t1 = now_seconds();
    record("bin_sub_32bit_wrapping", t1 - t0, (long)acc32);

    /* ---- mul ---- */
    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i)
        tacc = tvm_clamp((int32_t)tacc * (tvm_tryte_t)((i % 19) - 9));
    t1 = now_seconds();
    record("tri_mul_value_saturating", t1 - t0, (long)tacc);

    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i) {
        int32_t s = (int32_t)acc16 * (int16_t)((i % 19) - 9);
        acc16 = bin_clamp(s);
    }
    t1 = now_seconds();
    record("bin_mul_16bit_saturating", t1 - t0, (long)acc16);

    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i)
        acc64 = (int64_t)((i % 19) - 9) * (int64_t)((i % 17) - 8);
    t1 = now_seconds();
    record("bin_mul_64bit_wrapping", t1 - t0, (long)acc64);

    /* ---- codec cost alone (the "стоимость кодирования" half) ---- */
    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i)
        enc_sink = tvm_encode((tvm_tryte_t)((i % 2001) - 1000));
    t1 = now_seconds();
    record("codec_encode_only", t1 - t0, (long)enc_sink);

    t0 = now_seconds();
    for (i = 0; i < TRI_ITER; ++i)
        enc_sink = tvm_decode((tvm_code_t)(i % 19683));
    t1 = now_seconds();
    record("codec_decode_only", t1 - t0, (long)enc_sink);
}

static double log3_ish(double x);

/* ------------------------------------------------------------------ */
/*  Trit/bit count facts (computed, not invented)                       */
/* ------------------------------------------------------------------ */

static void print_count_facts(void)
{
    printf("# trit-vs-bit counts: 3^9 = 19683 values (9 trits, [-9841,+9841])\n");
    printf("# log2(19683) = %.3f bits -> a 9-trit tryte carries the information\n",
           log2(19683.0));
    printf("# of ~14.27 bits; the smallest binary type that holds it is int16\n");
    printf("# (16 bits, 65536 codes, of which a tryte uses %.1f%%).\n",
           100.0 * 19683.0 / 65536.0);
    printf("# values up to 10^k need ceil(log3(2*10^k+1)) trits vs\n");
    printf("# ceil(log2(2*10^k+1)) bits; e.g. 10^6: %d trits vs %d bits,\n",
           (int)ceil(log3_ish(2000001)), (int)ceil(log2(2000001.0)));
}

static double log3_ish(double x) { return log2(x) / log2(3.0); }

int main(void)
{
    double t0, t1;

    printf("# ternary-vs-binary arithmetic cost (bench/bench_tri_vs_bin.c)\n");
    printf("# compiler: "
#if defined(__GNUC__)
           "GCC " __VERSION__
#else
           "unknown"
#endif
           "\n");
    printf("# build: TRI_ITER=%d\n", TRI_ITER);
    cross_check_bare_vs_header();

    t0 = now_seconds();
    bench_all();
    t1 = now_seconds();

    print_count_facts();

    printf("# ratios (computed from the numbers above):\n");
    {
        int i;
        double tri_v = 0, bin_s = 0, dw = 0, dw_t = 0, pk = 0;
        for (i = 0; i < g_n_results; ++i) {
            if (strcmp(g_results[i].name, "tri_add_value_saturating") == 0) tri_v = g_results[i].ns;
            if (strcmp(g_results[i].name, "bin_add_16bit_saturating") == 0) bin_s = g_results[i].ns;
            if (strcmp(g_results[i].name, "tri_add_digitwise_no_trace") == 0) dw = g_results[i].ns;
            if (strcmp(g_results[i].name, "tri_add_digitwise_traced") == 0) dw_t = g_results[i].ns;
            if (strcmp(g_results[i].name, "tri_add_packed_decode_encode") == 0) pk = g_results[i].ns;
        }
        if (bin_s > 0 && tri_v > 0)
            printf("#   ternary value-domain add vs binary 16-bit saturating add: %.2fx\n", tri_v / bin_s);
        if (bin_s > 0 && dw > 0)
            printf("#   digit-wise (untraced) add vs binary 16-bit saturating add: %.2fx\n", dw / bin_s);
        if (dw > 0)
            printf("#   traced vs untraced digit-wise add: %.2fx\n", dw_t / dw);
        if (tri_v > 0 && pk > 0)
            printf("#   packed-domain vs value-domain ternary add: %.2fx (the pack/unpack tax)\n", pk / tri_v);
    }

    printf("# HOST: x86-64 host CPU, NOT an MCU. Wall-clock time only.\n");
    printf("# Эти числа НЕ дают энергетических выводов: упаковка троичных\n");
    printf("# трайтов в биты не гарантирует энергетической эффективности;\n");
    printf("# сравнение с бинарными системами по энергии — гипотеза,\n");
    printf("# требующая измерений на железе.\n");
    printf("# total bench wall time: %.3f s (sink=%ld)\n", t1 - t0, g_sink);
    return 0;
}
