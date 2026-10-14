/*
 * demo.c -- usage example for the MCU core include/ternary_vm.h.
 *
 * Runs every program from programs_generated.h (produced by
 * tools/emit_bytecode.py out of the programs/ directory) on the C core,
 * checks the OUT stream against the expectation, prints the register file,
 * and demonstrates the documented error paths (step limit, bad opcode).
 *
 * Build (no external dependencies):
 *   gcc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -Iexamples/c \
 *       examples/c/demo.c -o demo && ./demo
 *
 * Exit code 0 = every check passed.
 */
#include <stdio.h>

#include "ternary_vm.h"
#include "programs_generated.h"

static int failures = 0;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("  FAIL %s\n", what);
        failures += 1;
    }
}

/* ------------------------------------------------------------------ */
/*  1. The three repository programs, bytecode frozen by the assembler */
/* ------------------------------------------------------------------ */
static void run_repository_programs(void)
{
    uint16_t p;
    for (p = 0; p < TVM_DEMO_PROGRAM_COUNT; ++p) {
        const tvm_demo_program_t *d = &tvm_demo_programs[p];
        tvm_t vm;
        int rc, i;
        tvm_init(&vm, d->code, d->code_len);
        rc = tvm_run(&vm, 0);

        printf("program %-9s len=%2u steps=%lu pc=%u rc=%d output=[",
               d->name, (unsigned)d->code_len, (unsigned long)vm.steps,
               (unsigned)vm.pc, rc);
        for (i = 0; i < (int)vm.out_len; ++i)
            printf("%s%d", i ? ", " : "", (int)vm.out[i]);
        printf("]\n");

        check(rc == TVM_OK, "run returned TVM_OK");
        check(vm.out_len == d->expect_len, "OUT length matches expectation");
        for (i = 0; i < (int)d->expect_len && i < (int)vm.out_len; ++i)
            check(vm.out[i] == d->expect[i], "OUT value matches expectation");
    }
}

/* ------------------------------------------------------------------ */
/*  2. Register read-out after a program: R0 = 12 for add.tasm         */
/* ------------------------------------------------------------------ */
static void show_registers(void)
{
    tvm_t vm;
    int i;
    tvm_init(&vm, prog_add, prog_add_len);
    (void)tvm_run(&vm, 0);
    printf("add.tasm registers:");
    for (i = 0; i < TVM_REG_COUNT; ++i) printf(" R%d=%d", i, (int)tvm_reg(&vm, i));
    printf("\n");
    check(tvm_reg(&vm, 0) == 12, "R0 == 12 after add.tasm");
    check(tvm_reg(&vm, 1) == 7, "R1 == 7 after add.tasm");
    check(tvm_reg(&vm, 2) == 0, "R2 untouched");
}

/* ------------------------------------------------------------------ */
/*  3. Saturating 9-trit arithmetic at the boundary                    */
/* ------------------------------------------------------------------ */
static void show_boundaries(void)
{
    printf("tryte range            : [%d, %d]\n", (int)TVM_TRYTE_MIN, (int)TVM_TRYTE_MAX);
    printf("encode(0)=%d encode(9841)=%d encode(-9841)=%d\n",
           (int)tvm_encode(0), (int)tvm_encode(9841), (int)tvm_encode(-9841));
    printf("decode(encode(1234))=%d decode(0)=%d\n",
           (int)tvm_decode(tvm_encode(1234)), (int)tvm_decode(0));
    printf("saturating add 9841+9841 -> %d\n", (int)tvm_clamp(9841 + 9841));
    printf("saturating sub -9841-9841 -> %d\n", (int)tvm_clamp(-9841 - 9841));
    printf("saturating mul 9841*9841 -> %d\n", (int)tvm_clamp(9841 * 9841));
    printf("saturating neg -(-9841)  -> %d\n", (int)tvm_clamp(-(-9841)));
    check(tvm_clamp(9841 + 9841) == 9841, "add saturates high");
    check(tvm_clamp(-9841 - 9841) == -9841, "sub saturates low");
    check(tvm_clamp(9841 * 9841) == 9841, "mul saturates high");
    check(tvm_decode(tvm_encode(1234)) == 1234, "codec round trip");
}

