/*
 * ternary_vm.h -- minimal balanced-ternary VM core for FPU-less MCUs.
 *
 * C99 single header, no external dependencies, no dynamic allocation.
 * This is the C counterpart announced in the README ("A future C port
 * (ternary_vm.h) is planned to run the same bytecode on FPU-less MCUs"),
 * and it executes the *same* bytecode format as the Python reference
 * runtime in vm/ternary_vm.py + vm/asm.py.
 *
 * Provenance
 * ----------
 * Museum record:  lost-tech-museum / digests/portfolio-review / id "ternary-vm"
 * Digest:         https://kostyk348.github.io/lost-tech-museum/digests/portfolio-review/
 * Recorded gap:   "заявленный ternary_vm.h для MCU не реализован -- это ключевая
 *                  незакрытая часть. Нет сравнения с MPTC по скорости."
 * Pitch:          "Довести до пары MPTC: троичный байткод, который исполняется
 *                  на MCU, плюс тулчейн (ассемблер + отладчик)."
 *
 * Data model (identical to the Python reference)
 * ----------------------------------------------
 * tryte = 9 trits, trit in {-1, 0, +1}, value range [-9841, +9841].
 * Registers      : R0..R7, each a tryte value.
 * Memory         : TVM_MEM_SIZE trytes, stored *packed*, i.e. mem[i] holds
 *                  tvm_encode(value) exactly like the Python STORE.
 * Program        : caller-provided array of tvm_code_t (packed trytes) living
 *                  in RAM or, better, in const/progmem/Flash. No copy is made.
 * Arithmetic     : saturating (clamping) into [-9841, +9841] -- "All
 *                  arithmetic clamps to the 9-trit range (saturation)".
 *
 * Packed tryte layout: code = sum(digit_i * 3^i) + 6561, digit_i in {0,1,2},
 * i.e. the least significant trit is the 3^0 place and a bias of 3^8 = 6561
 * keeps the code non-negative in [0, 19682]. Identical to
 * vm.ternary_vm.int_to_tryte_code / tryte_to_int.
 *
 * Known defect of the reference codec (reproduced here on purpose)
 * ----------------------------------------------------------------
 * The bias 3^8 = 6561 is too small for a 9-trit tryte: the value range is
 * [-(3^9-1)/2, +(3^9-1)/2] = [-9841, 9841], so the correct bias is
 * (3^9-1)/2 = 9841. As a result, for tryte values v in [-9841, -6562]
 * (3280 of the 19683 values) int_to_tryte_code(v) yields a code in
 * [16403, 19682] whose decoding is not v but a value above the tryte range:
 *
 *     encode(-6562) = 19682 -> decode = 13121   (wrong)
 *     encode(-9000) = 17244 -> decode = 10683   (wrong)
 *     encode(-9841) = 16403 -> decode =  9842   (wrong)
 *
 * Measured: 3280 round-trip failures out of 19683 values; the codec is exact
 * on [-6561, 9841]. Practical effect: `LOAD R0 <literal>` with a literal below
 * -6561 loads a wrong value; every program in programs/ stays inside the exact
 * sub-range, so no shipped program is affected.
 *
 * This header mirrors the reference bit-for-bit (including Python's floor
 * division semantics, hence the explicit tvm_floor_div/tvm_floor_mod) so the
 * same bytecode image produces the same state on both runtimes. Fixing the
 * bias belongs upstream in vm/ternary_vm.py; tests/test_reference_defects.py
 * pins the current behaviour so both sides must change together.
 *
 * Example
 * -------
 *     #include "ternary_vm.h"
 *     static const tvm_code_t prog[] = { TVM_OP_LOAD, 0, 6566,  // LOAD R0 5
 *                                        TVM_OP_LOAD, 1, 6568,  // LOAD R1 7
 *                                        TVM_OP_ADD,  0, 1,     // ADD  R0 R1
 *                                        TVM_OP_OUT,  0,        // OUT  R0
 *                                        TVM_OP_HALT };
 *     tvm_t vm;
 *     tvm_init(&vm, prog, (uint16_t)(sizeof prog / sizeof prog[0]));
 *     int rc = tvm_run(&vm, 1000);            // rc == TVM_OK
 *     tvm_tryte_t r0 = tvm_reg(&vm, 0);       // 12
 *     tvm_tryte_t o0 = tvm_out(&vm, 0);       // 12
 *
 * Licence: Apache-2.0 (same as the repository).
 */

