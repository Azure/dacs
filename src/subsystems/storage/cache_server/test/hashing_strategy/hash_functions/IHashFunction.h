#pragma once
#include "bits/stdc++.h"

class IHashFunction {
public:
    virtual ~IHashFunction() = default;
    virtual std::vector<uint8_t> Hash(const std::string& input) const = 0;
    virtual std::string GetName() const = 0;
    virtual std::unique_ptr<IHashFunction> Clone() const = 0;

    // Get 64-bit integer from full hash (default: first 8 bytes)
    virtual uint64_t Hash64(const std::string& input) const {
        auto fullHash = Hash(input);
        if (fullHash.size() < 8) {
            throw std::runtime_error("Hash too small to extract uint64_t");
        }

        uint64_t result;
        std::memcpy(&result, fullHash.data(), sizeof(result));
        return result;
    }
};
