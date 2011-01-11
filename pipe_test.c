#include "pipe.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define countof(a) (sizeof(a)/sizeof((a)[0]))

#define array_eq(a1, a2)              \
    (sizeof(a1) == sizeof(a2)         \
    ? memcmp(a1, a2, sizeof(a1) == 0) \
    : 0)

#define array_eq_len(type, a1, a2, len2) \
    (sizeof(a1) == sizeof(type)*(len2)   \
    ? memcmp(a1, a2, sizeof(a1)) == 0    \
    : 0)

// This test answers the question: "Can we use a pipe like a normal queue?"
DEF_TEST(basic_storage)
{
    pipe_t* pipe = pipe_new(sizeof(int), 0);
    producer_t* p = pipe_producer_new(pipe);
    consumer_t* c = pipe_consumer_new(pipe);
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

    assert(array_eq_len(int, expecteda, bufa, acnt));
    assert(array_eq_len(int, expectedb, bufb, bcnt));

    pipe_consumer_free(c);
}

#define RUN_TEST(name) \
    do { test_##name(); printf("%s -> [  OK  ]\n", #name); } while(0)

void pipe_run_test_suite()
{
    RUN_TEST(basic_storage);
}
