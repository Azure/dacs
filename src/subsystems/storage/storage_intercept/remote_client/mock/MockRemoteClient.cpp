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

#include <fcntl.h>
#include <sys/xattr.h>
#include "StorageInterceptConfig.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptUtils.h"
#include "StorageLibcApiWrapper.h"
#include "MockRemoteClient.h"
#include "MockReadChunkApi.h"
#include "MockReadDirectoryApi.h"
#include "MockGetPropertiesApi.h"
//---------------------------------------------------------------------------------------------------------------------
MockRemoteClient::MockRemoteClient(const std::string& mountName)
    : m_strFsRoot(StorageInterceptConfig::GetInstance().GetMockClientRoot(mountName))
{
    LOG_INFO("Initialized mock remote of client name={} mockFsRoot={}", mountName, m_strFsRoot);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t MockRemoteClient::ReadChunkSync(
    const std::string& remotePath,
    void *buf,
    size_t length,
    off_t offset)
{
    MockReadChunkApi api(
        StorageInterceptUtils::CombinePath(m_strFsRoot, remotePath),
        buf,
        length,
        offset);
    api.Execute();
    return api.GetResult();
}
//---------------------------------------------------------------------------------------------------------------------
std::string MockRemoteClient::ReadDirectory(
    const std::string& remotePath,
    const std::string& continuationToken,
    std::vector<DirectoryEntry>& dirEntries)
{
    MockReadDirectoryApi api(
        StorageInterceptUtils::CombinePath(m_strFsRoot, remotePath),
        continuationToken,
        dirEntries);
    api.Execute();
    return api.GetResult();
}
//---------------------------------------------------------------------------------------------------------------------
int MockRemoteClient::GetProperties(
    const std::string& remotePath,
    FileAttributes& attributes)
{
    MockGetPropertiesApi api(
        StorageInterceptUtils::CombinePath(m_strFsRoot, remotePath),
        attributes);
    api.Execute();
    return api.GetResult();
}
//---------------------------------------------------------------------------------------------------------------------
void MockRemoteClient::PrepareBlocksForUpload(
    const std::string&,
    size_t)
{
    // No-op
    return;
}
//---------------------------------------------------------------------------------------------------------------------
int MockRemoteClient::StageBlocksForUpload(
    /*[in]*/ const std::string& remotePath,
    /*[in]*/ const void* buf,
    /*[in]*/ size_t length,
    /*[in]*/ off64_t blockId)
{
    // Implement by storing the chunk in a temp file
    const std::string tempBlockPath =
        StorageInterceptUtils::CombinePath(m_strFsRoot, remotePath)
        + "_upload_temp_"
        + std::to_string(blockId);
    LOG_INFO("Staging blockId={} of size={} to temp file={}", blockId, length, tempBlockPath);

    const int fd = StorageLibcApiWrapper::open64(
        tempBlockPath.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1)
    {
        LOG_ERROR("Failed to open temp block file for writing at path {}", tempBlockPath);
        return -1;
    }

    ssize_t nBytesWritten = StorageLibcApiWrapper::pwrite(
        fd,
        buf,
        length,
        0);
    if (nBytesWritten != static_cast<ssize_t>(length))
    {
        LOG_ERROR("Failed to write all bytes to temp block file at path {}. Expected={}, Written={}",
            tempBlockPath,
            length,
            nBytesWritten);
    }

    StorageLibcApiWrapper::close(fd);
    LOG_INFO("Staged blockId={} of size={} to temp file={}", blockId, nBytesWritten, tempBlockPath);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int MockRemoteClient::CommitBlocksForUpload(
    /*[in]*/ const std::string& remotePath,
    /*[in]*/ const std::string& version)
{
    // Implement by reading the blocks, combining them into the final file and deleting the temp files.
    const std::string baseFilePath =
        StorageInterceptUtils::CombinePath(m_strFsRoot, remotePath);
    const std::string tempBlockPathPrefix = baseFilePath + "_upload_temp_";
    int blockId = 0;;
    const int fdFinal = StorageLibcApiWrapper::open64(
        baseFilePath.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fdFinal == -1)
    {
        LOG_ERROR("Failed to open final file for writing at path {}", baseFilePath);
        return -1;
    }

    uint32_t nChunks = 0;
    off64_t writeOffset = 0;
    while (true)
    {
        const std::string tempBlockPath =
            tempBlockPathPrefix + std::to_string(blockId);
        const int fdBlock = StorageLibcApiWrapper::open64(
            tempBlockPath.c_str(),
            O_RDONLY, 0666);
        if (fdBlock == -1)
        {
            // No more blocks
            break;
        }

        struct stat64 statBuf;
        if (StorageLibcApiWrapper::fstat64(fdBlock, &statBuf) == -1)
        {
            LOG_ERROR("Failed to stat temp block file at path {}", tempBlockPath);
            StorageLibcApiWrapper::close(fdBlock);
            return -1;
        }
        if (statBuf.st_size < 0)
        {
            StorageLibcApiWrapper::close(fdBlock);
            return -1;
        }
        const size_t fileSize = static_cast<size_t>(statBuf.st_size);

        std::unique_ptr<char[]> buffer(new char[fileSize]);
        ssize_t nBytesRead = 0;
        nBytesRead = StorageLibcApiWrapper::pread(
                    fdBlock,
                    buffer.get(),
                    fileSize,
                    0);
        if (nBytesRead != static_cast<ssize_t>(fileSize))
        {
            LOG_ERROR("Failed to read all bytes from temp block file at path {}. Expected={}, Read={}",
                tempBlockPath,
                fileSize,
                nBytesRead);
            StorageLibcApiWrapper::close(fdBlock);
            return -1;
        }

        LOG_INFO("Appending blockId={} of size={} at offset={} to final file={}",
            blockId,
            nBytesRead,
            writeOffset,
            baseFilePath);
        ssize_t nBytesWritten = StorageLibcApiWrapper::pwrite(
            fdFinal,
            buffer.get(),
            static_cast<size_t>(nBytesRead),
            writeOffset);
        if (nBytesWritten != nBytesRead)
        {
            LOG_ERROR("Failed to write all bytes to final file at path {}. Expected={}, Written={}",
                baseFilePath,
                nBytesRead,
                nBytesWritten);
            StorageLibcApiWrapper::close(fdBlock);
            return -1;
        }

        StorageLibcApiWrapper::close(fdBlock);
        // Delete the temp block file
        ::unlink(tempBlockPath.c_str());
        blockId++;
        nChunks++;
        writeOffset += nBytesRead;
    }

    StorageLibcApiWrapper::close(fdFinal);

    // Mirror BlobClient: persist the version as blob metadata. The mock backend
    // simulates blob metadata via an xattr that MockGetPropertiesApi reads back,
    // so the Commit -> GetProperties round-trip behaves like the real backend.
    if (!version.empty())
    {
        const std::string xattrName =
            std::string("user.") + StorageInterceptConstants::BlobMetadataKeyVersion;
        if (setxattr(baseFilePath.c_str(), xattrName.c_str(), version.c_str(), version.size(), 0) != 0)
        {
            LOG_ERROR("Failed to set dacsfileversion xattr on file={} version={}",
                baseFilePath, version);
        }
    }

    LOG_INFO("Committed all {} blocks to final file={}", nChunks, baseFilePath);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------