if [[ $# -ne 2 ]]; then
    echo "Specify 2 args: threads count, iterations count"
    exit 1
fi

threads_count=$1
iterations=$2

global_cnt=0
while [[ iterations -ne 0 ]]; do
    th_cnt=0
    while [[ th_cnt -ne threads_count ]]; do
        echo "[$th_cnt] thread line: $global_cnt"
        global_cnt=$(expr $global_cnt + 1)
        th_cnt=$(expr $th_cnt + 1)
    done
    iterations=$(expr $iterations - 1)
done

