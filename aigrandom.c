/***************************************************************************
Copyright (c) 2026, Jingren Wang, HKUST(GZ).

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
***************************************************************************/

#include "aigrandom.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/times.h>

/*------------------------------------------------------------------------*/

static unsigned rng_state;

static void rng_init(unsigned seed)
{
    rng_state = seed ? seed : 1;
}

static unsigned rng_next(void)
{
    rng_state *= 1664525u;
    rng_state += 1013904223u;
    return rng_state;
}

static unsigned rng_pick(unsigned from, unsigned to)
{
    unsigned range = to - from + 1;
    return from + (rng_next() % range);
}

static int rng_flip(void)
{
    return rng_next() & 1;
}

/*------------------------------------------------------------------------*/

typedef struct LitPool LitPool;

struct LitPool {
    unsigned* lits;
    unsigned count;
    unsigned size;
};

static void pool_init(LitPool* p)
{
    p->lits = 0;
    p->count = 0;
    p->size = 0;
}

static void pool_push(LitPool* p, unsigned lit)
{
    if (p->count == p->size) {
        p->size = p->size ? 2 * p->size : 64;
        p->lits = realloc(p->lits, p->size * sizeof(unsigned));
    }
    p->lits[p->count++] = lit;
}

static unsigned pool_random(const LitPool* p)
{
    assert(p->count > 0);
    return p->lits[rng_pick(0, p->count - 1)];
}

static void pool_release(LitPool* p)
{
    free(p->lits);
    pool_init(p);
}

static unsigned pool_random_avoid_var(const LitPool* p, unsigned avoid_var)
{
    unsigned lit, tries = 0;
    do {
        lit = pool_random(p);
        if (aiger_lit2var(lit) != avoid_var)
            return lit;
    } while (++tries < p->count + 1);
    return lit;
}

/*------------------------------------------------------------------------*/

static void remove_unused_inputs(aiger* model)
{
    unsigned char* used;
    unsigned i, new_num_inputs;

    used = calloc(model->maxvar + 1, 1);
    if (!used)
        return;

    for (i = 0; i < model->num_ands; i++) {
        used[aiger_lit2var(model->ands[i].rhs0)] = 1;
        used[aiger_lit2var(model->ands[i].rhs1)] = 1;
    }
    for (i = 0; i < model->num_latches; i++) {
        used[aiger_lit2var(model->latches[i].next)] = 1;
    }
    for (i = 0; i < model->num_outputs; i++) {
        used[aiger_lit2var(model->outputs[i].lit)] = 1;
    }
    for (i = 0; i < model->num_bad; i++) {
        used[aiger_lit2var(model->bad[i].lit)] = 1;
    }
    for (i = 0; i < model->num_constraints; i++) {
        used[aiger_lit2var(model->constraints[i].lit)] = 1;
    }
    for (i = 0; i < model->num_justice; i++) {
        unsigned k;
        for (k = 0; k < model->justice[i].size; k++)
            used[aiger_lit2var(model->justice[i].lits[k])] = 1;
    }
    for (i = 0; i < model->num_fairness; i++) {
        used[aiger_lit2var(model->fairness[i].lit)] = 1;
    }

    new_num_inputs = 0;
    for (i = 0; i < model->num_inputs; i++) {
        if (used[aiger_lit2var(model->inputs[i].lit)])
            model->inputs[new_num_inputs++] = model->inputs[i];
    }
    model->num_inputs = new_num_inputs;

    free(used);

    if (new_num_inputs < i)
        aiger_reencode(model);
}

