/**
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

/*
 * A pipe is a collection of elements, enqueued and dequeued in a FIFO pattern.
 * The beauty of it lies in that pushing and popping may be done in multiple
 * threads, with no need for external synchronization. This makes it ideal for
 * assisting in a producer/consumer concurrency model.
 *
 * pipe_t
 *
 * pipe_t is used as a god handle. If there is a valid pipe_t in circulation,
 * you may create producer_t and consumer_t handles from it. It also allows you
 * to run maintainance tasks such as pipe_reserve. Since you can create
 * producer_t handles from it, a pipe_t "counts" as a producer. Therefore,
 * consumers will still wait for elements if a pipe_t has been issued, even if
 * there are no other producers. Try and keep the pipe_t allocated for as short
 * a time as possible. This means you should make all your producer and
 * consumer handles at the start, then use those for the duration of your task.
 *
 *   Sample code:
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
 * producer_t
 *
 * producer_t is used for pushing into the pipe. It is recommended that each
 * thread has their own (as it keeps ownership semantics simple), however, it
 * is not mandatory. As long as there is at least one producer_t in circulation,
 * consumers will block until they can fill their buffers.
 *
 * consumer_t
 *
 * consumer_t is used for popping from the pipe. It is recommended that each
 * thread has their own (as it keeps ownership semantics simple), however, it
 * is not mandatory. As long as there is at least one producer_t in circulation,
 * a consumer_t will block until the buffer can be filled.
 *
 *   Sample code:
 *
 *   #include "pipe.h"
 *
 *   #define BUFSIZE    1024
 *
 *   void do_stuff(consumer_t* p)
 *   {
 *       int buf[BUFSIZE];
 *       size_t bytes_read;
 *
 *       while((bytes_read = pipe_pop(p, buf, BUFSIZE)))
 *           process(buf, bytes_read);
 *
 *       pipe_consumer_free(p);
 *   }
 *
 * Guarantees:
 *
 * If, in a single thread, elements are pushed in the order [a, b, c, d, ...],
 * they will be popped in that order. Note that they may not necessarily be
 * popped together.
 *
 * The underlying pipe will exist until all the handles have been freed.
 *
 * All functions are re-entrant. All synchronization is handled internally.
 */
typedef struct pipe     pipe_t;

typedef struct producer producer_t;
typedef struct consumer consumer_t;

// Initializes a new pipe storing elements of size `elem_size'. A pusher handle
// is returned, from which you may push elements into the pipe.
//
// If `limit' is 0, the pipe has no maximum size. If it is nonzero, the pipe
// will never have more than `limit' elements in it at any one time. This can
// help prevent an explosion of memory usage in cases where production is
// significantly faster than consumption. In most cases, you want this to be 0.
pipe_t*     pipe_new(size_t elem_size, size_t limit);

// Makes a production handle to the pipe, allowing push operations. Note that
// this function is extremely cheap, as it cheats and doesn't even allocate
// memory.
producer_t* pipe_producer_new(pipe_t* p);

// Makes a consumption handle to the pipe, allowing pop operations. Note that
// this function is extremely cheap, as it cheats and doesn't even allocate
// memory.
consumer_t* pipe_consumer_new(pipe_t* p);

/*
 * As a general rule of thumb, if you call *_new, you must call the matching
 * *_free. Failure to do so may result in resource leaks, undefined behavior,
 * and spontaneous combustion.
 */

void pipe_free(pipe_t* p);
void pipe_producer_free(producer_t* p);
void pipe_consumer_free(consumer_t* p);

// Copies `count' elements from `elems' into the pipe.
void pipe_push(producer_t* p, const void* elems, size_t count);

// Tries to pop `count' elements out of the pipe and into `target', returning
// the number of elements successfully copied. If there aren't at least `count'
// elements currently in the pipe, this function will block until:
//
//   a) there are enough elements to fill the request entirely or
//   b) the pipe's maximum capacity has been hit or
//   c) all producer_t handles have been freed (including the parent pipe_t).
//
// Therefore, if this function returns 0, there will be no more elements coming
// in. Every subsequent call will return 0.
size_t pipe_pop(consumer_t* p, void* target, size_t count);

// Modifies the pipe to never have room for less than `count' elements.
// This can be useful if you want to save memory (if the pipe will always be
// short) or if you tend to burst your requests (such as random 1024 element
// pushes).
//
// The default minimum is 32 elements. To reset the reservation size to the
// default, set count to 0.
void pipe_reserve(pipe_t* p, size_t count);

// Use this to run the pipe self-test. It will call abort() if anything is
// wrong. You probably don't want to call this. If you never do, you don't need
// to link pipe_test.c
void pipe_run_test_suite();
