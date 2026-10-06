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

// Checksum factory for creating and managing checksum strategies
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IChecksumStrategy.h"
#include <string>
#include <string_view>
//---------------------------------------------------------------------------------------------------------------------
// Checksum algorithm enumeration
enum class ChecksumAlgorithm : uint8_t
{
    XXHASH32 = 0,
    CRC32 = 1,
    CRC64 = 2,
    INVALID = 255
};
// Factory class for creating checksum strategies
class ChecksumFactory
{
public:
    // Create or get strategy for given algorithm enum
    static IChecksumStrategy* GetChecksumStrategyInstance(ChecksumAlgorithm algorithm);
    
    // Parse algorithm string to enum
    static ChecksumAlgorithm ParseAlgorithm(std::string_view algorithmStr);
    
    // Get algorithm name string from enum
    static std::string GetAlgorithmName(ChecksumAlgorithm algorithm);
};

//---------------------------------------------------------------------------------------------------------------------