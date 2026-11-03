#include <string.h>
#include <stdio.h>
#include "bip39.h"
#include "bip39_words.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"

static const char *TAG = "bip39";

bool bip39_from_entropy(const uint8_t *ent, size_t ent_len, mnemonic_t *out)
{
    if (!ent || !out || (ent_len != 16 && ent_len != 32)) return false;

    // Checksum = first (ENT/32) bits of SHA-256(entropy), appended to the entropy.
    uint8_t hash[32];
    mbedtls_sha256(ent, ent_len, hash, 0);

    const int ent_bits = (int)ent_len * 8;
    const int cs_bits  = ent_bits / 32;
    const int n_words  = (ent_bits + cs_bits) / 11;

    uint8_t buf[33];
    memcpy(buf, ent, ent_len);
    buf[ent_len] = hash[0];              // only the top 8 bits are ever needed

    memset(out, 0, sizeof(*out));
    out->n_words  = n_words;
    out->ent_bits = ent_bits;

    char *w = out->joined;
    size_t room = sizeof(out->joined);
    for (int i = 0; i < n_words; i++) {
        int bit = i * 11;
        uint32_t idx = 0;
        for (int b = 0; b < 11; b++) {
            int p = bit + b;
            idx = (idx << 1) | ((buf[p >> 3] >> (7 - (p & 7))) & 1);
        }
        out->word[i] = BIP39_WORDS[idx];
        int n = snprintf(w, room, "%s%s", i ? " " : "", BIP39_WORDS[idx]);
        if (n > 0 && (size_t)n < room) { w += n; room -= n; }
    }
    return true;
}

// ---- official BIP-39 vectors (trezor/python-mnemonic vectors.json) ----
typedef struct { const char *hex; const char *expect; } vec_t;
static const vec_t vectors[] = {
    { "00000000000000000000000000000000",
      "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about" },
    { "7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f",
      "legal winner thank year wave sausage worth useful legal winner thank yellow" },
    { "80808080808080808080808080808080",
      "letter advice cage absurd amount doctor acoustic avoid letter advice cage above" },
    { "ffffffffffffffffffffffffffffffff",
      "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo wrong" },
    { "0000000000000000000000000000000000000000000000000000000000000000",
      "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon "
      "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon art" },
    { "8080808080808080808080808080808080808080808080808080808080808080",
      "letter advice cage absurd amount doctor acoustic avoid letter advice cage absurd "
      "amount doctor acoustic avoid letter advice cage absurd amount doctor acoustic bless" },
    { "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
      "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo vote" },
};

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool bip39_selftest(void)
{
    bool all = true;
    for (size_t v = 0; v < sizeof(vectors) / sizeof(vectors[0]); v++) {
        uint8_t ent[32];
        size_t len = strlen(vectors[v].hex) / 2;
        if (len != 16 && len != 32) { all = false; continue; }
        for (size_t i = 0; i < len; i++)
            ent[i] = (uint8_t)((hexval(vectors[v].hex[i * 2]) << 4) | hexval(vectors[v].hex[i * 2 + 1]));

        mnemonic_t m;
        bool ok = bip39_from_entropy(ent, len, &m) && strcmp(m.joined, vectors[v].expect) == 0;
        if (!ok) {
            ESP_LOGE(TAG, "vector %u FAILED", (unsigned)v);
            ESP_LOGE(TAG, "  expected: %s", vectors[v].expect);
            ESP_LOGE(TAG, "  got     : %s", m.joined);
            all = false;
        }
    }
    ESP_LOGI(TAG, "BIP-39 self-test: %s (%u vectors)",
             all ? "PASS" : "FAIL", (unsigned)(sizeof(vectors) / sizeof(vectors[0])));
    return all;
}
