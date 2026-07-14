///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "AdlsReadDirectoryApi.h"
#include "AdlsHttpClientException.h"
#include "AdlsClientException.h"
#include "AdlsHostContext.h"
#include <iostream>
//---------------------------------------------------------------------------------------------------------------------
AdlsReadDirectoryApi::AdlsReadDirectoryApi(
  const AdlsHostContext& hostContext,
  const std::string& remotePath,
  const std::string& continuationToken,
  std::vector<DirectoryEntry>& dirEntries)
  : AdlsApiBase(
      hostContext,
      "AdlsReadDirectoryApi")
  , m_strRemotePath(remotePath)
  , m_vecDirEntries(dirEntries)
  , m_strContinuationToken(continuationToken)
  , m_strResultContinuationToken("")
{
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsReadDirectoryApi::PerformAdlsOperation()
{
  try
  {
    m_strResultContinuationToken = 
      m_spConnection->ListDirectory(
        GetAuthToken(),
        m_strRemotePath,
        m_strContinuationToken,
        m_vecDirEntries);
  }
  catch(const AdlsHttpClientException& e)
  {
    LOG_ERROR("Unable to list directory for the given path: {}", m_strRemotePath);
    throw AdlsClientException(e);
  }
}
//---------------------------------------------------------------------------------------------------------------------
std::string AdlsReadDirectoryApi::GetResult() const
{
  return m_strResultContinuationToken;
}
//---------------------------------------------------------------------------------------------------------------------