/* pipe.c - The pipe implementation. This is the only file that must be linked
 *          to use the pipe.
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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Vanity bytes. As long as this isn't removed from the executable, I don't
// mind if I don't get credits in a README or any other documentation. Consider
// this your fulfillment of the MIT license.
const char _pipe_copyright[] =
    __FILE__
    " : Copyright (c) 2011 Clark Gaebel <cg.wowus.cg@gmail.com> (MIT License)";

#ifndef min
#define min(a, b) ((a) <= (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) >= (b) ? (a) : (b))
#endif

#ifdef __GNUC__
#define likely(cond)   __builtin_expect(!!(cond), 1)
#define unlikely(cond) __builtin_expect(  (cond), 0)
#define CONSTEXPR __attribute__((const))
#else
#define likely(cond)   (cond)
#define unlikely(cond) (cond)
#define CONSTEXPR
#endif

#ifdef NDEBUG
    #if defined(_MSC_VER)
        #define assertume __assume
    #else // _MSC_VER
        #define assertume assert
    #endif // _MSC_VER
#else // NDEBUG
    #define assertume assert
#endif // NDEBUG

// Runs a memcpy, then returns the end of the range copied.
// Has identical functionality as mempcpy, but is portable.
static inline void* offset_memcpy(void* restrict dest,
                                  const void* restrict src,
                                  size_t n)
{
    memcpy(dest, src, n);
    return (char*)dest + n;
}

// Adds padding to a struct of `amount' bytes.
#define PAD1(amount, line) char _pad_##line[(amount)]
#define PAD0(amount, line) PAD1(amount, line)
#define PAD(amount) PAD0(amount, __LINE__)

// Pads a variable of type `type' to a 64-byte cache line, to prevent false
// sharing.
#define ALIGN_TO_CACHE(type, name) type name; PAD(64 - sizeof(type))

// The number of spins to do before performing an expensive kernel-mode context
// switch. This is a nice easy value to tweak for your application's needs. Set
// it to 0 if you want the implementation to decide, a low number if you are
// copying many objects into pipes at once (or a few large objects), and a high
// number if you are coping small or few objects into pipes at once.
#define MUTEX_SPINS 8192

// Standard threading stuff. This lets us support simple synchronization
// primitives on multiple platforms painlessly.

#if defined(_WIN32) || defined(_WIN64) // use the native win32 API on windows

#include <windows.h>

// On vista+, we have native condition variables and fast locks. Yay.
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0600

#define mutex_t             SRWLOCK

#define mutex_init          InitializeSRWLock
#define mutex_lock          AcquireSRWLockExclusive
#define mutex_unlock        ReleaseSRWLockExclusive
#define mutex_destroy(m)

#define cond_t              CONDITION_VARIABLE

#define cond_init           InitializeConditionVariable
#define cond_signal         WakeConditionVariable
#define cond_broadcast      WakeAllConditionVariable
#define cond_wait(c, m)     SleepConditionVariableSRW((c), (m), INFINITE, 0)
#define cond_destroy(c)

// Oh god. Microsoft has slow locks and lacks native condition variables on
// anything lower than Vista. Looks like we're rolling our own today.
#else /* vista+ */

#define mutex_t         CRITICAL_SECTION

#define mutex_init(m)   InitializeCriticalSectionAndSpinCount((m), MUTEX_SPINS)
#define mutex_lock      EnterCriticalSection
#define mutex_unlock    LeaveCriticalSection
#define mutex_destroy   DeleteCriticalSection

#define cond_t          HANDLE

// cond_signal may be the same as cond_broadcast, since our code is resistant to
// spurious wakeups.
#define cond_signal     SetEvent
#define cond_broadcast  SetEvent
#define cond_destroy    CloseHandle

static inline void cond_init(cond_t* c)
{
    *c = CreateEvent(NULL, FALSE, FALSE, NULL);
}

static inline void cond_wait(cond_t* c, mutex_t* m)
{
    mutex_unlock(m);

    // We wait for the signal (which only signals ONE thread), propogate it,
    // then lock our mutex and return. This can potentially lead to a lot of
    // spurious wakeups, but it does not affect the correctness of the code.
    // This method has the advantage of being dead-simple, though.
    WaitForSingleObject(c, INFINITE);
    cond_signal(c);

    mutex_lock(m);
}

