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

// XXHash32 checksum strategy implementation
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IChecksumStrategy.h"
//---------------------------------------------------------------------------------------------------------------------

// XXHash32 Strategy Singleton Implementation
class XXHash32Strategy : public IChecksumStrategy
{
public:
    static XXHash32Strategy& GetInstance();
    
    uint64_t CalculateChecksum(const void* pBuffer, size_t nBufferLength) override;

private:
    XXHash32Strategy() = default;
};

//---------------------------------------------------------------------------------------------------------------------