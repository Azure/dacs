///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// XXHash32 checksum strategy implementation
//---------------------------------------------------------------------------------------------------------------------
#include "XXHash32Strategy.h"
#include "xxhash.h"
//---------------------------------------------------------------------------------------------------------------------

XXHash32Strategy& XXHash32Strategy::GetInstance()
{
    static XXHash32Strategy s_instance;
    return s_instance;
}

uint32_t XXHash32Strategy::CalculateChecksum(const void* pBuffer, size_t nBufferLength)
{
    return XXH32(pBuffer, nBufferLength, 0);
}

//---------------------------------------------------------------------------------------------------------------------