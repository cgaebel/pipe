CC=gcc

OBJS=main.c pipe.c pipe_test.c
NAME=pipe

CFLAGS=-Wall -fstrict-aliasing -std=c99 -pthread
D_CFLAGS=-DDEBUG -O
R_CFLAGS=-DNDEBUG -O3

all: pipe_debug pipe_release

pipe_debug: $(OBJS)
	$(CC) $(CFLAGS)  $(D_CFLAGS) -o pipe_debug $(OBJS)
	valgrind ./pipe_debug

pipe_release: $(OBJS)
	$(CC) $(CFLAGS)  $(R_CFLAGS) -o pipe_release $(OBJS)
	valgrind ./pipe_release

pipe.h:

main.c: pipe.h
	
pipe.c: pipe.h

pipe_test.c: pipe.h

.PHONY : clean analyze

analyze: $(OBJS)
	clang --analyze $(CFLAGS) $(OBJS)

clean:
	rm -f *.plist pipe_debug pipe_release
