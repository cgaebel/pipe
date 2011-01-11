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
#include "pipe.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef min
#define min(a, b) ((a) <= (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) >= (b) ? (a) : (b))
#endif

// Runs a memcpy, then returns the end of the range copied.
// Has identical functionality as mempcpy, but is portable.
static inline void* offset_memcpy(void* restrict dest, const void* restrict src, size_t n)
{
    return (char*)memcpy(dest, src, n) + n;
}

/*
 * Pipe implementation overview
 * =================================
 *
 * A pipe is implemented as a circular buffer. There are two special cases for
 * this structure: nowrap and wrap.
 *
 * Nowrap:
 *
 *     buffer          begin               end                 bufend
 *       [               >==================>                    ]
 *
 * In this case, the data storage is contiguous, allowing easy access. This is
 * the simplest case.
 *
 * Wrap:
 *
 *     buffer        end                 begin                 bufend
 *       [============>                    >=====================]
 *
 * In this case, the data storage is split up, wrapping around to the beginning
 * of the buffer when it hits bufend. This case is more complex, as it ensures
 * the structure is maintained, and data can be easily copied in/out.
 *
 * Invariants:
 *
 * The invariants of a pipe are documented in the check_invariants function,
 * and checked frequently in debug builds. This helps restore sanity when
 * making modifications, but may slow down calls. It's best to disable the
 * checks in release builds.
 *
 * Thread-safety:
 *
 * pipe_t has been designed primarily with high threading workloads in mind.
 *
 * No complex threading tricks are used; there's a simple mutex
 * guarding the pipe, with a condition variable to signal when there are new
 * elements so the blocking consumers can get them. If you modify the pipe,
 * lock the mutex. Keep it locked for as short a time as possible.
 *
 * Complexity:
 *
 * Pushing and popping must run in O(n) where n is the number of elements being
 * inserted/removed. It must also run in O(1) with respect to the number of
 * elements currently in the pipe.
 *
 * Efficiency:
 *
 * Asserts are used liberally, and many of them, when inlined, can be turned
 * into no-ops. Therefore, it is recommended that you compile with -O1 in
 * debug builds, as the pipe can easily become a bottleneck.
 */
struct pipe {
    size_t elem_size;  // The size of each element. This is read-only and
                       // therefore does not need to be locked to read.
    size_t elem_count; // The number of elements currently in the pipe.
    size_t capacity;   // The maximum number of elements the buffer can hold
                       // before a reallocation.
    size_t min_cap;    // The smallest sane capacity before the buffer refuses
                       // to shrink.
    size_t max_cap;    // The maximum capacity of the pipe before push requests
                       // are blocked. This is read-only and therefore does not
                       // need to be locked to read.

    char * buffer,     // The internal buffer, holding the enqueued elements.
         * bufend,     // Just a shortcut pointer to the end of the buffer.
                       // It helps to not constantly type (p->buffer + p->elem_size*p->capacity).
         * begin,      // Always points to the least-recently pushed element in the pipe.
         * end;        // Always points to the most-recently pushed element in the pipe.

    size_t producer_refcount;      // The number of producers currently in circulation.
    size_t consumer_refcount;      // The number of consumers currently in circulation.

    pthread_mutex_t m;             // The mutex guarding the WHOLE pipe.

    pthread_cond_t  just_pushed;   // Signaled immediately after any push operation.
    pthread_cond_t  just_popped;   // Signaled immediately after any pop operation.
};

// Poor man's typedef. For more information, see DEF_NEW_FUNC's typedef.
struct producer { pipe_t pipe; };
struct consumer { pipe_t pipe; };

// Converts a pointer to either a producer or consumer into a suitable pipe_t*.
#define PIPIFY(producer_or_consumer) (&(producer_or_consumer)->pipe)

// The initial minimum capacity of the pipe. This can be overridden dynamically
// with pipe_reserve.
#ifdef DEBUG
#define DEFAULT_MINCAP  2
#else
#define DEFAULT_MINCAP  32
#endif

// Moves bufend to the end of the buffer in the event that bufend is not valid,
// and assuming buffer, capacity, and elem_size are all sane.
static inline void fix_bufend(pipe_t* p)
{
    p->bufend = p->buffer + p->elem_size * p->capacity;
}

// Does the buffer wrap around?
//   true  -> wrap
//   false -> nowrap
static inline bool wraps_around(const pipe_t* p)
{
    return p->begin > p->end;
}

// Is the pointer `p' within [left, right]?
static inline bool in_bounds(const void* left, const void* p, const void* right)
{
    return p >= left && p <= right;
}

