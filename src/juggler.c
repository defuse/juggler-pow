#include <stdio.h>

#include "proofofwork.h"

int main(int argc, char **argv)
{
    puzzle_t puzzle;
    solution_t solution;

    /* We use pointer hacks to hash the buckets. If they contain padding, the
     * proof-of-work function becomes insecure, because the prover can twiddle
     * the padding values to get more proof-of-work input combinations. */
    if (sizeof(bucket_t) != sizeof(juint_t) * (1 + (1 << J_BUCKET_SIZE_BITS))) {
        printf("WARNING: bucket_t struct contains padding!");
    }

    printf("Puzzle size: %zu\n", sizeof(puzzle_t));
    printf("Solution size: %zu\n", sizeof(solution));

    juggler_create_puzzle(&puzzle);
    juggler_find_solution(&puzzle, &solution);

    if (juggler_check_solution(&puzzle, &solution)) {
        printf("Solution found.\n");
    } else {
        printf("Solution is wrong (BUG!)\n");
    }

    return 0;
}
