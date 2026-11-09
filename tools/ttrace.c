/*
 * ttrace.c -- carry/borrow visualisation tracer for balanced-ternary
 * arithmetic (the research-lab tool requested by the audit).
 *
 * Takes an operation and one or two decimal operands and prints the *whole*
 * digit-wise computation: every operand trit, every incoming and outgoing
 * carry (add) or borrow (sub), every result trit. Plain ASCII by default,
 * a standalone HTML page with --html. The arithmetic itself lives in
 * tools/ttrace_add.h and is exhaustively verified against int64 arithmetic
 * by tests/property_test.c.
 *
 * Usage:
 *   ttrace [--html] [--width W] OP A [B]
 *     OP  add | sub | mul | neg
 *     A,B decimal integers representable in W trits
 *     W   width in trits, 1..18, default 9 (one tryte)
 *
 * Exit codes: 0 = ok, 1 = usage error, 2 = operand not representable in the
 * requested width, 3 = traced fine but the exact result does not fit in the
 * width (the trace is still printed; the VM would saturate instead).
 *
 * Examples:
 *   ./ttrace add 5 7
 *   ./ttrace sub -5 7
 *   ./ttrace add 9841 1          # overflow demonstration (exit 3)
 *   ./ttrace --width 6 add 40 -40
 *   ./ttrace --html add -5 7 > trace.html
 *
 * This file is a tool, not part of the library: the library is the single
 * header include/ternary_vm.h; the digit layer is tools/ttrace_add.h.
 * Licence: Apache-2.0 (same as the repository).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "ttrace_add.h"
#include "ternary_vm.h"

#define DEFAULT_WIDTH 9

static int g_html = 0;
static int g_width = DEFAULT_WIDTH;

/* ------------------------------------------------------------------ */
/*  Small output helpers (ASCII or HTML)                                */
/* ------------------------------------------------------------------ */

static void out_legend(void)
{
    if (g_html)
        printf("<p>Trits: <code>+</code>&nbsp;=&nbsp;+1, "
               "<code>0</code>&nbsp;=&nbsp;0, <code>-</code>&nbsp;=&nbsp;-1. "
               "Position <code>i</code> has weight <code>3<sup>i</sup></code>. "
               "Left column = most significant trit.</p>\n");
    else
        printf("trits: + = +1, 0 = 0, - = -1 (position i has weight 3^i;"
               " left = most significant)\n");
}

static void out_row(const char *label, const int8_t *cells, int width)
{
    int i;
    if (g_html) {
        printf("<tr><td>%s</td>", label);
        for (i = width - 1; i >= 0; --i) printf("<td>%c</td>", bt_char(cells[i]));
        printf("</tr>\n");
    } else {
        printf("%-11s", label);
        for (i = width - 1; i >= 0; --i) printf(" %c", bt_char(cells[i]));
        printf("\n");
    }
}

static void out_start_table(void)
{
    if (g_html) {
        printf("<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\" "
               "style=\"border-collapse:collapse;text-align:center;"
               "font-family:monospace\">\n");
    }
}

static void out_end_table(void)
{
    if (g_html) printf("</table>\n");
}

static void out_line(const char *fmt, ...)
{
    va_list ap;
    if (g_html) printf("<p>");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (g_html) printf("</p>\n");
    else printf("\n");
}

