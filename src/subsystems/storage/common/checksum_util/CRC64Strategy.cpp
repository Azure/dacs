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

#include "CRC64Strategy.h"
#include <azure/storage/common/crypt.hpp>

CRC64Strategy& CRC64Strategy::GetInstance()
{
    static CRC64Strategy s_instance;
    return s_instance;
}

uint64_t CRC64Strategy::CalculateChecksum(const void* pBuffer, size_t nBufferLength)
{
    Azure::Storage::Crc64Hash crc64;
    const auto checksum = crc64.Final(static_cast<const uint8_t*>(pBuffer), nBufferLength);

    uint64_t value = 0;
    for (size_t index = 0; index < checksum.size(); ++index)
    {
        value |= static_cast<uint64_t>(checksum[index]) << (index * 8);
    }
    return value;
}

//---------------------------------------------------------------------------------------------------------------------
