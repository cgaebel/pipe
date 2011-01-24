/*
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
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __GNUC__
#define PURE __attribute__((pure))
#else
#define PURE
#endif

/*
 * A pipe is a collection of elements, enqueued and dequeued in a FIFO pattern.
 * The beauty of it lies in that pushing and popping may be done in multiple
 * threads, with no need for external synchronization. This makes it ideal for
 * assisting in a producer/consumer concurrency model.
 *
 * pipe_t
 *
 * If there is a valid pipe_t in circulation, you may create producer_t and
 * consumer_t handles from it. It also allows you to run maintainance tasks
 * such as pipe_reserve.
 *
 * Sample code:
 *
 *   #include "pipe.h"
 *
 *   #define THREADS 4
 *
 *   pipe_t* p = pipe_new(sizeof(int));
 *
 *   producer_t* pros[THREADS] = { pipe_producer_new(p) };
 *   consumer_t* cons[THREADS] = { pipe_consumer_new(p) };
 *
 *   pipe_free(p);
 *
 *   // At this point, you can freely use the producer and consumer handles.
 *
 *   // Then clean them up when you're done!
 *   for(int i = 0; i < THREADS; ++i)
 *   {
 *     pipe_producer_free(pros[i]);
 *     pipe_consumer_free(pros[i]);
 *   }
 *
 * producer_t
 *
 * producer_t is used for pushing into the pipe. It is recommended that each
 * thread has their own (as it keeps ownership semantics simple); however, it
 * is not mandatory. As long as there is at least one producer_t in circulation,
 * consumers will block until they can fill their buffers. A pipe_t also counts
 * as a producer_t, since valid producer_t handles can be created from it.
 *
 * consumer_t
 *
 * consumer_t is used for popping from the pipe. It is recommended that each
 * thread has their own (as it keeps ownership semantics simple), however, it
 * is not mandatory. As long as there is at least one producer_t or pipe_t in
 * circulation, a consumer_t will block until the buffer can be filled. A
 * pipe_t also counts as a consumer_t, since valid consumer_t handles can be
 * created from it.
 *
 * Sample code:
 *
 * #include "pipe.h"
 *
 * #define BUFSIZE 1024
 *
 * void do_stuff(consumer_t* p)
 * {
 *   int buf[BUFSIZE];
 *   size_t bytes_read;
 *
 *   while((bytes_read = pipe_pop(p, buf, BUFSIZE)))
 *     process(buf, bytes_read);
 *
 *   pipe_consumer_free(p);
 * }
 *
 * Try and keep the pipe_t allocated for as short a time as possible. This
 * means you should make all your producer and consumer handles at the start,
 * deallocate the pipe_t, then use producers and consumers for the duration
 * of your task.
 *
 * pipe_generic_t
 *
 * Generic pipe pointers can be created from pipe_t's, producer_t's, or
 * consumer_t's with the PIPE_GENERIC macro. This new pointer can then be used
 * as a parameter to any function requiring it, allowing certain operations to
 * be performed regardless of the type.
 *
 * Guarantees:
 *
 * If, in a single thread, elements are pushed in the order [a, b, c, d, ...],
 * they will be popped in that order. Note that they may not necessarily be
 * popped together.
 *
 * The underlying pipe will exist until all the handles have been freed.
 *
 * All functions are re-entrant. Synchronization is handled internally.
 */
typedef struct pipe         pipe_t;
typedef struct producer     producer_t;
typedef struct consumer     consumer_t;
typedef struct pipe_generic pipe_generic_t;

#define PIPE_GENERIC(handle) ((pipe_generic_t*)(handle))

/*
 * Initializes a new pipe storing elements of size `elem_size'. A pusher handle
 * is returned, from which you may push elements into the pipe.
 *
 * If `limit' is 0, the pipe has no maximum size. If it is nonzero, the pipe
 * will never have more than `limit' elements in it at any time. In most cases,
 * you want this to be 0. However, limits help prevent an explosion of memory
 * usage in cases where production is significantly faster than consumption.
 */
pipe_t* pipe_new(size_t elem_size, size_t limit);

/*
 * Makes a production handle to the pipe, allowing push operations. This
 * function is extremely cheap: it doesn't allocate memory.
 */
