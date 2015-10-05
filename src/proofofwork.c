#include "proofofwork.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <openssl/sha.h>

#include "log.h"

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
    // XXX: do the correct calculation
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
            juint_t prefix = juggler_hash_prefix(
                full_nonce,
                (uint8_t *)&(solution->buckets[i].indices[j]),
                sizeof(juint_t),
                PURPOSE_GETPREFIX,
                J_PREFIX_BITS
            );

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

    // XXX: this could be optimized by saving the max preimage in the solution
    // and stopping at that value (and doing whatever else is required to make
    // it consistent)
    log_debug("    Looking for preimage selection trickery...");
    juint_t total_added = 0, prefix;
    for (
        juint_t preimage = 0;
        /* We get to this value of total_added exactly when all buckets are full. */
        preimage < ((juint_t)1 << (J_MEMORY_BITS + 1));
        preimage++
        ) {

        prefix = juggler_hash_prefix(
            full_nonce,
            (uint8_t *)&preimage,
            sizeof(juint_t),
            PURPOSE_GETPREFIX,
            J_PREFIX_BITS
        );

        for (int i = 0; i < J_INPUT_BUCKETS; i++) {
            if (prefix == solution->buckets[i].prefix) {
                /* It must be either in the list, or greater than the last one. */
                int valid = 0;
                juint_t j = 0;
                // XXX: We could do a binary search here, but is it worth it?
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
            }
        }

    }

    /* Check that the buckets are a solution to the proof-of-work. */
    juint_t pow = juggler_hash_prefix(
        full_nonce,
        (uint8_t *)solution->buckets,
        sizeof(bucket_t) * J_INPUT_BUCKETS,
        PURPOSE_PROOFWORK,
        J_DIFFICULTY_BITS
    );

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
        // XXX: find the optimal calculation here
        // XXX: we should probably check index upper bounds.
        log_debug("    Filling the buckets");
        juint_t total_added = 0, prefix;
        for (
            juint_t preimage = 0;
            /* We get to this value of total_added exactly when all buckets are full. */
            total_added < ((juint_t)1 << (J_PREFIX_BITS + J_BUCKET_SIZE_BITS)) && preimage < ((juint_t)1 << (J_MEMORY_BITS + 1));
            preimage++
            ) {
            prefix = juggler_hash_prefix(
                full_nonce,
                (uint8_t *)&preimage,
                sizeof(juint_t),
                PURPOSE_GETPREFIX,
                J_PREFIX_BITS
            );
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

        /* Find a proof of work solution where the input is buckets. */
        log_debug("    Finding a proof-of-work solution...");
        juint_t prefixes[J_INPUT_BUCKETS];
        for (solution->selector = 0; solution->selector < ((juint_t)1 << (J_DIFFICULTY_BITS)); solution->selector++) {
            juggler_select_buckets(full_nonce, solution->selector, prefixes);

            /* Create the potential proof-of-work solution (the hash input) */
            for (int i = 0; i < J_INPUT_BUCKETS; i++) {
                memcpy(&solution->buckets[i], &buckets[prefixes[i]], sizeof(bucket_t));
                /* Set the prefix to the correct value. Before, we were using it to
                * store the count. In the hash it should be the prefix. */
                solution->buckets[i].prefix = prefixes[i];
            }

            /* Check if we found a solution to the proof-of-work. */
            juint_t pow = juggler_hash_prefix(
                full_nonce,
                (uint8_t *)solution->buckets,
                sizeof(bucket_t) * J_INPUT_BUCKETS,
                PURPOSE_PROOFWORK,
                J_DIFFICULTY_BITS
            );

            if (pow == 0) {
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

juint_t juggler_hash_prefix(const uint8_t *full_nonce, const uint8_t *msg, size_t len, const uint8_t *purpose, size_t bits)
{
    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, full_nonce, J_PUZZLE_SIZE + J_EXTRA_NONCE_SIZE);
    SHA256_Update(&sha256, purpose, strlen(purpose));
    SHA256_Update(&sha256, msg, len);
    SHA256_Final(hash, &sha256);

    assert(bits <= 64);

    juint_t prefix = 0;

    /* Take just as many bytes as we need. */
    for (int i = 0; i < (bits + 8 - 1) / 8; i++) {
        prefix <<= 8;
        prefix ^= hash[i];
    }

    /* Shift out the few extra bits in the last byte. */
    if (bits % 8 != 0) {
        prefix = prefix >> (8 - (bits % 8));
    }

    return prefix;
}

void juggler_select_buckets(const uint8_t *full_nonce, juint_t selector, juint_t *prefixes)
{
    // XXX: This can be made more efficient. Instead of calling the hash
    // function for each one, we can use up all the bits of the hash function to
    // select multiple prefixes. If we switch to blake2, then we can set the
    // output length to be just right.
    uint8_t msg[sizeof(juint_t) + sizeof(juint_t)];
    memcpy(msg, (uint8_t *)&selector, sizeof(juint_t));

    for (juint_t i = 0; i < J_INPUT_BUCKETS; i++) {
        memcpy(msg + sizeof(juint_t), (uint8_t *)&i, sizeof(juint_t));
        prefixes[i] = juggler_hash_prefix(
            full_nonce,
            msg,
            sizeof(juint_t) + sizeof(juint_t),
            PURPOSE_SELECTION,
            J_PREFIX_BITS
        );
    }
}
