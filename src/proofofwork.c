#include "proofofwork.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "log.h"

#include "BLAKE2/sse/blake2.h"

void juggler_create_puzzle(puzzle_t *puzzle)
{
    FILE *fh = fopen("/dev/urandom", "r");
    if (fh == NULL) {
        log_fatal("Error opening /dev/urandom.");
    }
    if (J_PUZZLE_SIZE != fread(puzzle->puzzle, 1, J_PUZZLE_SIZE, fh)) {
        log_fatal("Error reading from /dev/urandom.");
    }
    if (fclose(fh) != 0) {
        log_fatal("Error closing /dev/urandom.");
    }
}

int juggler_check_solution(const puzzle_t *puzzle, const solution_t *solution)
{
    log_debug("Checking solution...");
    /* It must be a solution to the right puzzle! */
    if (0 != memcmp(puzzle->puzzle, solution->puzzle, J_PUZZLE_SIZE)) {
        log_debug("    It's a solution to the wrong puzzle.");
        return 0;
    }

    /* The proof-of-work input selector must be within range. */
    if (solution->selector >= ((juint_t)1 << (J_DIFFICULTY_BITS + 2))) {
        log_debug("    The outer PoW input selector is too big.");
        return 0;
    }

    /* Compute the full nonce. */
    uint8_t full_nonce[J_PUZZLE_SIZE + J_EXTRA_NONCE_SIZE];
    memcpy(full_nonce, puzzle->puzzle, J_PUZZLE_SIZE);
    memcpy(full_nonce + J_PUZZLE_SIZE, (uint8_t *)&solution->extra_nonce, J_EXTRA_NONCE_SIZE);

    /* The given buckets must have been selected by the input selector. */
    juint_t prefixes[J_INPUT_BUCKETS];
    juggler_select_buckets(full_nonce, solution->selector, prefixes);

    for (int i = 0; i < J_INPUT_BUCKETS; i++) {
        if (solution->buckets[i].prefix != prefixes[i]) {
            log_debug("    The buckets given aren't the ones selected by the selector.");
            return 0;
        }
    }

    /* Check that each bucket is valid by recomputing it. */
    bucket_t check_buckets[J_INPUT_BUCKETS];

    /* Initialize the check buckets. */
    for (int i = 0; i < J_INPUT_BUCKETS; i++) {
        bucket_init(&check_buckets[i]);
    }

    /* Re-compute the check buckets. */
    juint_t max_preimage = ((juint_t)1 << (J_MEMORY_BITS));
    juint_t prefix;
    for (juint_t preimage = 0; preimage < max_preimage; preimage++) {
        prefix = juggler_hash_prefix(full_nonce, preimage);
        for (int i = 0; i < J_INPUT_BUCKETS; i++) {
            if (prefix == solution->buckets[i].prefix) {
                bucket_update(&check_buckets[i], preimage);
                break;
            }
        }
    }

    /* Finalize the check buckets. */
    for (int i = 0; i < J_INPUT_BUCKETS; i++) {
        bucket_final(&check_buckets[i], solution->buckets[i].prefix);
    }

    /* Make sure the result is the same. */
    if (memcmp(check_buckets, solution->buckets, sizeof(bucket_t) * J_INPUT_BUCKETS) != 0) {
        log_fatal("Re-computed buckets don't match!");
    }

    /* Check that the buckets are a solution to the proof-of-work. */
    juint_t pow;
    blake2b_state S[1];
    blake2b_init(S, sizeof(juint_t));
    blake2b_update(S, full_nonce, J_PUZZLE_SIZE + J_EXTRA_NONCE_SIZE);
    blake2b_update(S, (uint8_t *)PURPOSE_PROOFWORK, strlen(PURPOSE_PROOFWORK));
    blake2b_update(S, (uint8_t *)solution->buckets, sizeof(bucket_t) * J_INPUT_BUCKETS);
    blake2b_final(S, (uint8_t *)&pow, sizeof(juint_t));
    pow = pow & ((1 << J_DIFFICULTY_BITS) - 1);

    if (pow != 0) {
        log_debug("    Not a solution to the hashcash proof of work!");
        return 0;
    }

    return 1;
}

