#pragma once
#include <stddef.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifndef restrict
#define restrict
#endif

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
pipe_t*   pipe_new(size_t elem_size);

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
void pipe_push(producer_t* restrict p, const void* restrict elems, size_t count);

// Tries to pop `count' elements out of the pipe and into `target', returning
// the number of elements successfully copied. If there aren't at least `count'
// elements currently in the pipe, this function will block until:
//
//   a) there are enough elements to fill the request entirely or
//   b) all producer_t handles have been freed (including the parent pipe_t).
//
// Therefore, if this function returns anything except for `count', there will
// be no more elements coming in. Every subsequent call will return 0.
size_t pipe_pop(consumer_t* restrict p, void* restrict target, size_t count);

// Modifies the pipe to never have room for less than `count' elements.
// This can be useful if you want to save memory (if the pipe will always be
// short) or if you tend to burst your requests (such as random 1024 element
// pushes).
//
// The default minimum is 32 elements. To reset the reservation size to the
// default, set count to 0.
void pipe_reserve(pipe_t* p, size_t count);

#ifdef __cplusplus

// NOTE: `T' should be a POD struct. If T has fancy construction/destruction
// semantics, everything will fuck up. It will be freely memcpy'd, malloc'd,
// and free'd.
template <typename T>
class Pipe
{
public:
    class Producer
    {
    private:
        friend class Pipe<T>;

        pipe_t* parent;
        producer_t* p;

        Producer(pipe_t* pipe) : p(pipe_producer_new(pipe)) {}

    public:
        Producer(const Producer& o) : parent(o.parent), p(pipe_producer_new(o.parent)) {}

        void push(const T* elems, size_t count) { pipe_push(p, (const void*)elems, count); }

        ~Producer() { pipe_producer_free(p); }
    };

   class Consumer
    {
    private:
        friend class Pipe<T>;

        pipe_t* parent;
        consumer_t* p;

        Consumer(pipe_t* pipe) : parent(pipe), p(pipe_consumer_new(pipe)) {}

    public:
        Consumer(const Consumer& o) : parent(o.parent), p(pipe_consumer_new(o.parent)) {}

        size_t pop(T* target, size_t count) { return pipe_pop(p, target, count); }

        ~Consumer() { pipe_consumer_free(p); }
    };

private:
    pipe_t* p;

    Pipe(const Pipe&); // = delete;

public:
    Pipe() : p(pipe_new(sizeof(T))) {}

    Producer new_producer() { return Producer(p); }
    Consumer new_consumer() { return Consumer(p); }

    void reserve(size_t count) { pipe_reserve(p, count); }

    ~Pipe() { pipe_free(p); }
};

#endif