#ifndef TERNARY_VM_H
#define TERNARY_VM_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Compile-time configuration (override before including this file)   */
/* ------------------------------------------------------------------ */

/** @brief Number of trits per tryte. Only 9 is supported by this release. */
#ifndef TVM_TRITS_PER_TRYTE
#define TVM_TRITS_PER_TRYTE 9
#endif

#if TVM_TRITS_PER_TRYTE != 9
#error "ternary_vm.h: only TVM_TRITS_PER_TRYTE == 9 is implemented"
#endif

/** @brief Number of general purpose registers (R0..R7). */
#ifndef TVM_REG_COUNT
#define TVM_REG_COUNT 8
#endif

/** @brief Memory size in trytes. */
#ifndef TVM_MEM_SIZE
#define TVM_MEM_SIZE 256
#endif

/** @brief Size of the ring-free output buffer (see tvm_out_total()). */
#ifndef TVM_OUT_SIZE
#define TVM_OUT_SIZE 64
#endif

/** @brief Default step limit for tvm_run(). */
#ifndef TVM_STEP_LIMIT
#define TVM_STEP_LIMIT 100000u
#endif

/* ------------------------------------------------------------------ */
/*  Types, constants, opcodes                                          */
/* ------------------------------------------------------------------ */

/** @brief A balanced-ternary value in [-9841, +9841]. */
typedef int16_t tvm_tryte_t;

/** @brief A packed 9-trit tryte in [0, 19682]. */
typedef int16_t tvm_code_t;

#define TVM_TRYTE_MAX   ((tvm_tryte_t)9841)  /* (3**9 - 1) / 2 */
#define TVM_TRYTE_MIN   ((tvm_tryte_t)-9841)
#define TVM_TRYTE_BIAS  ((tvm_code_t)6561)   /* 3**8           */
#define TVM_CODE_MAX    ((tvm_code_t)19682)  /* 3**9 - 1       */

/* Opcode numbers are frozen: they are the bytecode format shared with
 * vm/asm.py and vm/ternary_vm.py. */
#define TVM_OP_HALT   0
#define TVM_OP_LOAD   1
#define TVM_OP_STORE  2
#define TVM_OP_ADD    3
#define TVM_OP_SUB    4
#define TVM_OP_MUL    5
#define TVM_OP_NEG    6
#define TVM_OP_JMP    7
#define TVM_OP_JZ     8
#define TVM_OP_JNZ    9
#define TVM_OP_OUT    10
#define TVM_OP_NOP    11
#define TVM_OP_MOV    12

/* tvm_run() return codes. */
#define TVM_OK              0   /**< halted cleanly                       */
#define TVM_ERR_STEP_LIMIT  (-1) /**< step limit hit (possible infinite loop) */
#define TVM_ERR_BAD_OPCODE  (-2) /**< opcode outside the ISA               */
#define TVM_ERR_BAD_REG     (-3) /**< register index >= TVM_REG_COUNT      */

/* ------------------------------------------------------------------ */
/*  Machine state                                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief VM state. Contains every mutable byte: no allocation, no globals,
 *        so the whole machine can be a stack or .bss object on an MCU.
 */
typedef struct {
    const tvm_code_t *prog;      /**< caller-owned program (packed trytes)   */
    uint16_t prog_len;           /**< number of packed trytes in prog        */
    tvm_tryte_t reg[TVM_REG_COUNT]; /**< R0..R7                              */
    tvm_code_t mem[TVM_MEM_SIZE];   /**< packed tryte memory                 */
    uint16_t pc;                 /**< program counter (index into prog)      */
    uint32_t steps;              /**< instructions executed so far           */
    uint16_t out_len;            /**< values stored into out[]               */
    uint32_t out_total;          /**< OUT executions (may exceed out_len)    */
    tvm_tryte_t out[TVM_OUT_SIZE]; /**< OUT values, truncated at TVM_OUT_SIZE */
    int8_t halted;               /**< 1 after HALT or after a fault          */
    int8_t fault;                /**< 0 or one of TVM_ERR_*                  */
} tvm_t;

