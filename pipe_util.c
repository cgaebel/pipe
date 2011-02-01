#include "pipe_util.h"

#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>

pipeline_t pipe_trivial_pipeline(pipe_t* p)
{
    return (pipeline_t) {
        .in  = pipe_producer_new(p),
        .out = pipe_consumer_new(p)
    };
}

#define DEFAULT_BUFFER_SIZE     128

typedef struct {
    pipe_consumer_t* in;
    pipe_processor_t proc;
    void* aux;
    pipe_producer_t* out;
} connect_data_t;

static void* process_pipe(void* param)
{
    connect_data_t p = *(connect_data_t*)param;
    free(param);

    char* buf = malloc(DEFAULT_BUFFER_SIZE * pipe_elem_size(PIPE_GENERIC(p.in)));

    size_t elems_read;

    while((elems_read = pipe_pop(p.in, buf, DEFAULT_BUFFER_SIZE)))
        p.proc(buf, elems_read, p.out, p.aux);

    p.proc(NULL, 0, NULL, p.aux);

    free(buf);

    pipe_consumer_free(p.in);
    pipe_producer_free(p.out);

    return NULL;
}

void pipe_connect(pipe_consumer_t* in,
                  pipe_processor_t proc, void* aux,
                  pipe_producer_t* out)
{
    assert(in);
    assert(out);
    assert(proc);

    connect_data_t* d = malloc(sizeof(connect_data_t));

    *d = (connect_data_t) {
        .in = in,
        .proc = proc,
        .aux = aux,
        .out = out
    };

    pthread_t t;
    pthread_create(&t, NULL, &process_pipe, d);
}

pipeline_t pipe_parallel(size_t           instances,
                         size_t           in_size,
                         pipe_processor_t proc,
                         void*            aux,
                         size_t           out_size)
{
    pipe_t* in  = pipe_new(in_size,  0),
          * out = pipe_new(out_size, 0);

    while(instances--)
        pipe_connect(pipe_consumer_new(in),
                     proc, aux,
                     pipe_producer_new(out));

    pipeline_t ret = {
        .in  = pipe_producer_new(in),
        .out = pipe_consumer_new(out)
    };

    pipe_free(in);
    pipe_free(out);

    return ret;
}

static pipeline_t va_pipe_pipeline(pipeline_t result_so_far,
                                   va_list args)
{
    pipe_processor_t proc = va_arg(args, pipe_processor_t);

    if(proc == NULL)
        return result_so_far;

    void*  aux       = va_arg(args, void*);
    size_t pipe_size = va_arg(args, size_t);

    if(pipe_size == 0)
    {
        pipe_consumer_free(result_so_far.out);
        result_so_far.out = NULL;
        return result_so_far;
    }

    pipe_t* pipe = pipe_new(pipe_size, 0);

    pipe_connect(result_so_far.out , proc, aux, pipe_producer_new(pipe));
    result_so_far.out = pipe_consumer_new(pipe);

    pipe_free(pipe);

    return va_pipe_pipeline(result_so_far, args);
}

pipeline_t pipe_pipeline(size_t first_size, ...)
{
    va_list va;
    va_start(va, first_size);

    pipe_t* p = pipe_new(first_size, 0);

    pipeline_t ret = va_pipe_pipeline(pipe_trivial_pipeline(p), va);

    pipe_free(p);

    va_end(va);

    return ret;
}


/* vim: set et ts=4 sw=4 softtabstop=4 textwidth=80: */
