//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <sys/types.h>
#include "RemoteClientApiBase.h"
//---------------------------------------------------------------------------------------------------------------------
class MockReadChunkApi : public RemoteClientApiBase
{
  public:
    MockReadChunkApi(
        /*[in] */ const std::string& filePath,
        /*[out]*/ void *buf,
        /*[in] */ size_t length,
        /*[in] */ off_t offset);

    void PerformOperation() override;

    ssize_t GetResult() const;

  private:
    // parameters
    const std::string                   m_strFilePath;
    void* const                         m_pBuf;
    const size_t                        m_nReadLength;
    const off_t                         m_nReadOffset;

    // API result
    ssize_t                             m_nBytesRead;
};
//---------------------------------------------------------------------------------------------------------------------