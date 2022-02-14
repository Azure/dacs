///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <sys/types.h>
#include "AdlsApiBase.h"
//---------------------------------------------------------------------------------------------------------------------
class AdlsGetPropertiesApi : public AdlsApiBase
{
  public:
    AdlsGetPropertiesApi(
      /*[in] */ const AdlsHostContext& hostContext,
      /*[in] */ const std::string& filePath,
      /*[out]*/ FileAttributes& attr);
  
  public:
    void PerformAdlsOperation() override;

    int GetResult() const;

  private:
    /* Api parameters */
    const std::string               m_strFilePath;
    FileAttributes&                  m_Attr;

    /* Api result */
    int                             m_nResult;
};
//---------------------------------------------------------------------------------------------------------------------
