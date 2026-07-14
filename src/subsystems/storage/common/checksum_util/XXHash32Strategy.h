///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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
    
    uint32_t CalculateChecksum(const void* pBuffer, size_t nBufferLength) override;

private:
    XXHash32Strategy() = default;
};

//---------------------------------------------------------------------------------------------------------------------