#endif /* vista+ */

// Fall back on pthreads if we haven't special-cased the current OS.
#else /* windows */

#include <pthread.h>

#define mutex_t pthread_mutex_t
#define cond_t  pthread_cond_t

#define mutex_init(m)  pthread_mutex_init((m), NULL)

// Since we can't use condition variables on spinlocks, we'll roll our own spinlocks
// out of trylocks! This gave me a 25% improvement in the execution speed of the
// test suite.
static void mutex_lock(mutex_t* m)
{
    for(size_t i = 0; i < MUTEX_SPINS; ++i)
        if(pthread_mutex_trylock(m) == 0)
            return;

    pthread_mutex_lock(m);
}

#define mutex_unlock   pthread_mutex_unlock
#define mutex_destroy  pthread_mutex_destroy

#define cond_init(c)   pthread_cond_init((c), NULL)
#define cond_signal    pthread_cond_signal
#define cond_broadcast pthread_cond_broadcast
#define cond_wait      pthread_cond_wait
#define cond_destroy   pthread_cond_destroy

#endif /* windows */

// End threading.

// Begin atomics.

#if defined(__GNUC__)

typedef volatile size_t atomic_t;

static inline size_t atomic_inc(atomic_t* a)
{
    return __sync_add_and_fetch(a, 1);
}

static inline size_t atomic_dec(atomic_t* a)
{
    return __sync_sub_and_fetch(a, 1);
}

#elif defined(_WIN32) || defined(_WIN64)

#include <windows.h>

typedef volatile LONG atomic_t;

#define atomic_inc InterlockedIncrement
#define atomic_dec InterlockedDecrement

#else
#error "We need atomic increment and decrement. Please fill them out for your platform."
#endif

// End atomics.

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
 * of the buffer when it hits bufend. Hackery must be done in this case to
 * ensure the structure is maintained and data can be easily copied in/out.
 *
 * Invariants:
 *
 * The invariants of a pipe are documented in the check_invariants function,
 * and double-checked frequently in debug builds. This helps restore sanity when
 * making modifications, but may slow down calls. It's best to disable the
 * checks in release builds.
 *
 * Thread-safety:
 *
 * pipe_t has been designed with high threading workloads foremost in my mind.
 * Its initial purpose was to serve as a task queue, with multiple threads
 * feeding data in (from disk, network, etc) and multiple threads reading it
 * and processing it in parallel. This created the need for a fully re-entrant,
 * lightweight, accommodating data structure.
 *
 * We have two locks guarding the pipe, instead of the naive solution of having
 * one. One guards writes to the begin pointer, the other guards writes to the
 * end pointer. This is due to the realization that when pushing, you don't need
 * an up-to-date value for begin, and when popping you don't need an up-to-date
 * value for end (since either can only move forward in the buffer). As long as
 * neither moves backwards, there will be no conflicts when they move
 * independently of each other. This optimization has improved benchmarks by
 * 15-20%.
 *
 * Complexity:
 *
 * Pushing and popping must run in O(n) where n is the number of elements being
 * inserted/removed. It must also run in O(1) with respect to the number of
 * elements in the pipe.
 *
 * Efficiency:
 *
 * Asserts are used liberally, and many of them, when inlined, can be turned
 * into no-ops. Therefore, it is recommended that you compile with -O1 in
 * debug builds as the pipe can easily become a bottleneck.
 */
struct pipe_t {
    size_t elem_size,  // The size of each element. This is read-only and
                       // therefore does not need to be locked to read.
           capacity,   // The maximum number of bytes the buffer can hold
                       // before a reallocation. To modify this variable, you
                       // must lock the whole pipe.
           min_cap,    // The smallest sane capacity before the buffer refuses
                       // to shrink because it would just end up growing again.
                       // To modify this variable, you must lock the whole pipe.
           max_cap;    // The maximum capacity of the pipe before push requests
                       // are blocked. This is read-only and therefore does not
                       // need to be locked to read. To modify this variable,
                       // you must lock the whole pipe.