// return p if it's before the end of the buffer, otherwise return the
// beginning.
static inline char* wrap_if_at_end(char* p, char* begin, const char* end)
{
    return p == end ? begin : p;
}

// round up to the next power of two
static inline size_t next_pow2(size_t n)
{
    for(size_t i = 1; i != 0; i <<= 1)
        if(n <= i)
            return i;

    // the number is too big to safely double
    return n;
}

// This function is called liberally through the codebase. It would be best to
// read this function over, as it also acts as documentation.
static void check_invariants(const pipe_t* p)
{
    assert(p);

    // p->buffer may be NULL. When it is, we must have no issued consumers.
    // It's just a way to save memory when all consumers have been deallocated
    // and push requests are still being made
    if(p->buffer == NULL)
    {
        assert(p->consumer_refcount == 0);
        return;
    }

    assert(p->bufend);
    assert(p->begin);
    assert(p->end);

    assert(p->elem_size != 0);

    assert(p->elem_count <= p->capacity && "There are more elements in the buffer than its capacity.");
    assert(p->bufend == p->buffer + p->elem_size*p->capacity && "This is axiomatic. Was fix_bufend not called somewhere?");

    assert(in_bounds(p->buffer, p->begin, p->bufend));
    assert(in_bounds(p->buffer, p->end, p->bufend));

    assert(p->min_cap >= DEFAULT_MINCAP);
    assert(p->min_cap <= p->max_cap);
    assert(p->capacity >= p->min_cap && p->capacity <= p->max_cap);

    assert(p->begin != p->bufend && "The begin pointer should NEVER point to the end of the buffer."
                    "If it does, it should have been automatically moved to the front.");

    // Ensure the size accurately reflects the begin/end pointers' positions.
    // Refer to the diagram in pipe's documentation.

    if(wraps_around(p)) //                   v     left half    v   v     right half     v
        assert(p->elem_size*p->elem_count == (p->end - p->buffer) + (p->bufend - p->begin));
    else
        assert(p->elem_size*p->elem_count == p->end - p->begin);
}

// Enforce is just assert, but runs the expression in release build, instead of
// filtering it out like assert would.
#ifdef NDEBUG
#define ENFORCE(expr) (void)(expr)
#else
#define ENFORCE assert
#endif

static inline void lock_pipe(pipe_t* p)
{
    ENFORCE(pthread_mutex_lock(&p->m) == 0);
    check_invariants(p);
}

static inline void unlock_pipe(pipe_t* p)
{
    check_invariants(p);
    ENFORCE(pthread_mutex_unlock(&p->m) == 0);
}

#define WHILE_LOCKED(stuff) do { lock_pipe(p); stuff unlock_pipe(p); } while(0)

static inline void init_mutex(pthread_mutex_t* m)
{
    pthread_mutexattr_t attr;

    ENFORCE(pthread_mutexattr_init(&attr) == 0);
    ENFORCE(pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE) == 0);

    ENFORCE(pthread_mutex_init(m, &attr) == 0);
}

pipe_t* pipe_new(size_t elem_size, size_t limit)
{
    assert(elem_size != 0);

    assert(sizeof(pipe_t) == sizeof(consumer_t));
    assert(sizeof(consumer_t) == sizeof(producer_t));

    pipe_t* p = malloc(sizeof(pipe_t));

    if(p == NULL)
        return NULL;

    p->elem_size = elem_size;
    p->elem_count = 0;
    p->capacity =
    p->min_cap  = DEFAULT_MINCAP;
    p->max_cap  = limit != 0 ? next_pow2(max(limit, p->min_cap)) : ~(size_t)0;

    p->buffer =
    p->begin  =
    p->end    = malloc(p->elem_size * p->capacity);

    fix_bufend(p);

    p->producer_refcount =
    p->consumer_refcount = 1;    // Since we're issuing a pipe_t, it counts as both
                                 // a producer and a consumer, since it can issue
                                 // new instances of both.

    init_mutex(&p->m);

    ENFORCE(pthread_cond_init(&p->just_pushed, NULL) == 0);
    ENFORCE(pthread_cond_init(&p->just_popped, NULL) == 0);

    check_invariants(p);

    return p;
}

