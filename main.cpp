#include "pipe.h"

int main()
{
    Pipe<int> p;

    Pipe<int>::Producer producer = p.new_producer();
    Pipe<int>::Consumer consumer = p.new_consumer();

    return 0;
}