/* ------------------------------------------------------------------ */
/*  4. Memory write (STORE) and the documented fault paths             */
/* ------------------------------------------------------------------ */
static void show_store_and_faults(void)
{
    /* STORE R0, 300 -- address wraps modulo TVM_MEM_SIZE (300 % 256 = 44). */
    tvm_t vm;
    int rc;

    /* LOAD R0 5 ; STORE R0 300 ; HALT  (imm 300 = code 6561 + 300) */
    static const tvm_code_t store_prog[] = {
        TVM_OP_LOAD, 0, (tvm_code_t)(TVM_TRYTE_BIAS + 5),
        TVM_OP_STORE, 0, (tvm_code_t)(TVM_TRYTE_BIAS + 300),
        TVM_OP_HALT
    };
    tvm_init(&vm, store_prog, (uint16_t)(sizeof store_prog / sizeof store_prog[0]));
    rc = tvm_run(&vm, 0);
    printf("STORE R0,300: rc=%d mem[44]=%d mem[0]=%d\n",
           rc, (int)tvm_mem_read(&vm, 44), (int)tvm_mem_read(&vm, 0));
    check(rc == TVM_OK, "store program halts cleanly");
    check(tvm_mem_read(&vm, 44) == 5, "memory cell 44 holds the stored value");
    check(vm.mem[44] == tvm_encode(5), "memory holds the packed code, as in Python");

    /* Step limit: JMP to itself. */
    {
        static const tvm_code_t spin[] = { TVM_OP_JMP, (tvm_code_t)TVM_TRYTE_BIAS, TVM_OP_HALT };
        tvm_init(&vm, spin, (uint16_t)(sizeof spin / sizeof spin[0]));
        rc = tvm_run(&vm, 100);
        printf("spin program: rc=%d steps=%lu fault=%d\n",
               rc, (unsigned long)vm.steps, (int)vm.fault);
        check(rc == TVM_ERR_STEP_LIMIT, "step limit reported");
    }

    /* Bad opcode (99 is outside the ISA). */
    {
        static const tvm_code_t bad[] = { 99 };
        tvm_init(&vm, bad, 1);
        rc = tvm_run(&vm, 100);
        printf("bad opcode: rc=%d fault=%d\n", rc, (int)vm.fault);
        check(rc == TVM_ERR_BAD_OPCODE, "bad opcode reported");
    }

    /* Bad register index (13 is not a register). */
    {
        static const tvm_code_t bad_reg[] = { TVM_OP_LOAD, 13, (tvm_code_t)(TVM_TRYTE_BIAS + 1) };
        tvm_init(&vm, bad_reg, (uint16_t)(sizeof bad_reg / sizeof bad_reg[0]));
        rc = tvm_run(&vm, 100);
        printf("bad register: rc=%d fault=%d\n", rc, (int)vm.fault);
        check(rc == TVM_ERR_BAD_REG, "bad register reported");
    }
}

int main(void)
{
    printf("ternary-vm C core demo (include/ternary_vm.h)\n");
    printf("TRITS_PER_TRYTE=%d REG_COUNT=%d MEM_SIZE=%d OUT_SIZE=%d\n\n",
           TVM_TRITS_PER_TRYTE, TVM_REG_COUNT, TVM_MEM_SIZE, TVM_OUT_SIZE);

    run_repository_programs();
    printf("\n");
    show_registers();
    printf("\n");
    show_boundaries();
    printf("\n");
    show_store_and_faults();

    printf("\n%s: %d check(s) failed\n", failures ? "DEMO FAILED" : "DEMO OK", failures);
    return failures ? 1 : 0;
}