/* ------------------------------------------------------------------ */
/*  Tryte conversion (bit-identical to the Python reference)           */
/* ------------------------------------------------------------------ */

/* Python's // and % are *floor* division/modulo; C's are truncating, so
 * the reference algorithm has to be mirrored with explicit floor helpers.
 * Only negative intermediates differ, and they appear for tryte values
 * below -TVM_TRYTE_BIAS (see the defect note above). */
static inline int32_t tvm_floor_div(int32_t a, int32_t b)
{
    int32_t q = a / b;
    if ((a % b) != 0 && ((a < 0) != (b < 0))) q -= 1;
    return q;
}

static inline int32_t tvm_floor_mod(int32_t a, int32_t b)
{
    int32_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

/** @brief Clamp an integer into the 9-trit range [-9841, +9841]. */
static inline tvm_tryte_t tvm_clamp(int32_t v)
{
    if (v > (int32_t)TVM_TRYTE_MAX) return TVM_TRYTE_MAX;
    if (v < (int32_t)TVM_TRYTE_MIN) return TVM_TRYTE_MIN;
    return (tvm_tryte_t)v;
}

/**
 * @brief Encode a tryte value into a packed 9-trit code, out of range clamps.
 *
 * Mirrors vm.ternary_vm.int_to_tryte_code exactly, including its defect: the
 * bias is 3^8 = 6561, so values in [-9841, -6562] produce codes in
 * [16403, 19682] that decode back to values outside the tryte range. See the
 * "Known defect" note at the top of this header.
 */
static inline tvm_code_t tvm_encode(tvm_tryte_t v)
{
    int32_t x = (int32_t)tvm_clamp((int32_t)v) + (int32_t)TVM_TRYTE_BIAS;
    int32_t code = 0;
    int32_t place = 1;
    int i;
    for (i = 0; i < TVM_TRITS_PER_TRYTE; ++i) {
        code += tvm_floor_mod(x, 3) * place;
        x = tvm_floor_div(x, 3);
        place *= 3;
    }
    return (tvm_code_t)code;
}

/** @brief Decode a packed 9-trit code back into a tryte value. */
static inline tvm_tryte_t tvm_decode(tvm_code_t code)
{
    int32_t x = (int32_t)code;
    int32_t val = 0;
    int32_t place = 1;
    int i;
    for (i = 0; i < TVM_TRITS_PER_TRYTE; ++i) {
        val += tvm_floor_mod(x, 3) * place;
        x = tvm_floor_div(x, 3);
        place *= 3;
    }
    return (tvm_tryte_t)(val - (int32_t)TVM_TRYTE_BIAS);
}

/** @brief Extract trit @p pos (0 = least significant) of a packed tryte: -1/0/+1. */
static inline int8_t tvm_trit_at(tvm_code_t code, uint8_t pos)
{
    int32_t place = 1;
    int i;
    for (i = 0; i < (int)pos; ++i) place *= 3;
    return (int8_t)(tvm_floor_mod(tvm_floor_div((int32_t)code, place), 3) - 1);
}

/* ------------------------------------------------------------------ */
/*  Lifecycle and inspection                                           */
/* ------------------------------------------------------------------ */

/** @brief Reset @p vm and attach a program. @p prog must outlive @p vm. */
static inline void tvm_init(tvm_t *vm, const tvm_code_t *prog, uint16_t prog_len)
{
    int i;
    vm->prog = prog;
    vm->prog_len = prog_len;
    for (i = 0; i < TVM_REG_COUNT; ++i) vm->reg[i] = 0;
    /* Memory starts as all-zero *codes*, exactly like the Python reference
     * (`self.mem = [0] * mem_size`). Note that code 0 decodes to the value
     * -6561, not to 0 -- the reference initialises memory to a raw pattern
     * rather than to "tryte zero" (+6561). Reproduced bit-for-bit here so the
     * two runtimes agree; tvm_mem_read() shows the real value. */
    for (i = 0; i < TVM_MEM_SIZE; ++i) vm->mem[i] = 0;
    vm->pc = 0;
    vm->steps = 0;
    vm->out_len = 0;
    vm->out_total = 0;
    vm->halted = 0;
    vm->fault = 0;
}

/** @brief Read register @p r as a value; out-of-range indices return 0. */
static inline tvm_tryte_t tvm_reg(const tvm_t *vm, uint8_t r)
{
    if (r >= TVM_REG_COUNT) return 0;
    return vm->reg[r];
}

/** @brief Read memory cell @p addr (index wraps like the Python `% MEM_SIZE`). */
static inline tvm_tryte_t tvm_mem_read(const tvm_t *vm, int32_t addr)
{
    int32_t m = addr % (int32_t)TVM_MEM_SIZE;
    if (m < 0) m += (int32_t)TVM_MEM_SIZE;
    return tvm_decode(vm->mem[m]);
}

/** @brief Read the i-th OUT value (0 if i >= number of captured values). */
static inline tvm_tryte_t tvm_out(const tvm_t *vm, uint16_t i)
{
    if (i >= vm->out_len) return 0;
    return vm->out[i];
}

/** @brief Number of OUT values actually captured (<= TVM_OUT_SIZE). */
static inline uint16_t tvm_out_len(const tvm_t *vm) { return vm->out_len; }

/* ------------------------------------------------------------------ */
/*  Execution core                                                     */
/* ------------------------------------------------------------------ */

/** @brief Wrap an address into [0, mod) using floor modulo (Python semantics). */
static inline uint16_t tvm_wrap(int32_t value, uint16_t mod)
{
    int32_t m;
    if (mod == 0) return 0;
    m = value % (int32_t)mod;
    if (m < 0) m += (int32_t)mod;
    return (uint16_t)m;
}

/** @brief Fetch the packed tryte at PC and advance PC. */
static inline tvm_code_t tvm_fetch(tvm_t *vm)
{
    if (vm->pc >= vm->prog_len) return (tvm_code_t)TVM_OP_HALT;
    return vm->prog[vm->pc++];
}

/**
 * @brief Execute one instruction.
 * @return 1 if the machine may continue, 0 when it halted or faulted.
 */
static inline int tvm_step(tvm_t *vm)
{
    tvm_code_t op = tvm_fetch(vm);
    tvm_code_t f1, f2;
    uint16_t a, b;

    vm->steps += 1;

    switch (op) {
    case TVM_OP_HALT:
        vm->halted = 1;
        return 0;

    case TVM_OP_LOAD:                       /* LOAD R dst, imm */
        a = (uint16_t)tvm_fetch(vm);
        f2 = tvm_fetch(vm);
        if (a >= TVM_REG_COUNT) { vm->fault = TVM_ERR_BAD_REG; vm->halted = 1; return 0; }
        vm->reg[a] = tvm_decode(f2);
        return 1;

    case TVM_OP_STORE:                      /* STORE R src, addr */
        a = (uint16_t)tvm_fetch(vm);
        f2 = tvm_fetch(vm);
        if (a >= TVM_REG_COUNT) { vm->fault = TVM_ERR_BAD_REG; vm->halted = 1; return 0; }
        vm->mem[tvm_wrap((int32_t)tvm_decode(f2), (uint16_t)TVM_MEM_SIZE)] =
            tvm_encode(vm->reg[a]);
        return 1;

    case TVM_OP_ADD:                        /* ADD R d, R s */
        a = (uint16_t)tvm_fetch(vm);
        b = (uint16_t)tvm_fetch(vm);
        if (a >= TVM_REG_COUNT || b >= TVM_REG_COUNT) {
            vm->fault = TVM_ERR_BAD_REG; vm->halted = 1; return 0;
        }
        vm->reg[a] = tvm_clamp((int32_t)vm->reg[a] + (int32_t)vm->reg[b]);
        return 1;

    case TVM_OP_SUB:                        /* SUB R d, R s */
        a = (uint16_t)tvm_fetch(vm);
        b = (uint16_t)tvm_fetch(vm);
        if (a >= TVM_REG_COUNT || b >= TVM_REG_COUNT) {
            vm->fault = TVM_ERR_BAD_REG; vm->halted = 1; return 0;
        }
        vm->reg[a] = tvm_clamp((int32_t)vm->reg[a] - (int32_t)vm->reg[b]);
        return 1;

    case TVM_OP_MUL:                        /* MUL R d, R s */
        a = (uint16_t)tvm_fetch(vm);
        b = (uint16_t)tvm_fetch(vm);
        if (a >= TVM_REG_COUNT || b >= TVM_REG_COUNT) {
            vm->fault = TVM_ERR_BAD_REG; vm->halted = 1; return 0;
        }
        /* 9841 * 9841 = 96845281 fits in int32_t; no 64-bit needed. */
        vm->reg[a] = tvm_clamp((int32_t)vm->reg[a] * (int32_t)vm->reg[b]);
        return 1;

    case TVM_OP_NEG:                        /* NEG R d */
        a = (uint16_t)tvm_fetch(vm);
        if (a >= TVM_REG_COUNT) { vm->fault = TVM_ERR_BAD_REG; vm->halted = 1; return 0; }
        vm->reg[a] = tvm_clamp(-(int32_t)vm->reg[a]);
        return 1;

    case TVM_OP_MOV:                        /* MOV R d, R s */
        a = (uint16_t)tvm_fetch(vm);
        b = (uint16_t)tvm_fetch(vm);
        if (a >= TVM_REG_COUNT || b >= TVM_REG_COUNT) {
            vm->fault = TVM_ERR_BAD_REG; vm->halted = 1; return 0;
        }
        vm->reg[a] = vm->reg[b];
        return 1;

    case TVM_OP_JMP:                        /* JMP target */
        f1 = tvm_fetch(vm);
        vm->pc = tvm_wrap((int32_t)tvm_decode(f1), vm->prog_len);
        return 1;

    case TVM_OP_JZ:                         /* JZ target, R c */
        f1 = tvm_fetch(vm);
        a = (uint16_t)tvm_fetch(vm);
        if (a >= TVM_REG_COUNT) { vm->fault = TVM_ERR_BAD_REG; vm->halted = 1; return 0; }
        if (vm->reg[a] == 0) vm->pc = tvm_wrap((int32_t)tvm_decode(f1), vm->prog_len);
        return 1;

    case TVM_OP_JNZ:                        /* JNZ target, R c */
        f1 = tvm_fetch(vm);
        a = (uint16_t)tvm_fetch(vm);
        if (a >= TVM_REG_COUNT) { vm->fault = TVM_ERR_BAD_REG; vm->halted = 1; return 0; }
        if (vm->reg[a] != 0) vm->pc = tvm_wrap((int32_t)tvm_decode(f1), vm->prog_len);
        return 1;

    case TVM_OP_OUT:                        /* OUT R s */
        a = (uint16_t)tvm_fetch(vm);
        if (a >= TVM_REG_COUNT) { vm->fault = TVM_ERR_BAD_REG; vm->halted = 1; return 0; }
        if (vm->out_len < (uint16_t)TVM_OUT_SIZE) vm->out[vm->out_len++] = vm->reg[a];
        vm->out_total += 1;
        return 1;

    case TVM_OP_NOP:
        return 1;

    default:
        vm->fault = TVM_ERR_BAD_OPCODE;
        vm->halted = 1;
        return 0;
    }
}

/**
 * @brief Run until HALT, fault, or @p max_steps instructions (0 = default).
 * @return TVM_OK, or a negative TVM_ERR_* code.
 */
static inline int tvm_run(tvm_t *vm, uint32_t max_steps)
{
    if (max_steps == 0u) max_steps = (uint32_t)TVM_STEP_LIMIT;
    for (;;) {
        if (!tvm_step(vm)) return (int)vm->fault;
        if (vm->steps > max_steps) {
            vm->fault = TVM_ERR_STEP_LIMIT;
            vm->halted = 1;
            return TVM_ERR_STEP_LIMIT;
        }
    }
}

#endif /* TERNARY_VM_H */
