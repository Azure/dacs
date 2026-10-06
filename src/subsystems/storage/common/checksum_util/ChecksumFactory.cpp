/// Copyright (c) Microsoft Corporation.
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

// Checksum factory implementation
//---------------------------------------------------------------------------------------------------------------------
#include "ChecksumFactory.h"
#include "XXHash32Strategy.h"
#include "CRC32Strategy.h"
#include "CRC64Strategy.h"
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
        case ChecksumAlgorithm::CRC64:
        {
            LOG_DEBUG("Using Azure SDK CRC64 checksum calculator");
            return &CRC64Strategy::GetInstance();
        }
        default:
        {
            LOG_ERROR("Unsupported checksum algorithm enum value: {}", static_cast<int>(algorithm));
            return nullptr;
        }
    }
}

ChecksumAlgorithm ChecksumFactory::ParseAlgorithm(std::string_view algorithmStr)
{
    if (algorithmStr.empty())
    {
        LOG_WARN("Algorithm string is empty, returning INVALID");
        return ChecksumAlgorithm::INVALID;
    }

    const auto equalsIgnoreCase = [algorithmStr](std::string_view expected) {
        return std::equal(algorithmStr.begin(), algorithmStr.end(), expected.begin(), expected.end(),
            [](unsigned char lhs, unsigned char rhs) {
                return std::tolower(lhs) == std::tolower(rhs);
            });
    };

    if (equalsIgnoreCase("xxhash32"))
    {
        return ChecksumAlgorithm::XXHASH32;
    }
    else if (equalsIgnoreCase("crc32"))
    {
        return ChecksumAlgorithm::CRC32;
    }
    else if (equalsIgnoreCase("crc64"))
    {
        return ChecksumAlgorithm::CRC64;
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
        case ChecksumAlgorithm::CRC64:
            return "crc64";
        case ChecksumAlgorithm::INVALID:
        default:
            return "invalid";
    }
}

//---------------------------------------------------------------------------------------------------------------------