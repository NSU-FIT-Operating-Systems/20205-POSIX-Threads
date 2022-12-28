echo "Compile..."
gcc main.c -Werror -Wpedantic -Wall -lpthread -o main

echo "Run tests:"
for i in {1..5}
do
    echo "test $i:"
    ./main 20000
    echo ""
done
