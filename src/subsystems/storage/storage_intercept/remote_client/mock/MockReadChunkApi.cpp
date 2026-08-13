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

#include "StorageLibcApiWrapper.h"
#include "MockRemoteClientException.h"
#include "MockReadChunkApi.h"
#include <fcntl.h>
//---------------------------------------------------------------------------------------------------------------------
MockReadChunkApi::MockReadChunkApi(
  const std::string& filePath,
  void *buf,
  size_t length,
  off_t offset)
  : RemoteClientApiBase("MockReadChunkApi")
  , m_strFilePath(filePath)
  , m_pBuf(buf)
  , m_nReadLength(length)
  , m_nReadOffset(offset)
  , m_nBytesRead(-1)
{
}
//---------------------------------------------------------------------------------------------------------------------
void MockReadChunkApi::PerformOperation()
{
  const int fd = StorageLibcApiWrapper::open64(m_strFilePath.c_str(), O_RDONLY, 0777);
  if (fd == -1)
  {
    throw MockRemoteClientException("Failed to open file at path " + m_strFilePath);
  }

  m_nBytesRead = StorageLibcApiWrapper::pread(fd, m_pBuf, m_nReadLength, m_nReadOffset);

  if (StorageLibcApiWrapper::close(fd) == -1)
  {
    throw MockRemoteClientException("Failed to close file at path " + m_strFilePath);
  }
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t MockReadChunkApi::GetResult() const
{
  return m_nBytesRead;
}
//---------------------------------------------------------------------------------------------------------------------