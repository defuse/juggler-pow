#ifndef PROOFOFWORK_H
#define PROOFOFWORK_H

#include <stdint.h>
#include <stddef.h>

#define J_PREFIX_BITS 18
#define J_BUCKET_SIZE_BITS 8
#define J_MEMORY_BITS (J_PREFIX_BITS + J_BUCKET_SIZE_BITS)
// TODO: What exactly is the signifcance of this parameter?
#define J_DIFFICULTY_BITS 16
#define J_INPUT_BUCKETS 4
#define J_PUZZLE_SIZE 32
#define J_EXTRA_NONCE_SIZE 32

#define PURPOSE_SELECTION "juggler_selection"
#define PURPOSE_GETPREFIX "juggler_getprefix"
#define PURPOSE_PROOFWORK "juggler_proofwork"

// XXX: need to ensure this struct is packed correctly (because pointer hax)
typedef struct Bucket {
    uint64_t prefix;
    uint64_t indices[1 << J_BUCKET_SIZE_BITS];
} bucket_t;

typedef struct Solution {
    uint8_t puzzle[J_PUZZLE_SIZE];
    uint8_t extra_nonce[J_EXTRA_NONCE_SIZE];
    uint64_t selector;
    bucket_t buckets[J_INPUT_BUCKETS];
} solution_t;

typedef struct Puzzle {
    uint8_t puzzle[J_PUZZLE_SIZE];
} puzzle_t;

void juggler_create_puzzle(puzzle_t *puzzle);
int juggler_check_solution(const puzzle_t *puzzle, const solution_t *solution);
void juggler_find_solution(const puzzle_t *puzzle, solution_t *solution);

uint64_t juggler_hash_prefix(const uint8_t *full_nonce, const uint8_t *msg, size_t len, const uint8_t *purpose, size_t bits);
void juggler_select_buckets(const uint8_t *full_nonce, uint64_t selector, uint64_t *prefixes);

#endif
