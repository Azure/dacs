///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <sys/types.h>
#include "AdlsApiBase.h"
//---------------------------------------------------------------------------------------------------------------------
class AdlsReadDirectoryApi : public AdlsApiBase
{
  public:
    AdlsReadDirectoryApi(
      /*[in] */ const AdlsHostContext& hostContext,
      /*[in] */ const std::string& remotePath,
      /*[in] */ const std::string& continuationToken,
      /*[out]*/ std::vector<DirectoryEntry>& dirEntries);
  
  public:
    void PerformAdlsOperation() override;

    std::string GetResult() const;

  private:
    /* Api parameters */
    const std::string               m_strRemotePath;
    const std::string               m_strContinuationToken;
    std::vector<DirectoryEntry>&    m_vecDirEntries;

    /* Api result */
    std::string                     m_strResultContinuationToken;
};
//---------------------------------------------------------------------------------------------------------------------