    char* buffer;      // The internal buffer, holding the enqueued elements. To
                       // modify this variable, you must lock the whole pipe.

    // Keep the shared variables away from the cache-aligned ones.
    PAD(64 - 4*sizeof(size_t) - sizeof(char*));

    ALIGN_TO_CACHE(char*, begin);   // Always points to the left-most element
                                    // in the pipe. To modify this variable,
                                    // you must lock begin_lock.
    ALIGN_TO_CACHE(char*, end);     // Always points past the right-most
                                    // element in the pipe. To modify this
                                    // variable, you must lock end_lock.

    ALIGN_TO_CACHE(atomic_t, producer_refcount); // The number of producers in
                                                 // circulation. Only modify
                                                 // this variable with
                                                 // atomic_[inc|dec].
    ALIGN_TO_CACHE(atomic_t, consumer_refcount); // The number of consumers in
                                                 // circulation. Only modify
                                                 // this variable with
                                                 // atomic_[inc|dec].

    // Our lovely mutexes. To lock the pipe, call lock_pipe. Depending on what
    // you modify, you may be able to get away with only locking one of them.
    ALIGN_TO_CACHE(mutex_t, begin_lock);
    ALIGN_TO_CACHE(mutex_t, end_lock);

    ALIGN_TO_CACHE(cond_t, just_pushed); // Signaled immediately after a push.
    ALIGN_TO_CACHE(cond_t, just_popped); // Signaled immediately after a pop.
};

// Converts a pointer to either a producer or consumer into a suitable pipe_t*.
#define PIPIFY(handle) ((pipe_t*)(handle))

// The initial minimum capacity of the pipe. This can be overridden dynamically
// with pipe_reserve.
#ifdef DEBUG
#define DEFAULT_MINCAP  2
#else
#define DEFAULT_MINCAP  32
#endif

// Does the buffer wrap around?
//   true  -> wrap
//   false -> nowrap
static inline bool wraps_around(const char* begin, const char* end)
{
    return begin > end;
}

// Returns the number of bytes currently in the buffer, NOT the capacity of the
// buffer. XXX: Is there a better name for this? I feel like it's misleading.
static inline size_t get_buffer_size(const pipe_t* p)
{
    size_t capacity = p->capacity;

    const char* begin = p->begin,
              * end = p->end,
              * buffer = p->buffer,
              * bufend = buffer + capacity;

    return (size_t)(wraps_around(begin, end)
    //                  v right half v   v  left half   v
                     ? ((end - buffer) + (bufend - begin))
                     : (end - begin));
}

#define WRAP_PTR_IF_NECESSARY(ptr) ((ptr) = (ptr) == bufend ? buffer : (ptr))

#define in_bounds(left, x, right) ((x) >= (left) && (x) <= (right))

static size_t CONSTEXPR next_pow2(size_t n)
{
    // I don't see why we would even try. Maybe a stacktrace will help.
    assertume(n != 0);

    // In binary, top is equal to 10000...0:  A 1 right-padded by as many zeros
    // as needed to fill up a size_t.
    size_t top = (~(size_t)0 >> 1) + 1;

    // If when we round up we will overflow our size_t, avoid rounding up and
    // exit early.
    if(unlikely(n >= top))
        return n;

    // Since we don't have to worry about overflow anymore, we can just use
    // the algorithm documented at:
    //   http://bits.stephan-brumme.com/roundUpToNextPowerOfTwo.html
    // It's my favorite due to being branch-free. The loop will be unrolled.
    n--;

    for(size_t shift = 1; shift < (sizeof n)*8; shift <<= 1)
        n |= n >> shift;

    n++;

    return n;
}