// What we do after incrementing the refcount is casting our pipe to the
// appropriate handle. Since the handle is defined with pipe_t as the
// first member (therefore lying at offset 0), we can secretly pass around
// our pipe_t structure without the rest of the world knowing it. This also
// keeps us from needlessly mallocing (and subsequently freeing) handles.
#define DEF_NEW_FUNC(type)                     \
    type##_t* pipe_##type##_new(pipe_t* p)     \
    {                                          \
        WHILE_LOCKED( ++p->type##_refcount; ); \
        return (type##_t*)p;                   \
    }

DEF_NEW_FUNC(producer)
DEF_NEW_FUNC(consumer)

#undef DEF_NEW_FUNC

static inline bool requires_deallocation(const pipe_t* p)
{
    return p->producer_refcount == 0 && p->consumer_refcount == 0;
}

static inline void deallocate(pipe_t* p)
{
    pthread_mutex_unlock(&p->m);

    pthread_mutex_destroy(&p->m);

    pthread_cond_destroy(&p->just_pushed);
    pthread_cond_destroy(&p->just_popped);

    free(p->buffer);
    free(p);
}

void pipe_free(pipe_t* p)
{
    WHILE_LOCKED(
        assert(p->producer_refcount > 0);
        assert(p->consumer_refcount > 0);

        --p->producer_refcount;
        --p->consumer_refcount;

        if(requires_deallocation(p))
            return deallocate(p);
    );
}

void pipe_producer_free(producer_t* handle)
{
    pipe_t* p = PIPIFY(handle);

    WHILE_LOCKED(
        assert(p->producer_refcount > 0);

        --p->producer_refcount;

        if(requires_deallocation(p))
            return deallocate(p);
    );
}

static inline void free_and_null(char** p)
{
    free(*p);
    *p = NULL;
}

void pipe_consumer_free(consumer_t* handle)
{
    pipe_t* p = PIPIFY(handle);

    WHILE_LOCKED(
        assert(p->consumer_refcount > 0);

        --p->consumer_refcount;

        // If this was the last consumer out of the gate, we can deallocate the
        // buffer. It has no use anymore.
        if(p->consumer_refcount == 0)
            free_and_null(&p->buffer);

        if(requires_deallocation(p))
            return deallocate(p);
    );
}

// Returns the end of the buffer (i.e. buf + number_of_bytes_copied).
static inline char* copy_pipe_into_new_buf(const pipe_t* p, char* buf, size_t bufsize)
{
    assert(bufsize >= p->elem_size * p->elem_count && "Trying to copy into a buffer that's too small.");
    check_invariants(p);

    if(wraps_around(p))
    {
        buf = offset_memcpy(buf, p->begin, p->bufend - p->begin);
        buf = offset_memcpy(buf, p->buffer, p->end - p->buffer);
    }
    else
    {
        buf = offset_memcpy(buf, p->begin, p->end - p->begin);
    }

    return buf;
}

static void resize_buffer(pipe_t* p, size_t new_size)
{
    check_invariants(p);

    // Let's NOT resize beyond our maximum capcity. Thanks =)
    if(new_size >= p->max_cap)
        new_size = p->max_cap;

    // I refuse to resize to a size smaller than what would keep all our
    // elements in the buffer or one that is smaller than the minimum capacity.
    if(new_size <= p->elem_count || new_size < p->min_cap)
        return;

    size_t new_size_in_bytes = new_size*p->elem_size;

    char* new_buf = malloc(new_size_in_bytes);
    p->end = copy_pipe_into_new_buf(p, new_buf, new_size_in_bytes);

    free(p->buffer);

    p->buffer = p->begin = new_buf;
    p->capacity = new_size;

    fix_bufend(p);

    check_invariants(p);
}

static inline void push_without_locking(pipe_t* p, const void* elems, size_t count)
{
    check_invariants(p);

    if(p->elem_count + count > p->capacity)
        resize_buffer(p, next_pow2(p->elem_count + count));

    // Since we've just grown the buffer (if necessary), we now KNOW we have
    // enough room for the push. So do it!
 
    size_t bytes_to_copy = count*p->elem_size;

    // We cache the end locally to avoid wasteful dereferences of p.
    char* newend = p->end;

    // If we currently have a nowrap buffer, we may have to wrap the new
    // elements. Copy as many as we can at the end, then start copying into the
    // beginning. This basically reduces the problem to only deal with wrapped
    // buffers, which can be dealt with using a single offset_memcpy.
    if(!wraps_around(p))
    {
        size_t at_end = min(bytes_to_copy, p->bufend - p->end);

        newend = wrap_if_at_end(
                     offset_memcpy(newend, elems, at_end),
                     p->buffer, p->bufend);

        elems = (const char*)elems + at_end;
        bytes_to_copy -= at_end;
    }

    // Now copy any remaining data...
    newend = wrap_if_at_end(
                 offset_memcpy(newend, elems, bytes_to_copy),
                 p->buffer, p->bufend
             );

    // ...and update the end pointer and count!
    p->end         = newend;
    p->elem_count += count;

    check_invariants(p);
}

void pipe_push(producer_t* prod, const void* elems, size_t count)
{
    pipe_t* p = PIPIFY(prod);

    assert(elems && "Trying to push a NULL pointer into the pipe. That just won't do.");
    assert(p);

    if(count == 0)
        return;

    size_t max_cap = p->max_cap;

    // If we're trying to push in more than the maximum capacity, we can split
    // up the elements into two sets. One which is just big enough, and the
    // other can be whatever size, as it will also hit the recursive case if
    // it's too large.
    if(count > max_cap)
    {
        size_t bytes_needed = max_cap * p->elem_size;

        pipe_push(prod, elems, bytes_needed);
        pipe_push(prod, (const char*)elems + bytes_needed, count - max_cap);
        return;
    }

    size_t elems_pushed = 0;

    WHILE_LOCKED(
        // Wait for there to be enough room in the buffer for some new elements.
        while(p->elem_count == max_cap && p->consumer_refcount > 0)
            pthread_cond_wait(&p->just_popped, &p->m);

        // Don't perform an actual push if we have no consumers issued. The
        // buffer's been freed.
        if(p->consumer_refcount == 0)
            return unlock_pipe(p);

        // Push as many elements into the queue as possible.
        push_without_locking(p, elems,
            elems_pushed = min(count, max_cap - p->elem_count)
        );

        assert(elems_pushed > 0);
    );

    pthread_cond_broadcast(&p->just_pushed);

    // now get the rest of the elements, which we didn't have enough room for
    // in the pipe.
    size_t elems_left = count - elems_pushed;
    pipe_push(prod, (const char*)elems + elems_pushed*p->elem_size, elems_left);
}

// wow, I didn't even intend for the name to work like that...
static inline size_t pop_without_locking(pipe_t* p, void* target, size_t count)
{
    check_invariants(p);

    const size_t elems_to_copy   = min(count, p->elem_count);
          size_t bytes_remaining = elems_to_copy * p->elem_size;

    assert(bytes_remaining <= p->elem_count*p->elem_size);

    // We're about to pop the elements. Fix the count now.
    p->elem_count -= elems_to_copy;

//  Copy [begin, min(bufend, begin + bytes_to_copy)) into target.
    {
        // Copy either as many bytes as requested, or the available bytes in
        // the RHS of a wrapped buffer - whichever is smaller.
        size_t first_bytes_to_copy = min(bytes_remaining, p->bufend - p->begin);

        target = offset_memcpy(target, p->begin, first_bytes_to_copy);

        bytes_remaining -= first_bytes_to_copy;
        p->begin         = wrap_if_at_end(
                               p->begin + first_bytes_to_copy,
                               p->buffer, p->bufend);
    }

    // If we're dealing with a wrap buffer, copy the remaining bytes
    // [buffer, buffer + bytes_to_copy) into target.
    if(bytes_remaining > 0)
    {
        memcpy(target, p->buffer, bytes_remaining);
        p->begin = wrap_if_at_end(p->begin + bytes_remaining, p->buffer, p->bufend);
    }    

    check_invariants(p);

    // To conserve space like the good computizens we are, we'll shrink
    // our buffer if our memory usage efficiency drops below 25%. However,
    // since shrinking/growing the buffer is the most expensive part of a push
    // or pop, we only shrink it to bring us up to a 50% efficiency. A common
    // pipe usage pattern is sudden bursts of pushes and pops. This ensures it
    // doesn't get too time-inefficient.
    if(p->elem_count <= (p->capacity / 4))
        resize_buffer(p, p->capacity / 2);

    return elems_to_copy;
}

size_t pipe_pop(consumer_t* c, void* target, size_t count)
{
    pipe_t* p = PIPIFY(c);

    assert(target && "Why are we trying to pop elements out of a pipe and into a NULL buffer?");
    assert(p);

    if(count > p->max_cap)
        count = p->max_cap;

    size_t ret;

    WHILE_LOCKED(
        // While we need more elements and there exists at least one producer...
        while(p->elem_count < count && p->producer_refcount > 0)
            pthread_cond_wait(&p->just_pushed, &p->m);

        ret = p->elem_count > 0
              ? pop_without_locking(p, target, count)
              : 0;
    );

    pthread_cond_broadcast(&p->just_popped);

    return ret;
}

void pipe_reserve(pipe_t* p, size_t count)
{
    if(p == NULL)
        return;

    if(count == 0)
        count = DEFAULT_MINCAP;

    WHILE_LOCKED(
        if(count > p->elem_count)
        {
            p->min_cap = min(count, p->max_cap);
            resize_buffer(p, count);
        }
    );
}
