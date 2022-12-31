
if [[ $# -ne 2 ]]; then
    echo "Specify 2 args: threads count, launches count"
    exit 1
fi

threads_count=$1
launches=$2

echo "Compiling..."
gcc main.c -Werror -Wpedantic -Wall -lpthread -o main

launch_idx=0
succ=0
fail=0

correct_result=$(./generate_output.sh $threads_count 5)

while [[ launch_idx -ne launches ]]
do
    result=$(./main $threads_count)
    if [[ "$result" == "$correct_result" ]]
    then
        echo "TEST $launch_idx: PASSED"
        succ=$(expr $succ + 1)
    else
        echo "TEST $launch_idx: FAILED"
        fail=$(expr $fail + 1)
    fi

    launch_idx=$(expr $launch_idx + 1)
done

echo "TESTS PASSED: $succ/$launches"
echo "TESTS FAILED: $fail/$launches"



