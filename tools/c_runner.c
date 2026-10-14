/*
 * c_runner.c -- differential-test harness for include/ternary_vm.h.
 *
 * Reads a program image (packed trytes, whitespace separated integers) from
 * stdin, runs it on the C core, and prints the machine state in a fixed,
 * easy-to-parse format so a Python test can compare it against the reference
 * interpreter in vm/ternary_vm.py.
 *
 * Usage:
 *   ./c_runner [max_steps]               < program.txt
 *   ./c_runner --table                   # tryte codec table (encode/decode)
 *   ./c_runner --mem [max_steps]         # also dump the 256 memory codes
 *
 * Output (fixed field order):
 *   STATUS ok|fault
 *   FAULT <code>
 *   STEPS <n>
 *   PC <n>
 *   OUT_TOTAL <n>
 *   OUTPUT <v> <v> ...
 *   REGS <r0> ... <r7>
 *   MEM <c0> ... <c255>        (only with --mem)
 *
 * This file is a test tool, not part of the library: the library is the
 * single header include/ternary_vm.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ternary_vm.h"

#define MAX_PROG 4096

static int run_program(int max_steps, int dump_mem)
{
    static tvm_code_t prog[MAX_PROG];
    uint16_t len = 0;
    long v;
    int rc, i;
    tvm_t vm;

    while (scanf("%ld", &v) == 1) {
        if (len >= (uint16_t)MAX_PROG) {
            fprintf(stderr, "c_runner: program too long (max %d)\n", MAX_PROG);
            return 2;
        }
        prog[len++] = (tvm_code_t)v;
    }

    tvm_init(&vm, prog, len);
    rc = tvm_run(&vm, (uint32_t)max_steps);

    printf("STATUS %s\n", (rc == TVM_OK && vm.fault == 0) ? "ok" : "fault");
    printf("FAULT %d\n", (int)vm.fault);
    printf("STEPS %lu\n", (unsigned long)vm.steps);
    printf("PC %u\n", (unsigned)vm.pc);
    printf("OUT_TOTAL %lu\n", (unsigned long)vm.out_total);
    printf("OUTPUT");
    for (i = 0; i < (int)vm.out_len; ++i) printf(" %d", (int)vm.out[i]);
    printf("\nREGS");
    for (i = 0; i < TVM_REG_COUNT; ++i) printf(" %d", (int)vm.reg[i]);
    printf("\n");
    if (dump_mem) {
        printf("MEM");
        for (i = 0; i < TVM_MEM_SIZE; ++i) printf(" %d", (int)vm.mem[i]);
        printf("\n");
    }
    /* rc is informational: the Python reference raises instead of returning. */
    printf("RC %d\n", rc);
    return 0;
}

static int print_table(void)
{
    int v, c;
    for (v = -9841; v <= 9841; ++v) {
        printf("E %d %d\n", v, (int)tvm_encode((tvm_tryte_t)v));
    }
    for (c = 0; c <= 19682; ++c) {
        printf("D %d %d\n", c, (int)tvm_decode((tvm_code_t)c));
    }
    /* Trit extraction, cross-checked against the Python digit expansion. */
    for (c = 0; c <= 19682; c += 97) {
        int pos;
        for (pos = 0; pos < TVM_TRITS_PER_TRYTE; ++pos)
            printf("T %d %d %d\n", c, pos, (int)tvm_trit_at((tvm_code_t)c, (uint8_t)pos));
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i, max_steps = 100000, dump_mem = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--table") == 0) return print_table();
        else if (strcmp(argv[i], "--mem") == 0) dump_mem = 1;
        else max_steps = atoi(argv[i]);
    }
    return run_program(max_steps, dump_mem);
}
