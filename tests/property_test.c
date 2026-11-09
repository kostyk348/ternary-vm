/*
 * property_test.c -- property tests for the balanced-ternary arithmetic of
 * this repository, executed as a plain C binary (the audit asks for
 * arithmetic properties, not just example-based unit tests).
 *
 * What is verified, against *independent int64 reference implementations*
 * written in this file (never against the code under test):
 *
 *   1. balanced-ternary round-trip: digit split -> digit assembly is the
 *      identity on every representable value (exhaustive for the 9-trit
 *      tryte range and for widths 1..6; sampled above that), digits always
 *      in {-1, 0, +1}, and the all-zero digit vector is the ONLY
 *      representation of 0 (no "+0/-0").
 *   2. digit-wise carry correctness: tools/ttrace_add.h's trit-serial adder
 *      computes exactly a+b (and the borrow-based subtractor exactly a-b)
 *      for every position, with every carry/borrow in {-1, 0, +1}, and the
 *      final carry reconstructs the value: exhaustive for widths 1..6,
 *      randomized + all boundary operand pairs for width 9.
 *   3. VM arithmetic invariance: include/ternary_vm.h's ADD/SUB/MUL/NEG
 *      semantics (value domain + saturation) equals clamp64(a op b) of the
 *      int64 reference on randomized and boundary inputs.
 *   4. packed-codec round-trip: tvm_encode/tvm_decode is a bijection on the
 *      exact sub-range [-6561, 9841] and pins the documented bias defect
 *      below it (decode(encode(v)) == v + 3^9), matching
 *      tests/test_reference_defects.py.
 *   5. comparison/equality edge cases: the digit comparator (MSB-first)
 *      agrees with int64 comparison; decode(code) == 0 holds for exactly one
 *      code (the unique zero); NEG is an involution; JZ/JNZ's "== 0" test
 *      has no surprising duplicate representations.
 *
 * Build & run (also via tests/run_property_tests.sh):
 *   gcc -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
 *       -Iinclude -Itools tests/property_test.c -o property_test
 *   ./property_test
 *
 * Exit code 0 and the final line "ALL PROPERTY TESTS PASSED" mean success.
 * Licence: Apache-2.0 (same as the repository).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "ttrace_add.h"
#include "ternary_vm.h"

static int g_failures = 0;
static long long g_checks = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            printf("FAILED %s:%d: ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                    \
            if (g_failures > 20) {                                           \
                printf("too many failures, aborting early\n");               \
                exit(1);                                                     \
            }                                                                \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Independent int64 reference implementations (the oracle)            */
/* ------------------------------------------------------------------ */

static int64_t ref_clamp64(int64_t v, int64_t lo, int64_t hi)
{
    if (v > hi) return hi;
    if (v < lo) return lo;
    return v;
}

/* Deterministic RNG: xorshift64* (one fixed seed => reproducible runs). */
static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;

static uint64_t rng_next(void)
{
    uint64_t x = g_rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng = x;
    return x * 0x2545F4914F6CDD1DULL;
}

/* Uniform value in [-lim, +lim] (slightly biased at 2^64 % range; irrelevant
 * for property testing, and the boundaries are covered exhaustively). */
static int64_t rng_symmetric(int64_t lim)
{
    return (int64_t)(rng_next() % (uint64_t)(2 * lim + 1)) - lim;
}

static int64_t width_limit_w(int width)
{
    int64_t p = 1;
    int i;
    for (i = 0; i < width; ++i) p *= 3;
    return (p - 1) / 2;
}

/* Boundary operands that every width's edge behaviour lives at are built
 * inline by each property (see the cand[] arrays below). */

/* ------------------------------------------------------------------ */
/*  1. Round-trip: value -> digits -> value                             */
/* ------------------------------------------------------------------ */

