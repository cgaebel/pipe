/* pipe_test.c - The pipe's unit tests. This does not need to be linked unless
 *               you plan on testing the pipe.
 *
 * The MIT License
 * Copyright (c) 2011 Clark Gaebel <cg.wowus.cg@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "pipe.h"
#include "pipe_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED_PARAMETER(var) (var) = (var)

// All this hackery is just to get asserts to work in release build.

#ifdef NDEBUG
#define NDEBUG_WAS_DEFINED
#undef NDEBUG
#endif

#include <assert.h>

#ifdef NDEBUG_WAS_DEFINED
#undef NDEBUG_WAS_DEFINED
#define NDEBUG
#endif

#define DEF_TEST(name) \
    static void test_##name()

#define countof(a) (sizeof(a)/sizeof(*(a)))

#define array_eq(a1, a2)              \
    (sizeof(a1) == sizeof(a2)         \
    ? memcmp(a1, a2, sizeof(a1) == 0) \
    : 0)


#define array_eq_len(a1, a2, len2) \
    (sizeof(a1) == sizeof((a2)[0])*(len2)   \
    ? memcmp(a1, a2, sizeof(a1)) == 0    \
    : 0)

// This test answers the question: "Can we use a pipe like a normal queue?"
DEF_TEST(basic_storage)
{
    pipe_t* pipe = pipe_new(sizeof(int), 0);
    pipe_producer_t* p = pipe_producer_new(pipe);
    pipe_consumer_t* c = pipe_consumer_new(pipe);
    pipe_free(pipe);

    int a[] = { 0, 1, 2, 3, 4 };
    int b[] = { 9, 8, 7, 6, 5 };

    pipe_push(p, a, countof(a));
    pipe_push(p, b, countof(b));

    pipe_producer_free(p);

    int bufa[6];
    int bufb[10];

    size_t acnt = pipe_pop(c, bufa, countof(bufa)),
           bcnt = pipe_pop(c, bufb, countof(bufb));

    int expecteda[] = {
        0, 1, 2, 3, 4, 9
    };

    int expectedb[] = {
        8, 7, 6, 5
    };

    assert(array_eq_len(expecteda, bufa, acnt));
    assert(array_eq_len(expectedb, bufb, bcnt));

    pipe_consumer_free(c);
}

typedef struct {
    int orig;
    int new;
} testdata_t;

static void double_elems(const void* elems, size_t count, pipe_producer_t* out, void* aux)
{
    UNUSED_PARAMETER(aux);

    if(count == 0)
        return;

    testdata_t outbuffer[count];

    memcpy(outbuffer, elems, count*sizeof(testdata_t));

    for(size_t i = 0; i < count; ++i)
        outbuffer[i].new *= 2;

    pipe_push(out, outbuffer, count);
}

#ifdef PIPE_DEBUG
#define MAX_NUM     250000
#else
#define MAX_NUM     500000
#endif

static void generate_test_data(pipe_producer_t* p)
{
    for(int i = 0; i < MAX_NUM; ++i)
    {
        testdata_t t = { i, i };
        pipe_push(p, &t, 1);
    }
}

static inline void validate_test_data(testdata_t t, int multiplier)
{
    assert(t.new == t.orig*multiplier);
}

static void validate_consumer(pipe_consumer_t* c, unsigned doublings)
{
    testdata_t t;

    while(pipe_pop(c, &t, 1))
        validate_test_data(t, 1 << doublings);
}

DEF_TEST(pipeline_multiplier)
{
    pipeline_t pipeline =
        pipe_pipeline(sizeof(testdata_t),
                      &double_elems, (void*)NULL, sizeof(testdata_t),
                      &double_elems, (void*)NULL, sizeof(testdata_t),
                      &double_elems, (void*)NULL, sizeof(testdata_t),
                      &double_elems, (void*)NULL, sizeof(testdata_t),
                      &double_elems, (void*)NULL, sizeof(testdata_t),
                      &double_elems, (void*)NULL, sizeof(testdata_t),
                      &double_elems, (void*)NULL, sizeof(testdata_t),
                      &double_elems, (void*)NULL, sizeof(testdata_t),
                      (void*)NULL
                     );

    assert(pipeline.in);
    assert(pipeline.out);

    generate_test_data(pipeline.in); pipe_producer_free(pipeline.in);
    validate_consumer(pipeline.out, 8);  pipe_consumer_free(pipeline.out);
}

DEF_TEST(parallel_multiplier)
{
    pipeline_t pipeline =
        pipe_parallel(4,
                      sizeof(testdata_t),
                      &double_elems, (void*)NULL,
                      sizeof(testdata_t));

    assert(pipeline.in);
    assert(pipeline.out);

    generate_test_data(pipeline.in); pipe_producer_free(pipeline.in);
    validate_consumer(pipeline.out, 1); pipe_consumer_free(pipeline.out);
}

struct Foo
{
    int a;
    int b;
    int c;
};

DEF_TEST(issue_4)
{
    pipe_t* p = pipe_new(sizeof(struct Foo), 0);
    pipe_producer_t* producer = pipe_producer_new(p);
    pipe_consumer_t* consumer = pipe_consumer_new(p);

    for(int i = 0; i < 22; ++i)
    {
        struct Foo f;
        pipe_push(producer, &f, 1);
    }

    for(int i = 0; i < 22; ++i)
    {
        struct Foo f;
        (void)pipe_pop(consumer, &f, 1);
    }

    for(int i = 0; i < 21; ++i)
    {
        struct Foo f;
        pipe_push(producer, &f, 1);
        (void)pipe_pop(consumer, &f, 1);
    }

    pipe_producer_free(producer);
    pipe_consumer_free(consumer);

    pipe_free(p);
}

/*
// This test is only legal if DEFAULT_MINCAP is less than or equal to 8.
//
// Therefore, this test is disabled in release mode.
#ifdef PIPE_DEBUG
DEF_TEST(clobbering)
{
    pipe_t* p = pipe_new(sizeof(int), 5);

    pipe_producer_t* pro = pipe_producer_new(p);
    pipe_consumer_t* con = pipe_consumer_new(p);

    pipe_free(p);

    const int x[] = { 1, 2, 3 };
    int y[3];

    pipe_push_clobber(pro, x, 3);
    size_t popped = pipe_pop(con, y, 3);
    assert(popped == 3);
    assert(array_eq_len(x, y, 3));

    int x2[] = { 2, 3, 1, 2, 3, 1, 2, 3 };
    int y2[8];

    pipe_push_clobber(pro, x, 3);
    pipe_push_clobber(pro, x, 3);
    pipe_push_clobber(pro, x, 3);
    popped = pipe_pop(con, y2, 8);
    assert(popped == 8);

    assert(array_eq_len(x2, y2, 8));

    pipe_producer_free(pro);
    pipe_consumer_free(con);
}
#endif
*/

/*
 * TEST IDEAS:
 *
 * - Create a fuzzer. Output random seed at program start (allow seed to be
 *   passed as a parameter). Put random amounts (and values) of data in one end
 *   of the queue, have some algorithm processing it. Do this whole bunches. If
 *   shit goes south, we can restart the program with the random seed and get a
 *   reproducable thing. Or get core dumps. Whichever is easier. Do something
 *   simple like randomly putting in every number from 1-10000, then ensuring
 *   that all the numbers are recieved on the other end, even with multiple
 *   consumers (and possibly multiple producers).
 */

#define RUN_TEST(name) \
    do { printf("%s ->", #name); test_##name(); printf(" [  OK  ]\n"); } while(0)

void pipe_run_test_suite(void)
{
    RUN_TEST(basic_storage);
    RUN_TEST(pipeline_multiplier);
    RUN_TEST(parallel_multiplier);
    RUN_TEST(issue_4);
/*
#ifdef PIPE_DEBUG
    RUN_TEST(clobbering);
#endif
*/
}

/* vim: set et ts=4 sw=4 softtabstop=4 textwidth=80: */
