///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "AdlsGetPropertiesApi.h"
#include "AdlsHttpClientException.h"
#include "AdlsClientException.h"
#include "AdlsHostContext.h"
#include <iostream>
//---------------------------------------------------------------------------------------------------------------------
AdlsGetPropertiesApi::AdlsGetPropertiesApi(
  const AdlsHostContext& hostContext,
  const std::string& filePath,
  FileAttributes& attr)
  : AdlsApiBase(
      hostContext,
      "AdlsGetPropertiesApi")
  , m_strFilePath(filePath)
  , m_Attr(attr)
  , m_nResult(-1)
{
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsGetPropertiesApi::PerformAdlsOperation()
{
  try
  {
    m_nResult = m_spConnection->GetFileOrDirectoryStatus(GetAuthToken(), m_strFilePath, m_Attr);
  }
  catch(const AdlsHttpClientException& e)
  {
    LOG_ERROR("Error getting properties for remote path: {}", m_strFilePath);
    throw AdlsClientException(e);
  }
}
//---------------------------------------------------------------------------------------------------------------------
int AdlsGetPropertiesApi::GetResult() const
{
  return m_nResult;
}
//---------------------------------------------------------------------------------------------------------------------
