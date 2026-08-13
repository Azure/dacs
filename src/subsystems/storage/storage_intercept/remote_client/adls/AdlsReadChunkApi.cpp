/// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "AdlsReadChunkApi.h"
#include "AdlsHttpClientException.h"
#include "AdlsClientException.h"
#include "AdlsHostContext.h"
#include <iostream>
#include <sstream>
//---------------------------------------------------------------------------------------------------------------------
AdlsReadChunkApi::AdlsReadChunkApi(
  const AdlsHostContext& hostContext,
  const std::string& filePath,
  void* buf,
  size_t length,
  off_t offset)
  : AdlsApiBase(
      hostContext,
      "AdlsReadChunkApi")
  , m_strFilePath(filePath)
  , m_pBuf(buf)
  , m_nReadLength(length)
  , m_nReadOffset(offset)
  , m_nBytesRead(-1)
{
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsReadChunkApi::PerformAdlsOperation()
{
  std::stringstream sStream;
  try
  {
    m_spConnection->ReadChunk(GetAuthToken(), m_strFilePath, sStream, m_nReadLength, m_nReadOffset);
  }
  catch(const AdlsHttpClientException& e)
  {
    if (strstr(e.what(), "Invalid offset value") != NULL)
    {
      LOG_WARN("Invalid offset value: {}, requested for file: {}", m_nReadOffset, m_strFilePath);
      m_nBytesRead = 0;
      return;
    }

    throw AdlsClientException(e);
  }

  sStream.seekg(0, std::ios::end);
  m_nBytesRead = sStream.tellg();
  sStream.seekg(0, std::ios::beg);
  sStream.read((char*) m_pBuf, m_nBytesRead);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t AdlsReadChunkApi::GetResult() const
{
  return m_nBytesRead;
}
//---------------------------------------------------------------------------------------------------------------------