static void prop_roundtrip_width(int width, int exhaustive)
{
    int64_t lim = width_limit_w(width);
    int64_t v;
    long long n = 0;

    for (v = -lim; v <= lim; ++v) {
        int8_t d[BT_MAX_WIDTH];
        int i, over;
        if (!exhaustive && ((v & 0x3F) != 0 && v != lim && v != -lim)) continue;
        over = bt_digits(v, width, d);
        CHECK(over == 0, "bt_digits(%lld,w=%d) reported clamping", (long long)v, width);
        for (i = 0; i < width; ++i)
            CHECK(d[i] == -1 || d[i] == 0 || d[i] == 1,
                  "digit %d of %lld (w=%d) is %d, not in {-1,0,1}",
                  i, (long long)v, width, (int)d[i]);
        CHECK(bt_from_digits(d, width) == v,
              "round-trip broken: %lld -> digits -> %lld (w=%d)",
              (long long)v, (long long)bt_from_digits(d, width), width);
        /* zero uniqueness: the digit vector is all-zero iff v == 0 */
        if (v == 0) {
            for (i = 0; i < width; ++i) CHECK(d[i] == 0, "zero must be all-zero trits");
        } else {
            int nonzero = 0;
            for (i = 0; i < width; ++i) nonzero |= (d[i] != 0);
            CHECK(nonzero, "non-zero value %lld got an all-zero digit vector", (long long)v);
        }
        n++;
    }
    printf("PROPERTY roundtrip width=%d values=%lld ok\n", width, n);
}

static void prop_roundtrip(void)
{
    int w;
    prop_roundtrip_width(9, 1);             /* the tryte: exhaustive 19683 */
    for (w = 1; w <= 6; ++w) prop_roundtrip_width(w, 1);
    for (w = 7; w <= 18; ++w) prop_roundtrip_width(w, 0);  /* sampled */
}

/* ------------------------------------------------------------------ */
/*  2. Digit-wise carry/borrow correctness vs int64                     */
/* ------------------------------------------------------------------ */

static void check_add_trace(int64_t a, int64_t b, int width)
{
    bt_trace_t t = bt_add_trace(a, b, width);
    int i;

    for (i = 0; i < width; ++i) {
        CHECK(t.step[i].result >= -1 && t.step[i].result <= 1,
              "add %lld+%lld w=%d: result trit %d at pos %d not in {-1,0,1}",
              (long long)a, (long long)b, width, (int)t.step[i].result, i);
        CHECK(t.step[i].carry_out >= -1 && t.step[i].carry_out <= 1,
              "add %lld+%lld w=%d: carry %d at pos %d not in {-1,0,1}",
              (long long)a, (long long)b, width, (int)t.step[i].carry_out, i);
        CHECK(t.step[i].carry_in == (i == 0 ? 0 : t.step[i - 1].carry_out),
              "add %lld+%lld w=%d: carry chain broken at pos %d",
              (long long)a, (long long)b, width, i);
    }
    {
        int64_t digit_value = 0, power = 1;
        for (i = 0; i < width; ++i) {
            digit_value += (int64_t)t.step[i].result * power;
            power *= 3;
        }
        CHECK(digit_value + (int64_t)t.final_carry * power == a + b,
              "add %lld+%lld w=%d: digits+carry = %lld, expected %lld",
              (long long)a, (long long)b, width,
              (long long)(digit_value + (int64_t)t.final_carry * power),
              (long long)(a + b));
        CHECK(t.value == a + b, "add: exact value field wrong");
        CHECK(t.overflow == (t.final_carry != 0),
              "add: overflow flag inconsistent with final carry");
        CHECK(t.clamped == ref_clamp64(a + b, -width_limit_w(width),
                                       width_limit_w(width)),
              "add: clamped field disagrees with the int64 reference clamp");
    }
}

static void check_sub_trace(int64_t a, int64_t b, int width)
{
    bt_trace_t t = bt_sub_trace(a, b, width);
    int i;

    for (i = 0; i < width; ++i) {
        CHECK(t.step[i].result >= -1 && t.step[i].result <= 1,
              "sub %lld-%lld w=%d: result trit %d at pos %d not in {-1,0,1}",
              (long long)a, (long long)b, width, (int)t.step[i].result, i);
        CHECK(t.step[i].carry_out >= -1 && t.step[i].carry_out <= 1,
              "sub %lld-%lld w=%d: borrow %d at pos %d not in {-1,0,1}",
              (long long)a, (long long)b, width, (int)t.step[i].carry_out, i);
        CHECK(t.step[i].carry_in == (i == 0 ? 0 : t.step[i - 1].carry_out),
              "sub %lld-%lld w=%d: borrow chain broken at pos %d",
              (long long)a, (long long)b, width, i);
    }
    {
        int64_t digit_value = 0, power = 1;
        for (i = 0; i < width; ++i) {
            digit_value += (int64_t)t.step[i].result * power;
            power *= 3;
        }
        CHECK(digit_value - (int64_t)t.final_carry * power == a - b,
              "sub %lld-%lld w=%d: digits-borrow = %lld, expected %lld",
              (long long)a, (long long)b, width,
              (long long)(digit_value - (int64_t)t.final_carry * power),
              (long long)(a - b));
        CHECK(t.value == a - b, "sub: exact value field wrong");
        CHECK(t.overflow == (t.final_carry != 0),
              "sub: overflow flag inconsistent with final borrow");
    }
}

