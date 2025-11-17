///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// Checksum factory for creating and managing checksum strategies
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IChecksumStrategy.h"
#include <string>
//---------------------------------------------------------------------------------------------------------------------
// Checksum algorithm enumeration
enum class ChecksumAlgorithm : uint8_t
{
    XXHASH32 = 0,
    CRC32 = 1,
    INVALID = 255
};
// Factory class for creating checksum strategies
class ChecksumFactory
{
public:
    // Create or get strategy for given algorithm enum
    static IChecksumStrategy* GetChecksumStrategyInstance(ChecksumAlgorithm algorithm);
    
    // Parse algorithm string to enum
    static ChecksumAlgorithm ParseAlgorithm(const std::string& algorithmStr);
    
    // Get algorithm name string from enum
    static std::string GetAlgorithmName(ChecksumAlgorithm algorithm);
};

//---------------------------------------------------------------------------------------------------------------------