aiger* aigrandom_generate(aigrandom_config* cfg)
{
    unsigned num_inputs, num_latches, num_ands, num_outputs;
    unsigned i, lit, lhs, rhs0, rhs1, next_lit;
    unsigned var_idx, prev_and_lhs, anchor_out;
    aiger* model;
    LitPool defined;
    char buf[120];

    rng_init(cfg->seed);

    num_inputs = rng_pick(cfg->min_inputs, cfg->max_inputs);

    if (cfg->sequential)
        num_latches = rng_pick(cfg->min_latches, cfg->max_latches);
    else
        num_latches = 0;

    num_ands = rng_pick(cfg->min_ands, cfg->max_ands);
    num_outputs = rng_pick(cfg->min_outputs, cfg->max_outputs);

    model = aiger_init();
    pool_init(&defined);

    for (i = 0; i < num_inputs; i++) {
        lit = 2 * (i + 1);
        if (cfg->add_symbols) {
            sprintf(buf, "input_%u", i);
            aiger_add_input(model, lit, buf);
        } else
            aiger_add_input(model, lit, 0);
        pool_push(&defined, lit);
    }

    for (i = 0; i < num_latches; i++) {
        lit = 2 * (num_inputs + i + 1);
        pool_push(&defined, lit);
    }

    var_idx = num_inputs + num_latches;

    prev_and_lhs = 0;
    for (i = 0; i < num_ands; i++) {
        lhs = 2 * (++var_idx);

        if (i > 0) {
            rhs0 = prev_and_lhs;
            if (rng_flip())
                rhs0 ^= 1;
            rhs1 = pool_random_avoid_var(&defined, aiger_lit2var(rhs0));
            if (rng_flip())
                rhs1 ^= 1;
        } else {
            assert(defined.count > 0);
            rhs0 = pool_random(&defined);
            if (rng_flip())
                rhs0 ^= 1;
            rhs1 = pool_random_avoid_var(&defined, aiger_lit2var(rhs0));
            if (rng_flip())
                rhs1 ^= 1;
        }

        aiger_add_and(model, lhs, rhs0, rhs1);
        pool_push(&defined, lhs);
        prev_and_lhs = lhs;
    }

    for (i = 0; i < num_latches; i++) {
        lit = 2 * (num_inputs + i + 1);

        assert(defined.count > 0);
        next_lit = pool_random(&defined);
        if (rng_flip())
            next_lit ^= 1;

        if (cfg->add_symbols) {
            sprintf(buf, "latch_%u", i);
            aiger_add_latch(model, lit, next_lit, buf);
        } else
            aiger_add_latch(model, lit, next_lit, 0);
    }

    anchor_out =
        (num_ands > 0 && num_outputs > 0) ? rng_pick(0, num_outputs - 1) : 0;

    for (i = 0; i < num_outputs; i++) {
        if (num_ands > 0 && i == anchor_out) {
            lit = prev_and_lhs;
        } else {
            assert(defined.count > 0);
            lit = pool_random(&defined);
        }
        if (rng_flip())
            lit ^= 1;

        if (cfg->add_symbols) {
            sprintf(buf, "output_%u", i);
            aiger_add_output(model, lit, buf);
        } else
            aiger_add_output(model, lit, 0);
    }

    if (cfg->add_comments) {
        sprintf(buf, "generated by aigrandom seed %u", cfg->seed);
        aiger_add_comment(model, buf);
        sprintf(buf, "MILOA %u %u %u %u %u",
                num_inputs + num_latches + num_ands, num_inputs, num_latches,
                num_outputs, num_ands);
        aiger_add_comment(model, buf);
    }

    aiger_reencode(model);
    remove_unused_inputs(model);
    pool_release(&defined);

    return model;
}

/*------------------------------------------------------------------------*/
/* DOT file writer                                                      */
/*------------------------------------------------------------------------*/