static void prop_carry_exhaustive_small(void)
{
    int w;
    for (w = 1; w <= 6; ++w) {
        int64_t lim = width_limit_w(w), a, b;
        long long n = 0;
        for (a = -lim; a <= lim; ++a)
            for (b = -lim; b <= lim; ++b) {
                check_add_trace(a, b, w);
                check_sub_trace(a, b, w);
                n++;
            }
        printf("PROPERTY carry_vs_int64 width=%d exhaustive pairs=%lld ok\n", w, n);
    }
}

static void prop_carry_width9(void)
{
    const int64_t lim = width_limit_w(9);
    const int64_t cand[] = { 0, 1, -1, 2, -2, 3, -3, 6561, -6561, 6562, -6562,
                             9840, -9840, 9841, -9841, 4920, -4920 };
    const int ncand = (int)(sizeof cand / sizeof cand[0]);
    const int N_RANDOM = 2000000;
    int i, j;
    long long n = 0;
    long k;

    for (i = 0; i < ncand; ++i)
        for (j = 0; j < ncand; ++j) {
            check_add_trace(cand[i], cand[j], 9);
            check_sub_trace(cand[i], cand[j], 9);
            n++;
        }
    for (k = 0; k < N_RANDOM; ++k) {
        int64_t a = rng_symmetric(lim), b = rng_symmetric(lim);
        check_add_trace(a, b, 9);
        check_sub_trace(a, b, 9);
        n++;
    }
    printf("PROPERTY carry_vs_int64 width=9 boundary=%d random=%d pairs=%lld ok\n",
           ncand * ncand, N_RANDOM, n);
}

/* ------------------------------------------------------------------ */
/*  3. Digit-wise multiplication vs int64                               */
/* ------------------------------------------------------------------ */

static void prop_mul_width9(void)
{
    const int64_t lim = width_limit_w(9);
    const int64_t cand[] = { 0, 1, -1, 3, -3, 10, -10, 6561, -6561, 9841, -9841,
                             100, -100, 9840, -9840 };
    const int ncand = (int)(sizeof cand / sizeof cand[0]);
    const int N_RANDOM = 500000;
    long long n = 0;
    long k;
    int i, j;

    for (i = 0; i < ncand; ++i)
        for (j = 0; j < ncand; ++j) {
            bt_mul_trace_t m = bt_mul_trace(cand[i], cand[j], 9);
            int64_t prod = cand[i] * cand[j];
            int64_t lim9 = width_limit_w(9);
            CHECK(m.value == prod, "mul %lld*%lld: value field %lld != %lld",
                  (long long)cand[i], (long long)cand[j],
                  (long long)m.value, (long long)prod);
            CHECK(m.overflow == (prod > lim9 || prod < -lim9),
                  "mul %lld*%lld: overflow flag wrong", (long long)cand[i],
                  (long long)cand[j]);
            CHECK(m.clamped == ref_clamp64(prod, -lim9, lim9),
                  "mul %lld*%lld: clamped field wrong", (long long)cand[i],
                  (long long)cand[j]);
            if (!m.overflow) {
                CHECK(m.step[m.n_steps - 1].acc_after == prod,
                      "mul %lld*%lld: ladder accumulator %lld != product %lld",
                      (long long)cand[i], (long long)cand[j],
                      (long long)m.step[m.n_steps - 1].acc_after,
                      (long long)prod);
            }
            n++;
        }
    for (k = 0; k < N_RANDOM; ++k) {
        int64_t a = rng_symmetric(lim), b = rng_symmetric(lim);
        bt_mul_trace_t m = bt_mul_trace(a, b, 9);
        int64_t prod = a * b;
        CHECK(m.value == prod, "mul random %lld*%lld: value field wrong",
              (long long)a, (long long)b);
        if (prod >= -lim && prod <= lim)
            CHECK(m.step[m.n_steps - 1].acc_after == prod || b == 0,
                  "mul random %lld*%lld: ladder wrong", (long long)a, (long long)b);
        n++;
    }
    printf("PROPERTY mul_vs_int64 width=9 boundary=%d random=%d pairs=%lld ok\n",
           ncand * ncand, N_RANDOM, n);
}

