/*
 * thread_ring.c - The classic `thread ring' benchmark, using pipes as its
 *                 communication medium.
 */
#include "pipe.h"

#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>

#define THREADS 8

typedef struct {
    int threadnumber;

    pipe_consumer_t* in;
    pipe_producer_t* out;
} thread_context_t;

static thread_context_t contexts[THREADS];

static void* thread_func(void* context)
{
    thread_context_t* ctx = context;

    int n = 0;

    while(pipe_pop(ctx->in, &n, 1))
    {
        if(n == 0)
        {
            printf("%i\n", ctx->threadnumber);
            break;
        }

        --n;

        pipe_push(ctx->out, &n, 1);
    }

    pipe_producer_free(ctx->out);
    pipe_consumer_free(ctx->in);

    return NULL;
}

static pthread_t thread;

static void spawn_thread(thread_context_t* ctx)
{
    pthread_create(&thread, NULL, &thread_func, ctx);
}

int main(int argc, char** argv)
{
    if(argc != 2)
    {
        printf("Usage: %s [N]\nN = the number of times to pass around a token.\n", argv[0]);
        return 255;
    }

    int passes = atoi(argv[1]);

    pipe_t* last_pipe = pipe_new(sizeof(int), 0);

    pipe_producer_t* first = pipe_producer_new(last_pipe);

    for(int i = 0; i < THREADS - 1; ++i)
    {
        thread_context_t* ctx = contexts + i;

        ctx->threadnumber = i + 1;

        ctx->in = pipe_consumer_new(last_pipe);
        pipe_free(last_pipe);

        last_pipe = pipe_new(sizeof(int), 0);
        ctx->out = pipe_producer_new(last_pipe);

        spawn_thread(ctx);
    }

    contexts[THREADS-1] = (thread_context_t) {
        .threadnumber = THREADS,
        .in = pipe_consumer_new(last_pipe),
        .out = first
    };

    pipe_free(last_pipe);

    spawn_thread(contexts + THREADS - 1);

    pipe_push(first, &passes, 1);

    pthread_join(thread, NULL);

    return 0;
}
