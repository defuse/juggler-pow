#include <stdio.h>

#include "proofofwork.h"

int main(int argc, char **argv)
{
    puzzle_t puzzle;
    solution_t solution;

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