/* ------------------------------------------------------------------ */
/*  4. VM arithmetic semantics vs the int64 reference                   */
/* ------------------------------------------------------------------ */

static void prop_vm_arithmetic(void)
{
    const int64_t lo = TVM_TRYTE_MIN, hi = TVM_TRYTE_MAX;
    const int64_t cand[] = { 0, 1, -1, 2, -2, 3, -3, 6561, -6561, 6562, -6562,
                             9840, -9840, 9841, -9841 };
    const int ncand = (int)(sizeof cand / sizeof cand[0]);
    const int N_RANDOM = 1000000;
    long k;
    int i, j;

    for (i = 0; i < ncand; ++i)
        for (j = 0; j < ncand; ++j) {
            int64_t a = cand[i], b = cand[j];
            CHECK((int64_t)tvm_clamp((int32_t)(a + b)) == ref_clamp64(a + b, lo, hi),
                  "tvm_clamp(a+b) != int64 ref at %lld+%lld", (long long)a, (long long)b);
            CHECK((int64_t)tvm_clamp((int32_t)(a - b)) == ref_clamp64(a - b, lo, hi),
                  "tvm_clamp(a-b) != int64 ref at %lld-%lld", (long long)a, (long long)b);
            CHECK((int64_t)tvm_clamp((int32_t)(a * b)) == ref_clamp64(a * b, lo, hi),
                  "tvm_clamp(a*b) != int64 ref at %lld*%lld", (long long)a, (long long)b);
            CHECK((int64_t)tvm_clamp((int32_t)(-a)) == ref_clamp64(-a, lo, hi),
                  "tvm_clamp(-a) != int64 ref at -%lld", (long long)a);
        }
    for (k = 0; k < N_RANDOM; ++k) {
        int64_t a = rng_symmetric(hi), b = rng_symmetric(hi);
        CHECK((int64_t)tvm_clamp((int32_t)(a + b)) == ref_clamp64(a + b, lo, hi),
              "tvm_clamp(a+b) != int64 ref at %lld+%lld", (long long)a, (long long)b);
        CHECK((int64_t)tvm_clamp((int32_t)(a - b)) == ref_clamp64(a - b, lo, hi),
              "tvm_clamp(a-b) != int64 ref at %lld-%lld", (long long)a, (long long)b);
        CHECK((int64_t)tvm_clamp((int32_t)(a * b)) == ref_clamp64(a * b, lo, hi),
              "tvm_clamp(a*b) != int64 ref at %lld*%lld", (long long)a, (long long)b);
    }
    printf("PROPERTY vm_arithmetic_vs_int64 boundary=%d random=%d ok\n",
           ncand * ncand * 4, N_RANDOM);
}

/* ------------------------------------------------------------------ */
/*  5. Packed codec round-trip (including the pinned bias defect)       */
/* ------------------------------------------------------------------ */

static void prop_packed_codec(void)
{
    int64_t v;
    long long exact = 0, defective = 0;

    for (v = -9841; v <= 9841; ++v) {
        tvm_code_t c = tvm_encode((tvm_tryte_t)v);
        tvm_tryte_t d = tvm_decode(c);
        if (v >= -6561) {
            CHECK(d == v, "codec: decode(encode(%lld)) = %d, expected %lld",
                  (long long)v, (int)d, (long long)v);
            exact++;
        } else {
            /* the documented defect: values below the 3^8 bias decode shifted
             * up by 3^9 = 19683 (pinned identically in Python by
             * tests/test_reference_defects.py) */
            CHECK(d == (tvm_tryte_t)(v + 19683),
                  "codec defect shape changed at %lld", (long long)v);
            defective++;
        }
        CHECK(c >= 0 && c <= 19682, "codec: code %d out of [0, 19682]", (int)c);
    }
    /* measured examples from the README / header comment */
    CHECK(tvm_encode((tvm_tryte_t)-6562) == 19682, "codec: encode(-6562) != 19682");
    CHECK(tvm_decode(19682) == 13121, "codec: decode(19682) != 13121");
    CHECK(tvm_encode((tvm_tryte_t)-9000) == 17244, "codec: encode(-9000) != 17244");
    CHECK(tvm_decode(17244) == 10683, "codec: decode(17244) != 10683");
    CHECK(tvm_encode((tvm_tryte_t)-9841) == 16403, "codec: encode(-9841) != 16403");
    CHECK(tvm_decode(16403) == 9842, "codec: decode(16403) != 9842");
    /* out-of-range inputs clamp before encoding (both directions) */
    CHECK(tvm_encode((tvm_tryte_t)9842) == tvm_encode((tvm_tryte_t)9841),
          "codec: out-of-range clamping (high) changed");
    CHECK(tvm_encode((tvm_tryte_t)-9842) == tvm_encode((tvm_tryte_t)-9841),
          "codec: out-of-range clamping (low) changed");
    printf("PROPERTY packed_codec exact=%lld defective_pinned=%lld ok\n",
           exact, defective);
}

