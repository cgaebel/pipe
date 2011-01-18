#!/bin/bash

./pipe_release
mv gmon.out gmon.sum

for i in {1..100}
do
    ./pipe_release
    gprof -s pipe_release gmon.out gmon.sum
done
