/*
 * ttrace_add.h -- digit-wise balanced-ternary arithmetic with an explicit
 * carry/borrow trace (the research layer of the lab).
 *
 * The VM core (include/ternary_vm.h) executes arithmetic in the *value*
 * domain: a tryte is an int16_t and ADD/SUB/MUL are ordinary integer
 * operations followed by tvm_clamp(). That is fast, but it hides the thing
 * balanced ternary is studied for: how carry propagates from trit to trit.
 * This header implements the same operations *digit-wise*, one trit at a
 * time, and records the incoming/outgoing carry (or borrow) of every
 * position so a tracer can print them.
 *
 * Everything here is pure C99, allocation-free and exhaustively verified by
 * tests/property_test.c against plain int64 arithmetic:
 *
 *   add: for every pair of operands the digit-wise result (plus the final
 *        carry out of the most significant trit) equals a + b exactly.
 *   sub: for every pair the digit-wise result (minus the final borrow) equals
 *        a - b exactly. The borrow is *signed* and takes values in
 *        {-1, 0, +1}: a borrow of -1 is a carry, which has no schoolbook
 *        analogue in binary subtraction.
 *   mul: shift-and-add over the digits of b; each partial add is verified.
 *   neg: digit-wise negation never produces a carry (a balanced-ternary
 *        property the binary world does not have).
 *
 * Per-position rules (trits a, b in {-1,0,+1}; c_in in {-1,0,+1}):
 *
 *   add: s = a + b + c_in            in [-3, +3]
 *        c_out = floor((s + 1) / 3)  in {-1, 0, +1}
 *        r     = s - 3 * c_out       in {-1, 0, +1}
 *        invariant: s = r + 3 * c_out
 *
 *   sub: s = a - b - c_in            in [-3, +2]
 *        c_out = floor((1 - s) / 3)  in {-1, 0, +1}
 *        r     = s + 3 * c_out       in {-1, 0, +1}
 *        invariant: s = r - 3 * c_out
 *
 * Width: up to BT_MAX_WIDTH trits (18 keeps every partial product of the
 * shift-and-add multiplier inside int64_t). The default width of this
 * repository is 9 trits (one tryte, range [-9841, +9841]).
 *
 * Licence: Apache-2.0 (same as the repository).
 */

#ifndef TTRACE_ADD_H
#define TTRACE_ADD_H

#include <stdint.h>
#include <stddef.h>

/** @brief Maximum supported width in trits (3^18 ~ 3.9e8; partial products
 *         of mul stay inside int64_t). */
#define BT_MAX_WIDTH 18

/** @brief One digit-wise addition step: result trit and outgoing carry. */
typedef struct {
    int8_t a_trit;    /**< trit of the left operand at this position      */
    int8_t b_trit;    /**< trit of the right operand at this position     */
    int8_t carry_in;  /**< incoming carry, {-1, 0, +1}                    */
    int8_t result;    /**< result trit, {-1, 0, +1}                       */
    int8_t carry_out; /**< outgoing carry, {-1, 0, +1}                    */
} bt_step_t;

/** @brief Full trace of one digit-wise operation. */
typedef struct {
    int width;               /**< number of trits traced (<= BT_MAX_WIDTH) */
    bt_step_t step[BT_MAX_WIDTH];
    int8_t final_carry;      /**< carry/borrow out of the top trit         */
    int overflow;            /**< 1 if the final carry is non-zero, i.e. the
                                  exact result does not fit in @p width
                                  trits (the VM would saturate instead)    */
    int64_t value;           /**< exact mathematical result (a+b or a-b)   */
    int64_t clamped;         /**< result clamped into the width's range,
                                  i.e. what the VM would store             */
} bt_trace_t;

/** @brief One shift-and-add step of the digit-wise multiplier. */
typedef struct {
    int digit_index;     /**< position i of the multiplier trit           */
    int8_t mult_trit;    /**< the multiplier trit b_i                     */
    int64_t partial;     /**< value added in this step (a * b_i * 3^i)    */
    int64_t acc_before;  /**< accumulator value before the step           */
    int64_t acc_after;   /**< accumulator value after the step            */
    int overflow;        /**< accumulator left the width's range          */
    bt_trace_t add;      /**< the digit-wise add that produced acc_after  */
} bt_mul_step_t;

/** @brief Result of the digit-wise multiplication trace. */
typedef struct {
    int width;
    int n_steps;         /**< number of non-zero multiplier trits         */
    bt_mul_step_t step[BT_MAX_WIDTH];
    int64_t value;       /**< exact mathematical product a * b            */
    int overflow;        /**< product does not fit in @p width trits      */
    int64_t clamped;     /**< product clamped into the width's range      */
} bt_mul_trace_t;

