#ifndef PROOFOFWORK_H
#define PROOFOFWORK_H

#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>

#define J_PREFIX_BITS 20
#define J_BUCKET_SIZE_BITS 6
#define J_MEMORY_BITS (J_PREFIX_BITS + J_BUCKET_SIZE_BITS)
// XXX: This is important for security. Prover can pre-compute the prefixes
// they'll need to search the PoW space, and only compute those. Thus this is
// really what sets the memory lower-bound. Find the optimal value!
#define J_DIFFICULTY_BITS (J_MEMORY_BITS - 2)
// XXX: Should ensure that J_INPUT_BUCKETS and J_PREFIX_BITS are enough to
// actually solve the J_DIFFICULTY_BITS PoW (larger space = less repeats)
#define J_INPUT_BUCKETS 4
#define J_PUZZLE_SIZE 32
#define J_EXTRA_NONCE_SIZE sizeof(uint32_t)

#define PURPOSE_SELECTION "juggler_selection"
#define PURPOSE_GETPREFIX "juggler_getprefix"
#define PURPOSE_PROOFWORK "juggler_proofwork"

// XXX: J_DIFFICULTY_BITS can be independent from J_MEMORY_BITS, and much
// bigger, but the code uses juint_t for that too.
#if J_MEMORY_BITS > 30
    #warning "Using an inefficient integer type."
    typedef uint64_t juint_t;
    #define JUINT_T_SIZE 8
    #define JUINT_T_FORMAT PRIu64
#else
    typedef uint32_t juint_t;
    #define JUINT_T_SIZE 4
    #define JUINT_T_FORMAT PRIu32
#endif

typedef struct Bucket {
    juint_t prefix;
    juint_t indices[1 << J_BUCKET_SIZE_BITS];
} bucket_t;

typedef struct Solution {
    uint8_t puzzle[J_PUZZLE_SIZE];
    uint32_t extra_nonce;
    juint_t selector;
    bucket_t buckets[J_INPUT_BUCKETS];
} solution_t;

typedef struct Puzzle {
    uint8_t puzzle[J_PUZZLE_SIZE];
} puzzle_t;

void juggler_create_puzzle(puzzle_t *puzzle);
int juggler_check_solution(const puzzle_t *puzzle, const solution_t *solution);
void juggler_find_solution(const puzzle_t *puzzle, solution_t *solution);
void juggler_print_solution(solution_t *solution);

juint_t juggler_hash_prefix(const uint8_t *full_nonce, juint_t preimage);
void juggler_select_buckets(const uint8_t *full_nonce, juint_t selector, juint_t *prefixes);

#endif
