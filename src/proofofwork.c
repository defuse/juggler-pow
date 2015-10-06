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
    if (solution->selector >= ((juint_t)1 << (J_DIFFICULTY_BITS + 1))) {
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

    /* Check that each bucket is valid. */
    for (int i = 0; i < J_INPUT_BUCKETS; i++) {
        for (int j = 0; j < ((juint_t)1 << J_BUCKET_SIZE_BITS); j++) {
            /* Check that the hash actually starts with this bucket's prefix. */
            juint_t prefix = juggler_hash_prefix(full_nonce, solution->buckets[i].indices[j]);

            if (prefix != solution->buckets[i].prefix) {
                log_debug("    Element in a bucket does not have its prefix.");
                return 0;
            }

            /* Make sure the indices are in strictly ascending order. Without
             * this check, the prover could simply iterate through the
             * permutations of indices inside one bucket. */
            if (j > 0) {
                /* This also makes sure the indices are unique. */
                if (solution->buckets[i].indices[j] <= solution->buckets[i].indices[j-1]) {
                    log_debug("    Bucket indices aren't in strictly ascending order.");
                    return 0;
                }
            }
        }
    }

    /* Check that the preimage indices were unambiguously chosen. */
    /* Unfortunately, this CPU-expensive operation is required to prevent an
     * attack (see one of the XXX coments in the solver code). */
    log_debug("    Looking for preimage selection trickery...");

    /* Calculate the maximum preimage, so we can stop checking ASAP. */
    /* Since we're computing the maximum from untrusted data, it's important
     * that this step come *after* the one above. Otherwise, the prover could
     * DoS this code by specifying an insanely-high maximum value. XXX: But
     * could the prover just offset all their indices by some constant value to
     * force us to do more work here? */
    juint_t max_preimage = 0;
    for (int i = 0; i < J_INPUT_BUCKETS; i++) {
        for (int j = 0; j < ((juint_t)1 << J_BUCKET_SIZE_BITS); j++) {
            if (solution->buckets[i].indices[j] > max_preimage) {
                max_preimage = solution->buckets[i].indices[j];
            }
        }
    }

    for (juint_t preimage = 0; preimage <= max_preimage; preimage++) {
        juint_t prefix = juggler_hash_prefix(full_nonce, preimage);

        // Don't bother optimizing the following loop. The hashing is the
        // bottleneck. Commenting out the code below doesn't appear to even
        // affect the performance.

        for (int i = 0; i < J_INPUT_BUCKETS; i++) {
            if (prefix == solution->buckets[i].prefix) {
                /* It must be either in the list, or greater than the last one. */
                int valid = 0;
                juint_t j = 0;
                for (; j < ((juint_t)1 << J_BUCKET_SIZE_BITS); j++) {
                    if (solution->buckets[i].indices[j] == preimage) {
                        valid = 1;
                        break;
                    }
                }
                valid |= preimage > solution->buckets[i].indices[j-1];
                if (!valid) {
                    log_debug("    Preimage selection trickery!");
                    return 0;
                }
                break;
            }
        }

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

        /* Set all of the buckets to empty.
         * NOTE: We're re-using the 'prefix' field of bucket as the current number
         * of elements in the bucket. The bucket's prefix is the same as its index
         * in the array. */
        log_debug("    Initializing bucket element counts...");
        for (juint_t i = 0; i < ((juint_t)1 << J_PREFIX_BITS); i++) {
            buckets[i].prefix = 0;
        }

        /* Fill the buckets. */
        // XXX: we should probably check index upper bounds.
        log_debug("    Filling the buckets");
        juint_t total_added = 0, prefix;
        for (
            juint_t preimage = 0;
            /* We get to this value of total_added exactly when all buckets are full. */
            total_added < ((juint_t)1 << (J_PREFIX_BITS + J_BUCKET_SIZE_BITS)) && preimage < ((juint_t)1 << (J_MEMORY_BITS + 1));
            preimage++
            ) {
            prefix = juggler_hash_prefix(full_nonce, preimage);
            if (buckets[prefix].prefix < ((juint_t)1 << J_BUCKET_SIZE_BITS)) {
                total_added += 1;
                buckets[prefix].indices[buckets[prefix].prefix] = preimage;
                buckets[prefix].prefix++;
            } else {
                /* Bucket is already full. Don't store this index anywhere. */
                // XXX WAIT... an attack:
                //      Can't the attacker just find one more than necessary for the bucket
                //      and then permute that in/out with others??
                //          Fix: We need to make a PRP output on the right number of bits so
                //          that the number in each bucket is EXACT, i.e. if there are 2^16
                //          buckets of size 2^10, then the PRP maps [0, 2^26) onto [0, 2^26)
                //          in a one-to-one, onto, and *hard-to-invert* way.
                // (An alternate fix is to make the client computationally verify
                // the exact counts, and always include all of them. But then the
                // client requires high CPU (but still low memory))
            }

            if (total_added % 100000 == 0) {
                log_debug("    Added (another) 100000 preimages.");
            }
        }

        if (total_added != ((juint_t)1 << (J_PREFIX_BITS + J_BUCKET_SIZE_BITS))) {
            log_debug("Didn't fill all of the buckets.");
            /* Unlucky! Try again with the next extra nonce. */
            solution->extra_nonce++;
            continue;
        }

        /* Set the prefix field to the right value. It is no longer the length. */
        log_debug("    Restoring the proper value of the prefix fields...");
        for (juint_t i = 0; i < (1 << J_PREFIX_BITS); i++) {
            buckets[i].prefix = i;
        }

        /* Find a proof of work solution where the input is buckets. */
        log_debug("    Finding a proof-of-work solution...");
        juint_t prefixes[J_INPUT_BUCKETS];
        blake2b_state S[1];
        for (solution->selector = 0; solution->selector < ((juint_t)1 << (J_DIFFICULTY_BITS + 1)); solution->selector++) {
            juggler_select_buckets(full_nonce, solution->selector, prefixes);

            juint_t pow;
            blake2b_init(S, sizeof(juint_t));
            blake2b_update(S, full_nonce, J_PUZZLE_SIZE + J_EXTRA_NONCE_SIZE);
            blake2b_update(S, (uint8_t *)PURPOSE_PROOFWORK, strlen(PURPOSE_PROOFWORK));

            for (int i = 0; i < J_INPUT_BUCKETS; i++) {
                blake2b_update(S, (uint8_t *)&buckets[prefixes[i]], sizeof(bucket_t));
            }
            blake2b_final(S, (uint8_t *)&pow, sizeof(juint_t));
            pow = pow & ((1 << J_DIFFICULTY_BITS) - 1);

            if (pow == 0) {
                /* Save the winning buckets in the solution output. */
                for (int i = 0; i < J_INPUT_BUCKETS; i++) {
                    memcpy(&solution->buckets[i], &buckets[prefixes[i]], sizeof(bucket_t));
                }
                free(buckets);
                return;
            }

            if (solution->selector % 100000 == 0) {
                log_debug("    Tried another 100000 selectors.");
            }
        }

        /* Unlucky! Didn't find a solution. Try again with the next extra nonce. */
        log_debug("    Didn't find a proof-of-work solution.");
        solution->extra_nonce++;
    }
}

// XXX: this is no longer the prefix, it's the suffix, but suffix is more
// efficent!
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