// You know all those assumptions we make about our data structure whenever we
// use it? This function checks them, and is called liberally through the
// codebase. It would be best to read this function over, as it also acts as
// documentation. Code AND documentation? What is this witchcraft?
static inline void check_invariants(const pipe_t* p)
{
    if(p == NULL) return;

    // p->buffer may be NULL. When it is, we must have no issued consumers.
    // It's just a way to save memory when we've deallocated all consumers
    // and people are still trying to push like idiots.
    if(p->buffer == NULL)
    {
        assertume(p->consumer_refcount == 0);
        return;
    }
    else
    {
        assertume(p->consumer_refcount != 0);
    }

    assertume(p->begin);
    assertume(p->end);

    assertume(p->elem_size != 0);

    assertume(get_buffer_size(p) <= p->capacity
            && "There are more elements in the buffer than its capacity.");

    assertume(in_bounds(p->buffer, p->begin, p->buffer + p->capacity));
    assertume(in_bounds(p->buffer, p->end, p->buffer + p->capacity));

    if(p->begin == p->end)
        assertume(get_buffer_size(p) == 0);

    assertume(in_bounds(DEFAULT_MINCAP*p->elem_size, p->min_cap, p->max_cap));
    assertume(in_bounds(p->min_cap, p->capacity, p->max_cap));

    assertume(p->begin != p->buffer + p->capacity
            && "The begin pointer should NEVER point to the end of the buffer."
               "If it does, it should have been automatically moved to the front.");
}

static inline void lock_pipe(pipe_t* p)
{
    // watch the locking order VERY carefully. end_lock must ALWAYS be locked
    // before begin_lock when dealing with both at once.
    mutex_lock(&p->end_lock);
    mutex_lock(&p->begin_lock);
    check_invariants(p);
}

static inline void unlock_pipe(pipe_t* p)
{
    check_invariants(p);
    mutex_unlock(&p->begin_lock);
    mutex_unlock(&p->end_lock);
}

// runs some code while automatically locking and unlocking the pipe. If `break'
// is used, the pipe will be unlocked before control returns from the macro.
#define WHILE_LOCKED(stuff) do { \
    do {                         \
        lock_pipe(p);            \
        stuff;                   \
    } while(0);                  \
    unlock_pipe(p);              \
 } while(0)

pipe_t* pipe_new(size_t elem_size, size_t limit)
{
    assertume(elem_size != 0);

    if(elem_size == 0)
        return NULL;

    pipe_t* p = malloc(sizeof *p);

    size_t cap = DEFAULT_MINCAP * elem_size;
    char* buf  = malloc(elem_size * cap);

    // Change the limit from being in "elements" to being in "bytes".
    limit *= elem_size;

    if(unlikely(p == NULL || buf == NULL))
        return NULL;

    *p = (pipe_t) {
        .elem_size  = elem_size,
        .capacity = cap,
        .min_cap = cap,
        .max_cap = limit ? next_pow2(max(limit, cap)) : ~(size_t)0,

        .buffer = buf,
        .begin  = buf,
        .end    = buf,

        // Since we're issuing a pipe_t, it counts as both a producer and a
        // consumer since it can issue new instances of both. Therefore, the
        // refcounts both start at 1; not the intuitive 0.
        .producer_refcount = 1,
        .consumer_refcount = 1,
    };

    mutex_init(&p->begin_lock);
    mutex_init(&p->end_lock);

    cond_init(&p->just_pushed);
    cond_init(&p->just_popped);

    check_invariants(p);

    return p;
}

// Instead of allocating a special handle, the pipe_*_new() functions just
// return the original pipe, cast into a user-friendly form. This saves needless
// malloc calls. Also, since we have to refcount anyways, it's free.
pipe_producer_t* pipe_producer_new(pipe_t* p)
{
    atomic_inc(&p->producer_refcount);
    return (pipe_producer_t*)p;
}

pipe_consumer_t* pipe_consumer_new(pipe_t* p)
{
    atomic_inc(&p->consumer_refcount);
    return (pipe_consumer_t*)p;
}

static inline void deallocate(pipe_t* p)
{
    assertume(p->producer_refcount == 0);
    assertume(p->consumer_refcount == 0);

    mutex_destroy(&p->begin_lock);
    mutex_destroy(&p->end_lock);

    cond_destroy(&p->just_pushed);
    cond_destroy(&p->just_popped);

    free(p->buffer);
    free(p);
}

#define x_free(p) (free(p), NULL)

