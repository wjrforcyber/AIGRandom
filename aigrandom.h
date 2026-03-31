/***************************************************************************
Copyright (c) 2026, aigerRandom contributors.

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

#ifndef aigrandom_h_INCLUDED
#define aigrandom_h_INCLUDED

#include "aiger.h"

typedef struct aigrandom_config aigrandom_config;

struct aigrandom_config {
    unsigned seed;
    unsigned min_inputs;
    unsigned max_inputs;
    unsigned min_latches;
    unsigned max_latches;
    unsigned min_ands;
    unsigned max_ands;
    unsigned min_outputs;
    unsigned max_outputs;
    unsigned num_layers;
    int output_ascii;
    int add_symbols;
    int add_comments;
    int sequential;
};

aiger* aigrandom_generate(aigrandom_config*);
int aigrandom_write_dot(aiger*, FILE*);

#endif
