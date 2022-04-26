CC=gcc

OBJS=pipe.c pipe_test.c pipe_util.c
NAME=pipe

VALGRIND_FLAGS= --leak-check=full --show-leak-kinds=all -s --track-origins=yes
CFLAGS=-Wall -Wextra -Wpointer-arith -fstrict-aliasing -std=c99 -DFORTIFY_SOURCE=2 -pipe -pedantic #-Werror
D_CFLAGS=-DDEBUG -g -O0
R_CFLAGS=-DNDEBUG -O3 -funroll-loops #-pg #-flto

target = $(shell sh -c '$(CC) -v 2>&1 | grep "Target:"')

ifeq (,$(findstring mingw,$(target)))
	CFLAGS += -pthread
endif

all: pipe_debug pipe_release thread_ring_debug thread_ring_release

pipe_debug: $(OBJS) main.c
	$(CC) $(CFLAGS)  $(D_CFLAGS) -o pipe_debug $(OBJS) main.c

pipe_release: $(OBJS) main.c
	$(CC) $(CFLAGS)  $(R_CFLAGS) -o pipe_release $(OBJS) main.c

thread_ring_debug: $(OBJS) thread_ring.c
	$(CC) $(CFLAGS)  $(D_FLAGS) -o thread_ring_debug $(OBJS) thread_ring.c

thread_ring_release: $(OBJS) thread_ring.c
	$(CC) $(CFLAGS)  $(R_FLAGS) -o thread_ring_release $(OBJS) thread_ring.c

pipe.h:

main.c: pipe.h

pipe.c: pipe.h

pipe_test.c: pipe.h pipe_util.h

pipe_util.c: pipe.h pipe_util.h

.PHONY : clean analyze

analyze: $(OBJS) pipe_debug pipe_release
	clang --analyze $(CFLAGS) $(OBJS)
	valgrind $(VALGRIND_FLAGS) ./pipe_debug
	valgrind $(VALGRIND_FLAGS) ./pipe_release
	# do not know equivilant flags for these, attempting to specify generates unknown option
	valgrind --tool=callgrind --dump-instr=yes --trace-jump=yes ./pipe_release
	valgrind --tool=cachegrind ./pipe_release
	valgrind --tool=massif ./pipe_release

clean:
	rm -f *.plist pipe_debug pipe_release thread_ring_debug thread_ring_release callgrind.out* cachegrind.out* massif.out*
