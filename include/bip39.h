#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BIP39_MAX_WORDS 24

typedef struct {
    int  n_words;                      // 12 or 24
    int  ent_bits;                     // 128 or 256
    const char *word[BIP39_MAX_WORDS]; // points into the embedded wordlist
    char joined[256];                  // space-separated phrase
} mnemonic_t;

// entropy_len must be 16 (-> 12 words) or 32 (-> 24 words).
bool bip39_from_entropy(const uint8_t *entropy, size_t entropy_len, mnemonic_t *out);

// Runs the official BIP-39 test vectors. Returns true if every one matches.
bool bip39_selftest(void);
