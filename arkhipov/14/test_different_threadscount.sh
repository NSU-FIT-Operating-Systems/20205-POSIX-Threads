if [[ $# -ne 1 ]]; then
    echo "Specify 1 arg: max threads count"
    exit 1
fi

max_threads_count=$1
threads=1
while [[ threads -le max_threads_count ]]
do
    echo "Launch with $threads threads:"
    ./test_many_times.sh $threads 10

    threads=$(expr $threads + 1)
done