void juggler_find_solution(const puzzle_t *puzzle, solution_t *solution)
{
    log_debug("Finding a solution...");
    /* Tag the solution with the puzzle it's a solution to. */
    log_debug("    Tagging the solution...");
    memcpy(solution->puzzle, puzzle->puzzle, J_PUZZLE_SIZE);

    /* Start with a zero extra nonce. */
    log_debug("    Initializing the extra nonce...");
    solution->extra_nonce = 0;

    /* One bucket for every possible prefix. */
    log_debug("    Allocating bucket memory...");
    bucket_t *buckets = malloc(sizeof(bucket_t) * ((juint_t)1 << J_PREFIX_BITS));
    if (buckets == NULL) {
        log_fatal("Couldn't allocate enough bucket memory.");
    }

    /* This outer loop increments extra_nonce and tries again in case we're
     * unlucky and don't find a solution with the first value of extra_nonce. */
    while (1) {
        /* Compute the full nonce. */
        log_debug("    Computing the full nonce...");
        uint8_t full_nonce[J_PUZZLE_SIZE + J_EXTRA_NONCE_SIZE];
        memcpy(full_nonce, solution->puzzle, J_PUZZLE_SIZE);
        memcpy(full_nonce + J_PUZZLE_SIZE, (uint8_t *)&solution->extra_nonce, J_EXTRA_NONCE_SIZE);

        log_debug("    Initializing buckets...");
        for (juint_t i = 0; i < ((juint_t)1 << J_PREFIX_BITS); i++) {
            bucket_init(&buckets[i]);
        }

        /* Fill the buckets. */
        log_debug("    Filling the buckets...");
        juint_t max_preimage = ((juint_t)1 << (J_MEMORY_BITS));
        juint_t prefix;
        for (juint_t preimage = 0; preimage < max_preimage; preimage++) {
            prefix = juggler_hash_prefix(full_nonce, preimage);
            bucket_update(&buckets[prefix], preimage);
            if ((preimage & ((1 << 20) - 1)) == 0) {
                log_debug(
                    "    Added %"JUINT_T_FORMAT" of %"JUINT_T_FORMAT" preimages (%2.2f%).",
                    preimage,
                    max_preimage,
                    100 * (double)preimage / (double)max_preimage
                );
            }
        }

        log_debug("    Finalizing the buckets...");
        for (juint_t i = 0; i < (1 << J_PREFIX_BITS); i++) {
            bucket_final(&buckets[i], i);
        }

        /* Find a proof of work solution where the input is buckets. */
        log_debug("    Finding a proof-of-work solution...");
        juint_t prefixes[J_INPUT_BUCKETS];
        juint_t difficulty = (juint_t)1 << J_DIFFICULTY_BITS;
        blake2b_state S[1];
        for (solution->selector = 0; solution->selector < ((juint_t)1 << (J_DIFFICULTY_BITS + 2)); solution->selector++) {
            juggler_select_buckets(full_nonce, solution->selector, prefixes);

            juint_t pow;
            blake2b_init(S, sizeof(juint_t));
            blake2b_update(S, full_nonce, J_PUZZLE_SIZE + J_EXTRA_NONCE_SIZE);
            blake2b_update(S, (uint8_t *)PURPOSE_PROOFWORK, strlen(PURPOSE_PROOFWORK));

            for (int i = 0; i < J_INPUT_BUCKETS; i++) {
                blake2b_update(S, (uint8_t *)&buckets[prefixes[i]], sizeof(bucket_t));
            }
            blake2b_final(S, (uint8_t *)&pow, sizeof(juint_t));
            pow = pow & (difficulty - 1);

            if (pow == 0) {
                /* Save the winning buckets in the solution output. */
                for (int i = 0; i < J_INPUT_BUCKETS; i++) {
                    memcpy(&solution->buckets[i], &buckets[prefixes[i]], sizeof(bucket_t));
                }
                free(buckets);
                return;
            }

            if (solution->selector % 100000 == 0) {
                log_debug(
                    "    Tried %"JUINT_T_FORMAT" of expected %"JUINT_T_FORMAT" selectors (%2.2f%%).",
                    solution->selector,
                    difficulty,
                    100 * (double)solution->selector / (double)difficulty
                );
            }
        }

        /* Unlucky! Didn't find a solution. Try again with the next extra nonce. */
        log_debug("    Didn't find a proof-of-work solution.");
        solution->extra_nonce++;
    }
}

juint_t juggler_hash_prefix(const uint8_t *full_nonce, juint_t preimage)
{
    juint_t prefix = 0;

    blake2b_state S[1];
    blake2b_init(S, sizeof(juint_t));
    blake2b_update(S, full_nonce, J_PUZZLE_SIZE + J_EXTRA_NONCE_SIZE);
    blake2b_update(S, (uint8_t *)PURPOSE_GETPREFIX, strlen(PURPOSE_GETPREFIX));
    blake2b_update(S, (uint8_t *)&preimage, sizeof(juint_t));
    blake2b_final(S, (uint8_t *)&prefix, sizeof(juint_t));

    return prefix & ((1 << J_PREFIX_BITS) - 1);
}

void juggler_select_buckets(const uint8_t *full_nonce, juint_t selector, juint_t *prefixes)
{
    blake2b_state S[1];

    /* BLAKE2b can output at most 64 bytes. */
#if J_INPUT_BUCKETS * JUINT_T_SIZE >= 64
    #error "There isn't enough available BLAKE2 output to support J_INPUT_BUCKETS."
#endif

    blake2b_init(S, J_INPUT_BUCKETS * sizeof(juint_t));
    blake2b_update(S, full_nonce, J_PUZZLE_SIZE + J_EXTRA_NONCE_SIZE);
    blake2b_update(S, (uint8_t *)PURPOSE_SELECTION, strlen(PURPOSE_SELECTION));
    blake2b_update(S, (uint8_t *)&selector, sizeof(juint_t));
    blake2b_final(S, (uint8_t *)prefixes, J_INPUT_BUCKETS * sizeof(juint_t));

    for (juint_t i = 0; i < J_INPUT_BUCKETS; i++) {
        prefixes[i] = prefixes[i] & ((1 << J_PREFIX_BITS) - 1);
    }
}

void juggler_print_solution(solution_t *solution)
{
    for (size_t i = 0; i < sizeof(solution_t); i++) {
        printf("%02x", ((uint8_t *)solution)[i]);
    }
    printf("\n");
}

void bucket_init(bucket_t *bucket)
{
    /* Initialize prefix (which is actually the count) and all elements to 0. */
    memset(bucket, 0, sizeof(bucket_t));
}

void bucket_update(bucket_t *bucket, juint_t item)
{
#define BUCKET_INDEX_MASK (((juint_t)1 << J_BUCKET_SIZE_BITS) - 1)
    bucket->indices[bucket->prefix & BUCKET_INDEX_MASK] ^= item;
    bucket->prefix++;
}

void bucket_final(bucket_t *bucket, juint_t prefix)
{
    bucket->prefix = prefix;
}
