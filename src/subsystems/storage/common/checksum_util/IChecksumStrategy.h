///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
// Checksum strategy interface - defines the contract for checksum calculation algorithms
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <cstdint>
#include <cstddef>
//---------------------------------------------------------------------------------------------------------------------
// Strategy interface for checksum calculation
class IChecksumStrategy
{
public:
    virtual ~IChecksumStrategy() = default;
    virtual uint32_t CalculateChecksum(const void* pBuffer, size_t nBufferLength) = 0;
};

//---------------------------------------------------------------------------------------------------------------------