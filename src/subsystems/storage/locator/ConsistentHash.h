// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef CONSISTENTHASH_H_
#define CONSISTENTHASH_H_
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <cstdint>
#include <string>
//---------------------------------------------------------------------------------------------------------------------

namespace kvclient
{

// Jump Consistent Hash - maps a key to one of nBuckets uniformly.
// Deterministic, zero-memory, O(ln(n)) time.
// Reference: Lamping & Veach, "A Fast, Minimal Memory, Consistent Hash Algorithm" (2014)
inline uint32_t JumpConsistentHash(uint64_t nKey, uint32_t nBuckets)
{
    int64_t b = -1;
    int64_t j = 0;
    while (j < static_cast<int64_t>(nBuckets))
    {
        b = j;
        nKey = nKey * 2862933555777941757ULL + 1;
        j = static_cast<int64_t>(static_cast<double>(b + 1) * (static_cast<double>(1LL << 31) / static_cast<double>((nKey >> 33) + 1)));
    }
    return static_cast<uint32_t>(b);
}

// Hash a string key to a uint64 using FNV-1a, then map to a bucket via JumpConsistentHash
inline uint32_t GetLocatorNode(const std::string& strKey, uint32_t nNumNodes)
{
    if (nNumNodes == 0) return 0;
    if (nNumNodes == 1) return 0;

    // FNV-1a 64-bit hash
    uint64_t nHash = 14695981039346656037ULL; // FNV offset basis
    for (char c : strKey)
    {
        nHash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        nHash *= 1099511628211ULL; // FNV prime
    }

    return JumpConsistentHash(nHash, nNumNodes);
}

} // namespace kvclient

//---------------------------------------------------------------------------------------------------------------------
#endif // CONSISTENTHASH_H_