/* ------------------------------------------------------------------ */
/*  Digit conversion                                                    */
/* ------------------------------------------------------------------ */

/** @brief Floor division by 3 (C's / truncates toward zero, which is wrong
 *         for negative intermediates; Python's // is floor). x in [-2, 4]. */
static inline int32_t bt_floor_div3(int32_t x)
{
    return (x >= 0) ? x / 3 : -((-x + 2) / 3);
}

/**
 * @brief Split @p v into @p width balanced-ternary digits (trit 0 = 3^0).
 * @return 0 if v is exactly representable, 1 otherwise (the top digits are
 *         those of the nearest representable value; |v| is clamped).
 */
static inline int bt_digits(int64_t v, int width, int8_t *out)
{
    int64_t lim = 0;
    int i, over = 0;
    int64_t half = 1;
    if (width > BT_MAX_WIDTH) width = BT_MAX_WIDTH;
    for (i = 0; i < width; ++i) half *= 3;
    lim = (half - 1) / 2;                 /* (3^width - 1) / 2 */
    if (v > lim) { v = lim; over = 1; }
    if (v < -lim) { v = -lim; over = 1; }
    for (i = 0; i < width; ++i) {
        int64_t d = v % 3;
        if (d < 0) d += 3;                /* C % truncates: -5 % 3 == -2 */
        if (d == 2) d = -1;               /* balanced digit of v mod 3 */
        out[i] = (int8_t)d;
        v = (v - d) / 3;                  /* v - d is divisible by 3 */
    }
    return over;
}

/** @brief Assemble digits (trit 0 = 3^0) back into a value. */
static inline int64_t bt_from_digits(const int8_t *digs, int width)
{
    int64_t v = 0, place = 1;
    int i;
    for (i = 0; i < width; ++i) {
        v += (int64_t)digs[i] * place;
        place *= 3;
    }
    return v;
}

/** @brief Clamp a value into the range of @p width trits (VM semantics). */
static inline int64_t bt_clamp(int64_t v, int width)
{
    int64_t half = 1, lim;
    int i;
    for (i = 0; i < width; ++i) half *= 3;
    lim = (half - 1) / 2;
    if (v > lim) return lim;
    if (v < -lim) return -lim;
    return v;
}

/** @brief Print a trit as '-1/0/+1' style sign character for compact tables. */
static inline char bt_char(int8_t t)
{
    switch (t) {
    case 1:  return '+';
    case -1: return '-';
    default: return '0';
    }
}

/* ------------------------------------------------------------------ */
/*  Digit-wise operations with traces                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Digit-wise addition a + b over @p width trits, recording every
 *        incoming/outgoing carry. Overflow (non-zero final carry) is
 *        reported, not hidden: the tracer shows it, the VM saturates.
 */
static inline bt_trace_t bt_add_trace(int64_t a, int64_t b, int width)
{
    bt_trace_t t;
    int8_t da[BT_MAX_WIDTH], db[BT_MAX_WIDTH];
    int i;
    int8_t carry = 0;

    t.width = width;
    t.overflow = 0;
    (void)bt_digits(a, width, da);
    (void)bt_digits(b, width, db);
    for (i = 0; i < width; ++i) {
        int32_t s = (int32_t)da[i] + (int32_t)db[i] + (int32_t)carry;
        int8_t cout = (int8_t)bt_floor_div3(s + 1);   /* floor((s+1)/3) */
        t.step[i].a_trit = da[i];
        t.step[i].b_trit = db[i];
        t.step[i].carry_in = carry;
        t.step[i].result = (int8_t)(s - 3 * (int32_t)cout);
        t.step[i].carry_out = cout;
        carry = cout;
    }
    t.final_carry = carry;
    t.overflow = (carry != 0);
    t.value = a + b;
    t.clamped = bt_clamp(a + b, width);
    return t;
}

/**
 * @brief Digit-wise subtraction a - b over @p width trits with a *signed*
 *        borrow in {-1, 0, +1}: an outgoing borrow of -1 is a carry (this
 *        happens when a position has a surplus, e.g. (+1) - (-1)), which
 *        binary schoolbook subtraction never produces.
 */
