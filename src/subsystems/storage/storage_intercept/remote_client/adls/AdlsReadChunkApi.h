///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <sys/types.h>
#include "AdlsApiBase.h"
//---------------------------------------------------------------------------------------------------------------------
class AdlsReadChunkApi : public AdlsApiBase
{
  public:
    AdlsReadChunkApi(
      /*[in] */ const AdlsHostContext& hostContext,
      /*[in] */ const std::string& filePath,
      /*[out]*/ void *buf,
      /*[in] */ size_t length,
      /*[in] */ off_t offset);

  public:
    void PerformAdlsOperation() override;

    ssize_t GetResult() const;

  private:
    /* Api parameters */
    const std::string               m_strFilePath;
    void* const                     m_pBuf;
    const size_t                    m_nReadLength;
    const off_t                     m_nReadOffset;

    /* Api result */
    ssize_t                         m_nBytesRead;
};
//---------------------------------------------------------------------------------------------------------------------