int aigrandom_write_dot(aiger* model, FILE* file)
{
    unsigned i;
    aiger_and*and;
    char oname[64];

    fprintf(file, "digraph AIG {\n");
    fprintf(file, "  rankdir = BT;\n");
    fprintf(file, "  node [shape = box];\n\n");

    for (i = 0; i < model->num_inputs; i++) {
        unsigned var = aiger_lit2var(model->inputs[i].lit);
        const char* name = model->inputs[i].name;
        if (name)
            fprintf(file,
                    "  \"%u\" [label = \"%s\", "
                    "style = filled, fillcolor = palegreen];\n",
                    var, name);
        else
            fprintf(file, "  \"%u\" [style = filled, fillcolor = palegreen];\n",
                    var);
    }

    for (i = 0; i < model->num_latches; i++) {
        unsigned var = aiger_lit2var(model->latches[i].lit);
        const char* name = model->latches[i].name;
        if (name)
            fprintf(file, "  \"%u\" [label = \"%s\", color = magenta];\n", var,
                    name);
        else
            fprintf(file, "  \"%u\" [color = magenta];\n", var);
    }

    for (i = 0; i < model->num_outputs; i++) {
        const char* name = model->outputs[i].name;

        if (name)
            snprintf(oname, sizeof oname, "%s", name);
        else
            snprintf(oname, sizeof oname, "out_%u", i);

        fprintf(file,
                "  \"%s\" [shape = doubleoctagon, "
                "style = filled, fillcolor = lightpink];\n",
                oname);
    }

    fprintf(file, "\n");

    if (model->num_inputs) {
        fprintf(file, "  { rank = source;");
        for (i = 0; i < model->num_inputs; i++)
            fprintf(file, " \"%u\";", aiger_lit2var(model->inputs[i].lit));
        fprintf(file, " }\n");
    }

    if (model->num_outputs) {
        fprintf(file, "  { rank = sink;");
        for (i = 0; i < model->num_outputs; i++) {
            const char* name = model->outputs[i].name;

            if (name)
                snprintf(oname, sizeof oname, "%s", name);
            else
                snprintf(oname, sizeof oname, "out_%u", i);

            fprintf(file, " \"%s\";", oname);
        }
        fprintf(file, " }\n");
    }

    fprintf(file, "\n");

    for (i = 0; i < model->num_ands; i++) {
        and= model->ands + i;
        unsigned lhs = aiger_lit2var(and->lhs);
        unsigned s0 = aiger_lit2var(and->rhs0);
        unsigned s1 = aiger_lit2var(and->rhs1);
        int neg0 = aiger_sign(and->rhs0);
        int neg1 = aiger_sign(and->rhs1);

        fprintf(file, "  \"%u\" -> \"%u\"%s;\n", s0, lhs,
                neg0 ? " [style = dashed]" : "");
        fprintf(file, "  \"%u\" -> \"%u\"%s;\n", s1, lhs,
                neg1 ? " [style = dashed]" : "");
    }

    for (i = 0; i < model->num_latches; i++) {
        unsigned var = aiger_lit2var(model->latches[i].lit);
        unsigned next_var = aiger_lit2var(model->latches[i].next);
        int neg = aiger_sign(model->latches[i].next);

        fprintf(file, "  \"%u\" -> \"%u\"%s;\n", next_var, var,
                neg ? " [style = dashed]" : "");
    }

    for (i = 0; i < model->num_outputs; i++) {
        unsigned lit = model->outputs[i].lit;
        unsigned var = aiger_lit2var(lit);
        int neg = aiger_sign(lit);
        const char* name = model->outputs[i].name;

        if (name)
            snprintf(oname, sizeof oname, "%s", name);
        else
            snprintf(oname, sizeof oname, "out_%u", i);

        if (var == 0) {
            fprintf(file, "  \"FALSE\" -> \"%s\" [style = bold];\n", oname);
        } else {
            fprintf(file, "  \"%u\" -> \"%s\"%s;\n", var, oname,
                    neg ? " [style = dashed]" : "");
        }
    }

    fprintf(file, "}\n");
    return 0;
}

/*------------------------------------------------------------------------*/
/* Command line driver                                                     */
/*------------------------------------------------------------------------*/