producer_t* pipe_producer_new(pipe_t*);

/*
 * Makes a consumption handle to the pipe, allowing pop operations. This
 * function is extremely cheap: it doesn't allocate memory.
 */
consumer_t* pipe_consumer_new(pipe_t*);

/*
 * If you call *_new, you must call the corresponding *_free. Failure to do so
 * may result in resource leaks, undefined behavior, and spontaneous combustion.
 */

void pipe_free(pipe_t*);
void pipe_producer_free(producer_t*);
void pipe_consumer_free(consumer_t*);

/* Copies `count' elements from `elems' into the pipe. */
void pipe_push(producer_t*, const void* elems, size_t count);

/*
 * Tries to pop `count' elements out of the pipe and into `target', returning
 * the number of elements successfully copied. If there aren't at least `count'
 * elements currently in the pipe, this function will block until:
 *
 * a) there are enough elements to fill the request entirely, or
 * b) all producer_t handles have been freed (including the parent pipe_t).
 *
 * If this function returns 0, there will be no more elements coming in. Every
 * subsequent call will return 0.
 */
size_t pipe_pop(consumer_t*, void* target, size_t count);

/*
 * Modifies the pipe to have room for at least `count' elements. If more room
 * is already allocated, the call does nothing. This can be useful if requests
 * tend to come in bursts.
 *
 * The default minimum is 32 elements. To reset the reservation size to the
 * default, set count to 0.
 */
void pipe_reserve(pipe_generic_t*, size_t count);

/*
 * Determines the size of a pipe's elements. This can be used for generic
 * pipe-processing algorithms to reserve appropriately-sized buffers.
 */
size_t PURE pipe_elem_size(pipe_generic_t*);

/*
 * A function that can be used for processing a pipe.
 *
 * elem_in  - an array of elements to process
 * count    - the number of elements in `elem_in'
 * elem_out - The producer that may be pushed into to continue the chain
 * aux      - auxilary data previously passed to pipe_connect.
 */
typedef void (*pipe_processor_t)(const void* /* elem_in */,
                                 size_t      /* count */,
                                 producer_t* /* elem_out */,
                                 void*       /* aux */
                                );

typedef struct {
    producer_t* p;
    consumer_t* c;
} pipeline_t;

/*
 * A pipeline consists of a list of functions and pipes. As data is recieved in
 * one end, it is processed by each of the pipes and pushed into the other end.
 * Each stage's processing is done in a seperate thread. The last parameter must
 * be NULL (in place of a pipe_processor_t) if you want to have a consumer_t
 * returned, or 0 (in place of a sizeof()) if you don't want or need a consumer_t.
 * If the last parameter replaces a sizeof(), the return value's `c' member will
 * be NULL.
 *
 * When passing NULL `aux' pointers to your functors, you MUST cast them to
 * void* to maintain 64-bit compatibility. The C standard only requires NULL to
 * be defined as 0, so will be cast to a 32-bit wide int. This will destroy
 * alignment since pipe_pipeline looks for a void* in that space.
 *
 * Sample:
 *  pipeline_t p = pipe_pipeline(sizeof(int),
 *                               &int_to_float,       &i2f_data,   sizeof(float),
 *                               &float_to_garbage,   &f2g_data,   sizeof(garbage),
 *                               &garbage_to_awesome, (void*)NULL, sizeof(awesome),
 *                               (void*)NULL
 *                              );
 *
 *  // Now push all your ints into p.p ...
 *
 *  pipe_producer_free(p.p);
 *
 *  // Now pop all your awesome out of p.c ...
 *
 *  pipe_consumer_free(p.c);
 *
 *  NOTE: All the functions must be of type pipe_processor_t. This call will
 *  return a pipeline which takes the type specified by the first parameter
 *  [int] and returns the last type [awesome] (or NULL if the last vararg was a
 *  function).
 */
pipeline_t pipe_pipeline(size_t first_size, ...);

/*
 * Use this to run the pipe self-test. It will call abort() if anything is
 * wrong. This is usually unnecessary. If this is never called, pipe_test.c
 * does not need to be linked.
 */
void pipe_run_test_suite();

#undef PURE

#ifdef __cplusplus
}
#endif

/* vim: set et ts=4 sw=4 softtabstop=4 textwidth=80: */
