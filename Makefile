CC=gcc

OBJS=main.c pipe.c pipe_test.c pipe_util.c
NAME=pipe

CFLAGS=-Wall -Wextra -Wpointer-arith -fstrict-aliasing -std=c99 -pthread -DFORTIFY_SOURCE=2 -pipe -pedantic #-Werror
D_CFLAGS=-DDEBUG -g -O0
R_CFLAGS=-DNDEBUG -O3 -funroll-loops #-flto

all: pipe_debug pipe_release

pipe_debug: $(OBJS)
	$(CC) $(CFLAGS)  $(D_CFLAGS) -o pipe_debug $(OBJS)

pipe_release: $(OBJS)
	$(CC) $(CFLAGS)  $(R_CFLAGS) -o pipe_release $(OBJS)

pipe.h:

main.c: pipe.h
	
pipe.c: pipe.h

pipe_test.c: pipe.h pipe_util.h

pipe_util.c: pipe.h pipe_util.h

.PHONY : clean analyze

analyze: $(OBJS) pipe_debug pipe_release
	clang --analyze $(CFLAGS) $(OBJS)
	valgrind ./pipe_debug
	valgrind ./pipe_release

clean:
	rm -f *.plist pipe_debug pipe_release
