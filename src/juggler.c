#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "proofofwork.h"

double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec*1e-6;
}

int main(int argc, char **argv)
{
    puzzle_t puzzle;
    solution_t solution;
    double start_time;
    int ret;

    /* We use pointer hacks to hash the buckets. If they contain padding, the
     * proof-of-work function becomes insecure, because the prover can twiddle
     * the padding values to get more proof-of-work input combinations. */
    if (sizeof(bucket_t) != sizeof(juint_t) * (1 + (1 << J_BUCKET_SIZE_BITS))) {
        printf("WARNING: bucket_t struct contains padding!");
    }

    printf("Puzzle size: %zu\n", sizeof(puzzle_t));
    printf("Solution size: %zu\n", sizeof(solution));

    start_time = get_time();
    juggler_create_puzzle(&puzzle);
    printf("Time to create a puzzle: %.5f\n", get_time() - start_time);

    start_time = get_time();
    juggler_find_solution(&puzzle, &solution);
    printf("Time to find a solution: %.5f\n", get_time() - start_time);

    start_time = get_time();
    ret = juggler_check_solution(&puzzle, &solution);
    printf("Time to check a solution: %.5f\n", get_time() - start_time);

    if (ret) {
        printf("Solution found.\n");
        juggler_print_solution(&solution);
    } else {
        printf("Solution is wrong (BUG!)\n");
    }

    return 0;
}
