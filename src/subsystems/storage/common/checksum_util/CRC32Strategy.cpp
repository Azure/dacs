///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
// CRC32 checksum strategy implementation
//---------------------------------------------------------------------------------------------------------------------
#include "CRC32Strategy.h"
#include <boost/crc.hpp>
//---------------------------------------------------------------------------------------------------------------------

CRC32Strategy& CRC32Strategy::GetInstance()
{
    static CRC32Strategy s_instance;
    return s_instance;
}

uint32_t CRC32Strategy::CalculateChecksum(const void* pBuffer, size_t nBufferLength)
{
    boost::crc_32_type crc32;
    crc32.process_bytes(pBuffer, nBufferLength);
    return crc32.checksum();
}

//---------------------------------------------------------------------------------------------------------------------