static void die(const char* fmt, ...)
{
    va_list ap;
    fputs("*** [aigrandom] ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
    exit(1);
}

static void msg(const char* fmt, ...)
{
    va_list ap;
    fputs("[aigrandom] ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

#define USAGE                                                             \
    "usage: aigrandom [-h][-v][-a][-c][-s][-d][-n <count>][<output>]\n"   \
    "\n"                                                                  \
    "Generate random AIGs in AIGER format.\n"                             \
    "\n"                                                                  \
    "  -h           print this command line option summary\n"             \
    "  -v           verbose output on stderr\n"                           \
    "  -a           ASCII output (.aag format)\n"                         \
    "  -c           combinational only (no latches)\n"                    \
    "  -s           add symbols to inputs, latches and outputs\n"         \
    "  -d           also write a DOT graph visualization file\n"          \
    "  -n <count>   generate <count> AIGs (default 1)\n"                  \
    "  <output>     output file path (use '%%d' for count placeholder)\n" \
    "\n"                                                                  \
    "  Size bounds:\n"                                                    \
    "  --min-inputs <n>   minimum inputs (default 1)\n"                   \
    "  --max-inputs <n>   maximum inputs (default 20)\n"                  \
    "  --min-latches <n>  minimum latches (default 0)\n"                  \
    "  --max-latches <n>  maximum latches (default 10)\n"                 \
    "  --min-ands <n>     minimum AND gates (default 0)\n"                \
    "  --max-ands <n>     maximum AND gates (default 100)\n"              \
    "  --min-outputs <n>  minimum outputs (default 1)\n"                  \
    "  --max-outputs <n>  maximum outputs (default 10)\n"                 \
    "  --seed <n>         random seed (default: time-based)\n"

static int isposnum(const char* str)
{
    const char* p;
    if (str[0] == '0' && !str[1])
        return 1;
    if (str[0] == '0')
        return 0;
    for (p = str; *p; p++)
        if (*p < '0' || *p > '9')
            return 0;
    return 1;
}

int main(int argc, char** argv)
{
    aigrandom_config cfg;
    const char* output_pattern = 0;
    int verbose = 0, ascii = 0, dot = 0, count = 1;
    int i;
    unsigned base_seed = 0;

    memset(&cfg, 0, sizeof cfg);
    cfg.min_inputs = 1;
    cfg.max_inputs = 20;
    cfg.min_latches = 0;
    cfg.max_latches = 10;
    cfg.min_ands = 0;
    cfg.max_ands = 100;
    cfg.min_outputs = 1;
    cfg.max_outputs = 10;
    cfg.sequential = 1;

    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (!strcmp(arg, "-h")) {
            printf("%s", USAGE);
            exit(0);
        } else if (!strcmp(arg, "-v"))
            verbose = 1;
        else if (!strcmp(arg, "-a"))
            ascii = 1;
        else if (!strcmp(arg, "-c"))
            cfg.sequential = 0;
        else if (!strcmp(arg, "-s"))
            cfg.add_symbols = 1;
        else if (!strcmp(arg, "-d"))
            dot = 1;
        else if (!strcmp(arg, "-n")) {
            if (++i == argc)
                die("argument to '-n' missing");
            if (!isposnum(argv[i]))
                die("invalid count '%s'", argv[i]);
            count = atoi(argv[i]);
        } else if (!strcmp(arg, "--min-inputs")) {
            if (++i == argc)
                die("argument to '--min-inputs' missing");
            cfg.min_inputs = atoi(argv[i]);
        } else if (!strcmp(arg, "--max-inputs")) {
            if (++i == argc)
                die("argument to '--max-inputs' missing");
            cfg.max_inputs = atoi(argv[i]);
        } else if (!strcmp(arg, "--min-latches")) {
            if (++i == argc)
                die("argument to '--min-latches' missing");
            cfg.min_latches = atoi(argv[i]);
        } else if (!strcmp(arg, "--max-latches")) {
            if (++i == argc)
                die("argument to '--max-latches' missing");
            cfg.max_latches = atoi(argv[i]);
        } else if (!strcmp(arg, "--min-ands")) {
            if (++i == argc)
                die("argument to '--min-ands' missing");
            cfg.min_ands = atoi(argv[i]);
        } else if (!strcmp(arg, "--max-ands")) {
            if (++i == argc)
                die("argument to '--max-ands' missing");
            cfg.max_ands = atoi(argv[i]);
        } else if (!strcmp(arg, "--min-outputs")) {
            if (++i == argc)
                die("argument to '--min-outputs' missing");
            cfg.min_outputs = atoi(argv[i]);
        } else if (!strcmp(arg, "--max-outputs")) {
            if (++i == argc)
                die("argument to '--max-outputs' missing");
            cfg.max_outputs = atoi(argv[i]);
        } else if (!strcmp(arg, "--seed")) {
            if (++i == argc)
                die("argument to '--seed' missing");
            base_seed = atoi(argv[i]);
        } else if (arg[0] == '-' && arg[1] == '-')
            die("invalid option '%s'", arg);
        else if (arg[0] == '-')
            die("invalid command line option '%s'", arg);
        else
            output_pattern = arg;
    }

    if (base_seed == 0) {
        struct tms tp;
        unsigned tmp = (unsigned) times(&tp) * (unsigned) getpid();
        base_seed = tmp ? tmp : 42;
    }

    if (cfg.min_inputs > cfg.max_inputs)
        die("--min-inputs (%u) > --max-inputs (%u)", cfg.min_inputs,
            cfg.max_inputs);
    if (cfg.min_latches > cfg.max_latches)
        die("--min-latches (%u) > --max-latches (%u)", cfg.min_latches,
            cfg.max_latches);
    if (cfg.min_ands > cfg.max_ands)
        die("--min-ands (%u) > --max-ands (%u)", cfg.min_ands, cfg.max_ands);
    if (cfg.min_outputs > cfg.max_outputs)
        die("--min-outputs (%u) > --max-outputs (%u)", cfg.min_outputs,
            cfg.max_outputs);

    for (i = 0; i < count; i++) {
        aiger* model;
        const char* err;
        int ok;
        aiger_mode mode;
        char output_file[1024];

        cfg.seed = base_seed + i;
        cfg.add_comments = verbose;

        model = aigrandom_generate(&cfg);

        err = aiger_check(model);
        if (err)
            die("generated invalid AIG: %s", err);

        if (verbose)
            msg("seed %u: MILOA %u %u %u %u %u", cfg.seed, model->maxvar,
                model->num_inputs, model->num_latches, model->num_outputs,
                model->num_ands);

        if (output_pattern) {
            if (count > 1 && strstr(output_pattern, "%d"))
                snprintf(output_file, sizeof output_file, output_pattern, i);
            else if (count > 1)
                snprintf(output_file, sizeof output_file, "%s.%d",
                         output_pattern, i);
            else
                snprintf(output_file, sizeof output_file, "%s", output_pattern);

            ok = aiger_open_and_write_to_file(model, output_file);
            if (!ok)
                die("failed to write '%s'", output_file);

            if (verbose)
                msg("wrote '%s'", output_file);

            if (dot) {
                char dot_file[1024];
                const char* ext = strrchr(output_file, '.');
                if (ext && (!strcmp(ext, ".aig") || !strcmp(ext, ".aag"))) {
                    snprintf(dot_file, sizeof dot_file, "%.*s.dot",
                             (int) (ext - output_file), output_file);
                } else {
                    snprintf(dot_file, sizeof dot_file, "%s.dot", output_file);
                }
                {
                    FILE* df = fopen(dot_file, "w");
                    if (!df)
                        die("failed to open '%s' for writing", dot_file);
                    aigrandom_write_dot(model, df);
                    fclose(df);
                }
                if (verbose)
                    msg("wrote '%s'", dot_file);
            }
        } else {
            mode = (ascii || isatty(1)) ? aiger_ascii_mode : aiger_binary_mode;
            ok = aiger_write_to_file(model, mode, stdout);
            if (!ok)
                die("write error");
        }

        aiger_reset(model);
    }

    return 0;
}
