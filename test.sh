#!/bin/sh

clang --analyze pipe.c
gcc -g -std=c99 -DDEBUG -pthread -O1 -pipe main.c pipe.c
valgrind ./a.out
