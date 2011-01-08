#include "pipe.h"

#include <stdio.h>

#define countof(a) (sizeof(a)/sizeof((a)[0]))

int main()
{
    pipe_t* pipe = pipe_new(sizeof(int));
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

    printf("Result -> [ ");

    for(int i = 0; i < acnt; ++i)
        printf("%i ", bufa[i]);

    printf("| ");

    for(int i = 0; i < bcnt; ++i)
        printf("%i ", bufb[i]);

    printf("]\n");

    pipe_consumer_free(c);

    return 0;
}
