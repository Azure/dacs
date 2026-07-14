///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
// CRC32 checksum strategy implementation
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IChecksumStrategy.h"
//---------------------------------------------------------------------------------------------------------------------

// CRC32 Strategy Singleton Implementation
class CRC32Strategy : public IChecksumStrategy
{
public:
    static CRC32Strategy& GetInstance();
    
    uint32_t CalculateChecksum(const void* pBuffer, size_t nBufferLength) override;

private:
    CRC32Strategy() = default;
};

//---------------------------------------------------------------------------------------------------------------------