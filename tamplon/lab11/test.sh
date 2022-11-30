#!/usr/bin/bash
if test "$#" -ne 3; then
    echo "usage: <program> <input> <expected result> <number of launches>"
fi
program=$1
input=$2
expected_result=$3
number_of_launches=$4
sed -i 's/\r$//' expected_result
gcc -o program.out -pthread "$program"
for i in $(seq 1 "$number_of_launches"); do
    ./program.out < "$input" > result
    if diff result "$expected_result"; then
        echo "Launch $i: correct result"
    else
        echo "Launch $i: failed"
        exit 1
    fi
done