static void out_emph(const char *fmt, ...)   /* emphasised line (overflow) */
{
    va_list ap;
    if (g_html) printf("<p style=\"color:#b00;font-weight:bold\">");
    else printf("!! ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (g_html) printf("</p>\n");
    else printf("\n");
}

static void out_header(const char *opname, const char *sym,
                       long long a, long long b)
{
    if (g_html) {
        printf("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
               "<meta charset=\"utf-8\">\n"
               "<title>ttrace: %s %lld %s %lld</title>\n"
               "<style>body{font-family:sans-serif;max-width:56em;"
               "margin:1em auto}td,th{font-family:monospace}</style>\n"
               "</head>\n<body>\n<h1>ttrace</h1>\n", opname, a, sym, b);
        printf("<h2>%s %lld %s %lld (width %d trits)</h2>\n",
               opname, a, sym, b, g_width);
    } else {
        printf("ttrace: %s %lld %s %lld (width %d)\n",
               opname, a, sym, b, g_width);
    }
}

static void out_footer(int code)
{
    if (g_html) printf("</body>\n</html>\n");
    (void)code; /* exit code is the caller's business */
}

/* ------------------------------------------------------------------ */
/*  Shared trace printing                                               */
/* ------------------------------------------------------------------ */

/*
 * Print the per-trit table and the consistency check of one add/sub trace.
 * Layout (width 9, add 5 + 7):
 *
 *   row \ pos    8 7 6 5 4 3 2 1 0
 *   A            0 0 0 0 0 0 + - -
 *   B            0 0 0 0 0 0 + - +
 *   carry in     0 0 0 0 0 0 0 0 0
 *   result       0 0 0 0 0 0 + + 0
 *   carry out    0 0 0 0 0 0 0 0 0
 */
static void print_trace(const bt_trace_t *t, const char *opname,
                        const char *sym, long long a, long long b, int is_sub)
{
    const char *cin_name = is_sub ? "borrow in" : "carry in";
    const char *cout_name = is_sub ? "borrow out" : "carry out";
    int8_t cin[BT_MAX_WIDTH], res[BT_MAX_WIDTH], cout[BT_MAX_WIDTH];
    int8_t da[BT_MAX_WIDTH], db[BT_MAX_WIDTH];
    int64_t digit_value, reconstructed;
    int64_t power = 1;
    int i;

    out_header(opname, sym, a, b);
    out_legend();
    (void)bt_digits(a, t->width, da);
    (void)bt_digits(b, t->width, db);
    for (i = 0; i < t->width; ++i) {
        cin[i] = t->step[i].carry_in;
        res[i] = t->step[i].result;
        cout[i] = t->step[i].carry_out;
    }

    out_start_table();
    if (g_html) {
        int p;
        printf("<tr><th>row \\ pos</th>");
        for (p = t->width - 1; p >= 0; --p)
            printf("<th>pos %d<br>3<sup>%d</sup></th>", p, p);
        printf("</tr>\n");
    } else {
        printf("%-11s", "row \\ pos");
        for (i = t->width - 1; i >= 0; --i) printf(" %d", i);
        printf("\n");
    }
    out_row("A", da, t->width);
    out_row("B", db, t->width);
    out_row(cin_name, cin, t->width);
    out_row("result", res, t->width);
    out_row(cout_name, cout, t->width);
    out_end_table();

    /* consistency: digits of the result row + top carry/borrow == exact */
    for (i = 0; i < t->width; ++i) power *= 3;          /* power = 3^width */
    digit_value = bt_from_digits(res, t->width);
    reconstructed = is_sub ? digit_value - (int64_t)t->final_carry * power
                           : digit_value + (int64_t)t->final_carry * power;
    if (reconstructed == t->value)
        out_line("check: %lld %s %lld = %lld (exact); digit row %s %s*3^%d = %lld -- matches",
                 a, is_sub ? "-" : "+", b, t->value,
                 is_sub ? "- borrow" : "+ carry", is_sub ? "" : "",
                 t->width, reconstructed);
    else
        out_emph("INCONSISTENT TRACE: digit row + top carry gives %lld but exact is %lld (this is a bug, please report)",
                 (long long)reconstructed, (long long)t->value);

    if (t->overflow) {
        out_emph("OVERFLOW: the exact result %lld does not fit in %d trits "
                 "([-%lld, +%lld]); the VM would saturate instead: tvm_clamp -> %lld",
                 (long long)t->value, t->width,
                 (long long)((power - 1) / 2), (long long)((power - 1) / 2),
                 (long long)t->clamped);
        out_footer(0);
        exit(3);
    }
    if (g_width == 9)
        out_line("VM semantics (include/ternary_vm.h): tvm_clamp(%lld) = %lld "
                 "-- fits in one tryte [-9841, +9841]",
                 (long long)t->value, (long long)tvm_clamp((int32_t)t->value));
    else
        out_line("VM semantics: this width is not the VM's 9-trit tryte; "
                 "clamped to width: %lld", (long long)t->clamped);
}

/* ------------------------------------------------------------------ */
/*  Operations                                                          */
/* ------------------------------------------------------------------ */

static int do_add(long long a, long long b)
{
    bt_trace_t t = bt_add_trace(a, b, g_width);
    print_trace(&t, "add", "+", a, b, 0);
    return 0;
}

static int do_sub(long long a, long long b)
{
    bt_trace_t t = bt_sub_trace(a, b, g_width);
    print_trace(&t, "sub", "-", a, b, 1);
    return 0;
}

static int do_neg(long long a)
{
    bt_trace_t t = bt_neg_trace(a, g_width);
    print_trace(&t, "neg", "(-1)*", a, 0LL, 0);
    out_line("note: negation flips every trit and never produces a carry -- "
             "impossible with an asymmetric binary digit set");
    return 0;
}

static int do_mul(long long a, long long b)
{
    bt_mul_trace_t m = bt_mul_trace(a, b, g_width);
    int i;
    int64_t acc = 0;

    out_header("mul", "*", a, b);
    out_legend();
    out_line("digit-wise shift-and-add: for every non-zero trit b_i of B, "
             "add A * b_i * 3^i into the accumulator");
    out_start_table();
    if (g_html)
        printf("<tr><th>step</th><th>pos i</th><th>trit b_i</th>"
               "<th>partial = A*b_i*3^i</th><th>acc before</th>"
               "<th>acc after</th></tr>\n");
    else
        printf("%-5s %-5s %-7s %-22s %-14s %-14s\n",
               "step", "pos", "trit", "partial = A*b_i*3^i",
               "acc before", "acc after");
    for (i = 0; i < m.n_steps; ++i) {
        if (g_html)
            printf("<tr><td>%d</td><td>%d</td><td>%c</td><td>%lld</td>"
                   "<td>%lld</td><td>%lld%s</td></tr>\n",
                   i + 1, m.step[i].digit_index, bt_char(m.step[i].mult_trit),
                   (long long)m.step[i].partial,
                   (long long)m.step[i].acc_before,
                   (long long)m.step[i].acc_after,
                   m.step[i].overflow ? " (!)" : "");
        else
            printf("%-5d %-5d %-7c %-22lld %-14lld %-14lld%s\n",
                   i + 1, m.step[i].digit_index, bt_char(m.step[i].mult_trit),
                   (long long)m.step[i].partial,
                   (long long)m.step[i].acc_before,
                   (long long)m.step[i].acc_after,
                   m.step[i].overflow ? "  <- exceeds tryte range" : "");
        acc = m.step[i].acc_after;
    }
    out_end_table();
    if (m.n_steps == 0) {
        out_line("B has no non-zero trits (B = 0): the product is 0");
    } else {
        out_line("check: %lld * %lld = %lld (exact); digit ladder = %lld -- %s",
                 a, b, (long long)m.value, (long long)acc,
                 acc == m.value ? "matches" : "MISMATCH (bug, please report)");
        out_line("each step is one addition; see its carry table with e.g. "
                 "ttrace --width 18 add %lld %lld",
                 (long long)m.step[0].acc_before, (long long)m.step[0].partial);
    }
    if (m.overflow) {
        out_emph("OVERFLOW: the product %lld does not fit in %d trits; the VM "
                 "would saturate instead: tvm_clamp -> %lld",
                 (long long)m.value, g_width, (long long)m.clamped);
        out_footer(0);
        return 3;
    }
    if (g_width == 9)
        out_line("VM semantics (include/ternary_vm.h): tvm_clamp(%lld) = %lld "
                 "-- fits in one tryte [-9841, +9841]",
                 (long long)m.value, (long long)tvm_clamp((int32_t)m.value));
    return 0;
}

/* ------------------------------------------------------------------ */
/*  CLI                                                                 */
/* ------------------------------------------------------------------ */

static int64_t width_limit(int width)
{
    int64_t p = 1;
    int i;
    for (i = 0; i < width; ++i) p *= 3;
    return (p - 1) / 2;
}

static int parse_operand(const char *s, long long *out)
{
    char *end = NULL;
    long long v;
    if (s == NULL || *s == '\0') return -1;
    v = strtoll(s, &end, 10);
    if (end == s || *end != '\0') return -1;
    *out = v;
    return 0;
}

static void usage(FILE *fh)
{
    fprintf(fh,
        "usage: %s [--html] [--width W] OP A [B]\n"
        "  OP  add | sub | mul | neg      (neg takes no B)\n"
        "  A,B decimal integers representable in W trits\n"
        "  W   width in trits, 1..18; default 9 = one tryte [-9841, +9841]\n"
        "examples:\n"
        "  %s add 5 7\n"
        "  %s sub -5 7\n"
        "  %s add 9841 1              # overflow demonstration (exit code 3)\n"
        "  %s --html add -5 7 > trace.html\n",
        "ttrace", "ttrace", "ttrace", "ttrace", "ttrace");
}

int main(int argc, char **argv)
{
    int i = 1;
    const char *op;
    long long a = 0, b = 0;
    int have_a, have_b;

    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "--html") == 0) { g_html = 1; i++; }
        else if (strcmp(argv[i], "--width") == 0) {
            if (i + 1 >= argc) { usage(stderr); return 1; }
            g_width = atoi(argv[i + 1]);
            if (g_width < 1 || g_width > BT_MAX_WIDTH) {
                fprintf(stderr, "ttrace: width must be 1..%d\n", BT_MAX_WIDTH);
                return 1;
            }
            i += 2;
        }
        else { usage(stderr); return 1; }
    }
    if (i >= argc) { usage(stderr); return 1; }
    op = argv[i++];

    have_a = i < argc;
    if (have_a && parse_operand(argv[i++], &a) != 0) {
        fprintf(stderr, "ttrace: bad operand '%s'\n", argv[i - 1]);
        return 1;
    }
    have_b = i < argc;
    if (have_b && parse_operand(argv[i++], &b) != 0) {
        fprintf(stderr, "ttrace: bad operand '%s'\n", argv[i - 1]);
        return 1;
    }
    if (i != argc) { usage(stderr); return 1; }

    if (strcmp(op, "neg") == 0) {
        if (!have_a || have_b) { usage(stderr); return 1; }
    } else if (!have_a || !have_b) {
        usage(stderr);
        return 1;
    }

    if (have_a && (a > width_limit(g_width) || a < -width_limit(g_width))) {
        fprintf(stderr, "ttrace: operand %lld not representable in %d trits "
                "(range [-%lld, +%lld])\n", a, g_width,
                (long long)width_limit(g_width), (long long)width_limit(g_width));
        return 2;
    }
    if (have_b && (b > width_limit(g_width) || b < -width_limit(g_width))) {
        fprintf(stderr, "ttrace: operand %lld not representable in %d trits "
                "(range [-%lld, +%lld])\n", b, g_width,
                (long long)width_limit(g_width), (long long)width_limit(g_width));
        return 2;
    }

    if (strcmp(op, "add") == 0) return do_add(a, b);
    if (strcmp(op, "sub") == 0) return do_sub(a, b);
    if (strcmp(op, "mul") == 0) return do_mul(a, b);
    if (strcmp(op, "neg") == 0) return do_neg(a);
    fprintf(stderr, "ttrace: unknown operation '%s' (add|sub|mul|neg)\n", op);
    return 1;
}
