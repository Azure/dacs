#pragma once
#include "IHashFunction.h"

class SHA256Hash : public IHashFunction {
public:
    std::vector<uint8_t> Hash(const std::string& input) const override;
    std::string GetName() const override { return "SHA-256"; }
    std::unique_ptr<IHashFunction> Clone() const override {
        return std::make_unique<SHA256Hash>();
    }
};

class Murmur128Hash : public IHashFunction {
public:
    std::vector<uint8_t> Hash(const std::string& input) const override;
    std::string GetName() const override { return "MurmurHash3_x64_128"; }
    std::unique_ptr<IHashFunction> Clone() const override {
        return std::make_unique<Murmur128Hash>();
    }
};

class XXHash3Hash : public IHashFunction {
public:
    std::vector<uint8_t> Hash(const std::string& input) const override;
    std::string GetName() const override { return "xxHash3_64"; }
    std::unique_ptr<IHashFunction> Clone() const override {
        return std::make_unique<XXHash3Hash>();
    }
};

class FNV1aHash : public IHashFunction {
public:
    std::vector<uint8_t> Hash(const std::string& input) const override;
    std::string GetName() const override { return "FNV-1a_64"; }
    std::unique_ptr<IHashFunction> Clone() const override {
        return std::make_unique<FNV1aHash>();
    }
};

class StdHash : public IHashFunction {
public:
    std::vector<uint8_t> Hash(const std::string& input) const override;
    std::string GetName() const override { return "StdHash_64"; }
    std::unique_ptr<IHashFunction> Clone() const override {
        return std::make_unique<StdHash>();
    }
};
