#include "HashFunctions.h"
#include "MurmurHash3.h"
#include "xxhash.h"
#include "openssl/sha.h"
#include "bits/stdc++.h"

// SHA-256 → 32 bytes
std::vector<uint8_t> SHA256Hash::Hash(const std::string& input) const {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash.data());
    return hash;
}

// MurmurHash3_x64_128 → 16 bytes
std::vector<uint8_t> Murmur128Hash::Hash(const std::string& input) const {
    std::vector<uint8_t> hash(16);
    MurmurHash3_x64_128(input.data(), input.size(), 42, hash.data()); // hardcoded seed 42
    return hash;
}

// xxHash3_64bits → 8 bytes
std::vector<uint8_t> XXHash3Hash::Hash(const std::string& input) const {
    uint64_t raw = XXH3_64bits(input.data(), input.size());
    std::vector<uint8_t> hash(sizeof(raw));
    std::memcpy(hash.data(), &raw, sizeof(raw));
    return hash;
}

// FNV-1a 64-bit → 8 bytes
std::vector<uint8_t> FNV1aHash::Hash(const std::string& input) const {
    const uint64_t FNV_offset_basis = 14695981039346656037ULL;
    const uint64_t FNV_prime = 1099511628211ULL;

    uint64_t hash = FNV_offset_basis;
    for (char c : input) {
        hash ^= static_cast<uint8_t>(c);
        hash *= FNV_prime;
    }

    std::vector<uint8_t> result(sizeof(hash));
    std::memcpy(result.data(), &hash, sizeof(hash));
    return result;
}

// StdHash (std::hash) → size_t (8 bytes on 64-bit systems)
std::vector<uint8_t> StdHash::Hash(const std::string& input) const {
    size_t hash = std::hash<std::string>{}(input);
    std::vector<uint8_t> result(sizeof(hash));
    std::memcpy(result.data(), &hash, sizeof(hash));
    return result;
}