static inline bt_trace_t bt_sub_trace(int64_t a, int64_t b, int width)
{
    bt_trace_t t;
    int8_t da[BT_MAX_WIDTH], db[BT_MAX_WIDTH];
    int i;
    int8_t borrow = 0;

    t.width = width;
    t.overflow = 0;
    (void)bt_digits(a, width, da);
    (void)bt_digits(b, width, db);
    for (i = 0; i < width; ++i) {
        int32_t s = (int32_t)da[i] - (int32_t)db[i] - (int32_t)borrow;
        int8_t bout = (int8_t)bt_floor_div3(1 - s);   /* floor((1-s)/3) */
        t.step[i].a_trit = da[i];
        t.step[i].b_trit = db[i];
        t.step[i].carry_in = borrow;
        t.step[i].result = (int8_t)(s + 3 * (int32_t)bout);
        t.step[i].carry_out = bout;
        borrow = bout;
    }
    /* a - b = R - borrow_out * 3^width, so a non-zero top borrow means the
     * exact result does not fit: for subtraction this is exactly the sign of
     * "b was bigger than a" (borrow 1) or "a was bigger" (borrow -1). */
    t.final_carry = borrow;
    t.overflow = (borrow != 0);
    t.value = a - b;
    t.clamped = bt_clamp(a - b, width);
    return t;
}

/**
 * @brief Digit-wise negation: every trit flips sign, no carry exists.
 *        Negation is an involution and never overflows on a balanced digit
 *        set -- the trace exists so a tracer can show the digit symmetry.
 */
static inline bt_trace_t bt_neg_trace(int64_t a, int width)
{
    bt_trace_t t;
    int8_t da[BT_MAX_WIDTH];
    int i;

    t.width = width;
    (void)bt_digits(a, width, da);
    for (i = 0; i < width; ++i) {
        t.step[i].a_trit = da[i];
        t.step[i].b_trit = 0;
        t.step[i].carry_in = 0;
        t.step[i].result = (int8_t)(-da[i]);
        t.step[i].carry_out = 0;
    }
    t.final_carry = 0;
    t.overflow = 0;
    t.value = -a;
    t.clamped = bt_clamp(-a, width);
    return t;
}

/**
 * @brief Digit-wise multiplication as shift-and-add over the trits of @p b:
 *        for every non-zero trit b_i, add a * b_i * 3^i into the accumulator.
 *        Each step's addition is traced (carries included).
 */
static inline bt_mul_trace_t bt_mul_trace(int64_t a, int64_t b, int width)
{
    bt_mul_trace_t m;
    int8_t da[BT_MAX_WIDTH], db[BT_MAX_WIDTH];
    int64_t acc = 0;
    int i, n = 0;
    /* Partial products reach ~3^(2*width), so every shift-and-add step is
     * traced at double width: there the digits are exact and the step's
     * overflow flag actually means something. */
    int tw = 2 * width;
    if (tw > BT_MAX_WIDTH) tw = BT_MAX_WIDTH;

    m.width = width;
    m.n_steps = 0;
    m.value = a * b;
    m.overflow = 0;
    m.clamped = bt_clamp(a * b, width);
    (void)bt_digits(a, width, da);
    (void)bt_digits(b, width, db);
    for (i = 0; i < width; ++i) {
        int64_t partial, before;
        bt_trace_t t;
        if (db[i] == 0) continue;
        before = acc;
        partial = a * (int64_t)db[i];
        {
            int k;
            for (k = 0; k < i; ++k) partial *= 3;   /* partial = a * b_i * 3^i */
        }
        t = bt_add_trace(acc, partial, tw);
        acc = t.value;
        m.step[n].digit_index = i;
        m.step[n].mult_trit = db[i];
        m.step[n].partial = partial;
        m.step[n].acc_before = before;
        m.step[n].acc_after = acc;
        m.step[n].overflow = (acc != bt_clamp(acc, width));
        m.step[n].add = t;
        n++;
        if (m.step[n - 1].overflow) m.overflow = 1;
    }
    m.n_steps = n;
    return m;
}

/**
 * @brief Compare two values by their balanced-ternary digits, most
 *        significant trit first (the way a ternary ALU would compare).
 * @return -1, 0 or +1; guaranteed to match int64 comparison (tested).
 */
static inline int bt_compare(int64_t a, int64_t b, int width)
{
    int8_t da[BT_MAX_WIDTH], db[BT_MAX_WIDTH];
    int i;
    (void)bt_digits(a, width, da);
    (void)bt_digits(b, width, db);
    for (i = width - 1; i >= 0; --i) {
        if (da[i] != db[i]) return (da[i] < db[i]) ? -1 : 1;
    }
    return 0;
}

#endif /* TTRACE_ADD_H */