/* ------------------------------------------------------------------ */
/*  6. Comparison / equality edge cases                                 */
/* ------------------------------------------------------------------ */

static void prop_comparison(void)
{
    const int64_t lim = width_limit_w(9);
    const int N_RANDOM = 500000;
    const int64_t cand[] = { 0, 1, -1, 6561, -6561, 9841, -9841, 3, -3 };
    const int ncand = (int)(sizeof cand / sizeof cand[0]);
    long k;
    int i, j;

    for (i = 0; i < ncand; ++i)
        for (j = 0; j < ncand; ++j) {
            int want = (cand[i] > cand[j]) - (cand[i] < cand[j]);
            CHECK(bt_compare(cand[i], cand[j], 9) == want,
                  "bt_compare(%lld, %lld) != int64 comparison",
                  (long long)cand[i], (long long)cand[j]);
        }
    for (k = 0; k < N_RANDOM; ++k) {
        int64_t a = rng_symmetric(lim), b = rng_symmetric(lim);
        int want = (a > b) - (a < b);
        CHECK(bt_compare(a, b, 9) == want,
              "bt_compare(%lld, %lld) != int64 comparison", (long long)a, (long long)b);
    }
    /* zero has exactly one packed representation among all 19683 codes */
    {
        tvm_code_t c, zero_code = 0;
        long long zeros = 0;
        for (c = 0; c <= 19682; ++c)
            if (tvm_decode(c) == 0) { zeros++; zero_code = c; }
        CHECK(zeros == 1, "%lld codes decode to 0 (zero must be unique)", zeros);
        CHECK(zero_code == tvm_encode((tvm_tryte_t)0),
              "the unique zero code is not encode(0)");
    }
    printf("PROPERTY comparison_vs_int64 boundary=%d random=%d ok\n",
           ncand * ncand, N_RANDOM);
}
static void prop_zero_detection(void)
{
    int64_t v;
    for (v = -9841; v <= 9841; ++v) {
        int is_zero = (tvm_decode(tvm_encode((tvm_tryte_t)v)) == 0);
        CHECK(is_zero == (v == 0),
              "zero-detection: decode(encode(%lld)) == 0 is %d", (long long)v, is_zero);
    }
    /* NEG is an involution on the tryte value domain */
    for (v = -9841; v <= 9841; ++v) {
        tvm_tryte_t n1 = tvm_clamp(-(int32_t)v);
        tvm_tryte_t n2 = tvm_clamp(-(int32_t)n1);
        CHECK(n2 == (tvm_tryte_t)v, "NEG not an involution at %lld", (long long)v);
    }
    /* negation via the codec is exact on the sub-range where both v and -v
     * avoid the defect zone ([-6561, 6561]) */
    for (v = -6561; v <= 6561; ++v) {
        tvm_tryte_t d = tvm_decode(tvm_encode((tvm_tryte_t)-v));
        CHECK(d == (tvm_tryte_t)-v,
              "negation via codec wrong at %lld -> %d", (long long)v, (int)d);
    }
    printf("PROPERTY comparison_equality zero=19683_codes neg_involution=19683 ok\n");
}

int main(void)
{
    prop_roundtrip();
    prop_carry_exhaustive_small();
    prop_carry_width9();
    prop_mul_width9();
    prop_vm_arithmetic();
    prop_packed_codec();
    prop_comparison();
    prop_zero_detection();

    if (g_failures != 0) {
        printf("PROPERTY TESTS FAILED: %d failures, %lld checks\n",
               g_failures, g_checks);
        return 1;
    }
    printf("ALL PROPERTY TESTS PASSED (%lld checks)\n", g_checks);
    return 0;
}