void pipe_free(pipe_t* p)
{
    assertume(p->producer_refcount > 0);
    assertume(p->consumer_refcount > 0);

    atomic_t new_producer_refcount = atomic_dec(&p->producer_refcount),
             new_consumer_refcount = atomic_dec(&p->consumer_refcount);

    if(unlikely(new_consumer_refcount == 0))
    {
        p->buffer = x_free(p->buffer);

        if(unlikely(new_producer_refcount == 0))
        {
            deallocate(p);
            return;
        }
    }

    // If this was the last "producer" handle issued, we need to wake up the
    // consumers so they don't spend forever waiting for elements that can never
    // come.
    if(unlikely(new_producer_refcount == 0))
        cond_broadcast(&p->just_pushed);

    // Same issue for the producers waiting on consumers that don't exist
    // anymore.
    if(unlikely(new_consumer_refcount == 0))
        cond_broadcast(&p->just_popped);
}

void pipe_producer_free(pipe_producer_t* handle)
{
    pipe_t* p = PIPIFY(handle);

    assertume(p->producer_refcount > 0);

    size_t new_producer_refcount = atomic_dec(&p->producer_refcount);

    if(unlikely(new_producer_refcount == 0))
    {
        // If there are still consumers, wake them up if they're waiting on
        // input from a producer. Otherwise, since we're the last handle
        // altogether, we can free the pipe.
        if(likely(p->consumer_refcount > 0))
            cond_broadcast(&p->just_pushed);
        else
            deallocate(p);
    }
}

void pipe_consumer_free(pipe_consumer_t* handle)
{
    pipe_t* p = PIPIFY(handle);

    assertume(p->consumer_refcount > 0);

    size_t new_consumer_refcount = atomic_dec(&p->consumer_refcount);

    if(unlikely(new_consumer_refcount == 0))
    {
        p->buffer = x_free(p->buffer);

        // If there are still producers, wake them up if they're waiting on
        // room to free up from a consumer. Otherwise, since we're the last
        // handle altogether, we can free the pipe.
        if(likely(p->producer_refcount > 0))
            cond_broadcast(&p->just_popped);
        else
            deallocate(p);
    }
}

// Returns the end of the buffer (buf + number_of_bytes_copied).
static inline char* copy_pipe_into_new_buf(const pipe_t* p,
                                           char* restrict buf)
{
    check_invariants(p);

    const char* const begin  = p->begin,
              * const end    = p->end,
              * const buffer = p->buffer,
              * const bufend = buffer + p->capacity;

    if(wraps_around(begin, end))
    {
        buf = offset_memcpy(buf, begin, bufend - begin);
        buf = offset_memcpy(buf, buffer, end - buffer);
    }
    else
    {
        buf = offset_memcpy(buf, begin, end - begin);
    }

    return buf;
}

static void resize_buffer(pipe_t* p, size_t new_size)
{
    check_invariants(p);

    const size_t max_cap    = p->max_cap,
                 min_cap    = p->min_cap;

    assertume(new_size >= get_buffer_size(p));

    if(unlikely(new_size >= max_cap))
        new_size = max_cap;

    if(new_size < min_cap)
        return;

    char* new_buf = malloc(new_size);
    p->end = copy_pipe_into_new_buf(p, new_buf);

    p->begin  =
    p->buffer = (free(p->buffer), new_buf);

    p->capacity = new_size;

    check_invariants(p);
}

// Ensures the buffer has enough room for `count' more bytes. This function
// assumes p->end_lock is locked.
static inline void validate_size(pipe_t* p, size_t count)
{
    size_t elem_size = p->elem_size;

    // We add 1 to ensure p->begin != p->end unless p->elem_count == 0,
    // maintaining one of our invariants.
    if(unlikely(get_buffer_size(p) + count + elem_size > p->capacity))
    {
        // upgrade our lock, then re-check. By taking both locks (end and begin)
        // in order, we have an equivalent operation to lock_pipe().
        mutex_lock(&p->begin_lock);

        if(likely(get_buffer_size(p) + count + elem_size > p->capacity))
            resize_buffer(p, next_pow2(get_buffer_size(p) + count + elem_size));

        mutex_unlock(&p->begin_lock);
    }
}

