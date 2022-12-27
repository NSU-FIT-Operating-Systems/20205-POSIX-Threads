#!/usr/bin/env bash
{
  gcc fast.c -pthread -o fast.out
  gcc slow.c -pthread -o slow.out
} &> /dev/null
TIMEFORMAT=%R
for i in {1..3}; do
    time ./fast.out > /dev/null
done
echo "-----------------"
for i in {1..3}; do
    time ./slow.out > /dev/null
done