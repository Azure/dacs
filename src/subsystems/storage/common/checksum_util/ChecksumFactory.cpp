///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// Checksum factory implementation
//---------------------------------------------------------------------------------------------------------------------
#include "ChecksumFactory.h"
#include "XXHash32Strategy.h"
#include "CRC32Strategy.h"
#include "CommonLogger.h"
#include <algorithm>
#include <cctype>
//---------------------------------------------------------------------------------------------------------------------

IChecksumStrategy* ChecksumFactory::GetChecksumStrategyInstance(ChecksumAlgorithm algorithm)
{
    switch(algorithm)
    {
        case ChecksumAlgorithm::XXHASH32:
        {
            LOG_DEBUG("Using XXHash32 checksum calculator");
            return &XXHash32Strategy::GetInstance();
        }
        case ChecksumAlgorithm::CRC32:
        {
            LOG_DEBUG("Using CRC32 checksum calculator");
            return &CRC32Strategy::GetInstance();
        }
        default:
        {
            LOG_DEBUG("Invalid checksum algorithm enum value: {}, defaulting to XXHash32", static_cast<int>(algorithm));
            return &XXHash32Strategy::GetInstance();
        }
    }
}

ChecksumAlgorithm ChecksumFactory::ParseAlgorithm(const std::string& algorithmStr)
{
    if (algorithmStr.empty())
    {
        LOG_WARN("Algorithm string is empty, returning INVALID");
        return ChecksumAlgorithm::INVALID;
    }

    // Convert to lowercase for case-insensitive comparison
    std::string algoStr = algorithmStr;
    std::transform(algoStr.begin(), algoStr.end(), algoStr.begin(), ::tolower);

    if (algoStr == "xxhash32")
    {
        return ChecksumAlgorithm::XXHASH32;
    }
    else if (algoStr == "crc32")
    {
        return ChecksumAlgorithm::CRC32;
    }
    else
    {
        LOG_WARN("Unknown checksum algorithm '{}', returning INVALID", algorithmStr);
        return ChecksumAlgorithm::INVALID;
    }
}

std::string ChecksumFactory::GetAlgorithmName(ChecksumAlgorithm algorithm)
{
    switch(algorithm)
    {
        case ChecksumAlgorithm::XXHASH32:
            return "xxhash32";
        case ChecksumAlgorithm::CRC32:
            return "crc32";
        case ChecksumAlgorithm::INVALID:
        default:
            return "invalid";
    }
}

//---------------------------------------------------------------------------------------------------------------------