static inline void process_push(pipe_t* p,
                                const void* restrict elems,
                                size_t bytes_to_copy)
{
    assertume(bytes_to_copy != 0);

    const size_t capacity  = p->capacity;

    char* const buffer = p->buffer,
        * const begin  = p->begin,
        *       end    = p->end,
        * const bufend = buffer + capacity;

    WRAP_PTR_IF_NECESSARY(end);

    // If we currently have a nowrap buffer, we may have to wrap the new
    // elements. Copy as many as we can at the end, then start copying into the
    // beginning. This basically reduces the problem to only deal with wrapped
    // buffers, which can be dealt with using a single offset_memcpy.
    if(!wraps_around(begin, end))
    {
        assertume(bufend >= end);
        size_t at_end = min(bytes_to_copy, (size_t)(bufend - end));

        end = offset_memcpy(end, elems, at_end);

        elems = (const char*)elems + at_end;
        bytes_to_copy -= at_end;
    }

    // Now copy any remaining data...
    if(bytes_to_copy)
    {
        WRAP_PTR_IF_NECESSARY(end);
        end = offset_memcpy(end, elems, bytes_to_copy);
    }

    WRAP_PTR_IF_NECESSARY(end);

    // ...and update the end pointer!
    p->end = end;
}

// Will spin until there is enough room in the buffer to push any elements.
// Returns the number of elements currently in the buffer. `end_lock` should be
// locked on entrance to this function. This function returns ~(size_t)0 if all
// consumers were freed, and we must stop pushing.
static inline size_t wait_for_room(pipe_t* p, size_t* max_cap)
{
    size_t buffer_size       = get_buffer_size(p),
           consumer_refcount = p->consumer_refcount;

    *max_cap = p->max_cap;

    for(; unlikely(buffer_size == *max_cap) && likely(consumer_refcount > 0);
          buffer_size = get_buffer_size(p),
          consumer_refcount = p->consumer_refcount,
          *max_cap = p->max_cap)
        cond_wait(&p->just_popped, &p->end_lock);

    if(unlikely(consumer_refcount == 0))
        return ~(size_t)0;

    return buffer_size;
}

// Peforms the actual pipe_push, but `count' is in "bytes" as opposed to
// "elements" to simplify processing.
static inline void pipe_push_bytes(pipe_t* p,
                                   const void* restrict elems,
                                   size_t count)
{
    if(unlikely(count == 0))
        return;

    size_t pushed  = 0;

    { mutex_lock(&p->end_lock);
        size_t max_cap = 0;
        size_t buffer_size = wait_for_room(p, &max_cap);

        assertume(max_cap != 0);

        // if no more consumers...
        if(unlikely(buffer_size == ~(size_t)0))
        {
            mutex_unlock(&p->end_lock);
            return;
        }

        validate_size(p, count);

        // Finally, we can now begin with pushing as many elements into the
        // queue as possible.
        process_push(p, elems,
            pushed = min(count, max_cap - buffer_size)
        );
    } mutex_unlock(&p->end_lock);

    assertume(pushed > 0);

    (pushed == 1 ? cond_signal : cond_broadcast)(&p->just_pushed);

    // We might not be done pushing. If the max_cap was reached, we'll need to
    // recurse.
    size_t bytes_remaining = count - pushed;

    if(unlikely(bytes_remaining))
    {
        elems = (const char*)elems + pushed;
        pipe_push_bytes(p, elems, bytes_remaining);
    }
}

void pipe_push(pipe_producer_t* p, const void* restrict elems, size_t count)
{
    pipe_push_bytes(PIPIFY(p), elems, count*PIPIFY(p)->elem_size);
}

// Waits for at least one element to be in the pipe. p->begin_lock must be
// locked when entering this function, and the current buffer size is returned.
static inline size_t wait_for_elements(pipe_t* p)
{
    size_t buffer_size = get_buffer_size(p);

    for(; buffer_size == 0 && likely(p->producer_refcount > 0);
          buffer_size = get_buffer_size(p))
        cond_wait(&p->just_pushed, &p->begin_lock);

    return buffer_size;
}

// wow, I didn't even intend for the name to work like that...
static inline void pop_without_locking(pipe_t* p,
                                       void* restrict target,
                                       size_t bytes_to_copy)
{
    const size_t capacity = p->capacity;

    char* const buffer = p->buffer,
        *       begin  = p->begin,
        * const bufend = buffer + capacity;

    WRAP_PTR_IF_NECESSARY(begin);

    // Copy either as many bytes as requested, or the available bytes in the RHS
    // of a wrapped buffer - whichever is smaller.
    {
        size_t first_bytes_to_copy = min(bytes_to_copy, (size_t)(bufend - begin));

        target = offset_memcpy(target, begin, first_bytes_to_copy);

        bytes_to_copy -= first_bytes_to_copy;
        begin         += first_bytes_to_copy;

        WRAP_PTR_IF_NECESSARY(begin);
    }

    if(bytes_to_copy > 0)
    {
        memcpy(target, buffer, bytes_to_copy);
        begin += bytes_to_copy;
    }

    // Since we cached begin on the stack, we need to reflect our changes back
    // on the pipe.
    p->begin = begin;
}

// If the buffer is shrunk to something a lot smaller than our current
// capacity, resize it to something sane.
static inline void trim_buffer(pipe_t* p)
{
    // We have a sane size. We're done here.
    if(likely(get_buffer_size(p) > p->capacity / 4))
        return;

    // Okay, we need to resize now. Upgrade our lock so we can check again. The
    // weird lock/unlock order is to make sure we always acquire the end_lock
    // before begin_lock. Deadlock can arise otherwise.
    mutex_unlock(&p->begin_lock);
    mutex_lock(&p->end_lock);
    mutex_lock(&p->begin_lock);

    // To conserve space like the good computizens we are, we'll shrink
    // our buffer if our memory usage efficiency drops below 25%. However,
    // since shrinking/growing the buffer is the most expensive part of a push
    // or pop, we only shrink it to bring us up to a 50% efficiency. A common
    // pipe usage pattern is sudden bursts of pushes and pops. This ensures it
    // doesn't get too time-inefficient.
    if(likely(get_buffer_size(p) <= p->capacity / 4))
        resize_buffer(p, p->capacity / 2);

    // All done. Downgrade our lock so we leave with the same lock-level we came
    // in with. We only need to unlock the extra lock we grabbed. We can keep
    // the begin_lock locked instead of unlocking then relocking it.
    mutex_unlock(&p->end_lock);
}

static inline size_t pipe_pop_bytes(pipe_t* p,
                                    void* restrict target,
                                    size_t requested)
{
    if(unlikely(requested == 0))
        return 0;

    size_t popped = 0;

    { mutex_lock(&p->begin_lock);
        size_t buffer_size = wait_for_elements(p);

        if(unlikely(buffer_size == 0))
        {
            mutex_unlock(&p->begin_lock);
            return 0;
        }

        pop_without_locking(p, target,
                            popped = min(requested, buffer_size)
        );

        trim_buffer(p);
    } mutex_unlock(&p->begin_lock);

    assertume(popped);

    (popped == p->elem_size ? cond_signal : cond_broadcast)(&p->just_popped);

    return popped +
        pipe_pop_bytes(p, (char*)target + popped, requested - popped);
}

size_t pipe_pop(pipe_consumer_t* p, void* restrict target, size_t count)
{
    size_t elem_size = PIPIFY(p)->elem_size;
    return pipe_pop_bytes(PIPIFY(p), target, count*elem_size) / elem_size;
}

size_t pipe_elem_size(pipe_generic_t* p)
{
    return PIPIFY(p)->elem_size;
}

void pipe_reserve(pipe_generic_t* gen, size_t count)
{
    pipe_t* p = PIPIFY(gen);

    count *= p->elem_size; // now `count' is in "bytes" instead of "elements".

    if(count == 0)
        count = DEFAULT_MINCAP;

    size_t max_cap = p->max_cap;

    WHILE_LOCKED(
        if(unlikely(count <= get_buffer_size(p)))
            break;

        p->min_cap = min(count, max_cap);
        resize_buffer(p, count);
    );
}

/* vim: set et ts=4 sw=4 softtabstop=4 textwidth=80: */
