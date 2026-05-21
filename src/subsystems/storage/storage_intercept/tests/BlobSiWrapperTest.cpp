//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// Unit tests for BlobSiWrapper (blob_read) — uses TestBase mock infrastructure
//---------------------------------------------------------------------------------------------------------------------
#include "TestBase.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptConfig.h"
#include <gtest/gtest.h>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// The blob_read function under test (declared extern "C" in the wrapper)
extern "C" ssize_t blob_read(
    const char* account, const char* container, const char* blob, void* buf,
    size_t offset, size_t length, char* error_buf, size_t error_buf_size);

// Access the fail-fast flag from BlobSiWrapper.cpp
extern bool s_config_init_failed;

//---------------------------------------------------------------------------------------------------------------------
// Test fixture — inherits TestBase for mock remote client + real SI
//---------------------------------------------------------------------------------------------------------------------
class BlobSiWrapperTest : public TestBase
{
protected:
    // Account/container used for test blobs
    static constexpr const char* TEST_ACCOUNT = "testaccount";
    static constexpr const char* TEST_CONTAINER = "testcontainer";

    void SetUp() override
    {
        // Override mount path to match TestBase's intercept path so blob_read
        // constructs paths that the real SI layer (backed by mock remote client) can serve
        setenv("STORAGE_INTCPT_MOUNT_PATH", m_strInterceptPath.c_str(), 1);
        s_config_init_failed = false;
    }

    // Create a blob file in the mock remote client root at the path blob_read will construct.
    // blob_read path = <mount>/<account>.blob.core.windows.net/<container>/<blob>
    // SI strips mount prefix → MockRemoteClient opens <mockRoot>/<remainder>
    void CreateMockBlob(const char* account, const char* container,
                        const char* blob, const std::string& content)
    {
        std::string mockRoot = StorageInterceptConfig::GetInstance().GetMockClientRoot("");
        std::string relPath = std::string(account) + ".blob.core.windows.net/"
                            + container + "/" + blob;
        std::string fullPath = mockRoot + "/" + relPath;

        // Create intermediate directories
        std::string dir = fullPath.substr(0, fullPath.rfind('/'));
        std::string cmd = "mkdir -p " + dir;
        ::system(cmd.c_str());

        int fd = ::open(fullPath.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
        ASSERT_NE(fd, -1) << "Failed to create mock blob at " << fullPath;
        ssize_t written = ::write(fd, content.data(), content.size());
        ASSERT_EQ(written, static_cast<ssize_t>(content.size()));
        ::close(fd);
    }
};

//---------------------------------------------------------------------------------------------------------------------
// Parameter validation tests
//---------------------------------------------------------------------------------------------------------------------

TEST_F(BlobSiWrapperTest, NullAccountReturnsError)
{
    char error_buf[512] = {};
    char buf[10];
    ssize_t ret = blob_read(nullptr, "container", "blob", buf, 0, 10, error_buf, sizeof(error_buf));
    EXPECT_EQ(ret, -1);
}

TEST_F(BlobSiWrapperTest, NullContainerReturnsError)
{
    char error_buf[512] = {};
    char buf[10];
    ssize_t ret = blob_read("account", nullptr, "blob", buf, 0, 10, error_buf, sizeof(error_buf));
    EXPECT_EQ(ret, -1);
}

TEST_F(BlobSiWrapperTest, NullBlobReturnsError)
{
    char error_buf[512] = {};
    char buf[10];
    ssize_t ret = blob_read("account", "container", nullptr, buf, 0, 10, error_buf, sizeof(error_buf));
    EXPECT_EQ(ret, -1);
}

TEST_F(BlobSiWrapperTest, NullBufferReturnsError)
{
    char error_buf[512] = {};
    ssize_t ret = blob_read("account", "container", "blob", nullptr, 0, 10, error_buf, sizeof(error_buf));
    EXPECT_EQ(ret, -1);
}

TEST_F(BlobSiWrapperTest, NullErrorBufIsAllowed)
{
    char buf[10];
    ssize_t ret = blob_read("account", "container", "nonexistent", buf, 0, 10, nullptr, 0);
    EXPECT_EQ(ret, -1);
}

TEST_F(BlobSiWrapperTest, LengthExceedsSsizeMaxReturnsError)
{
    char buf[10];
    char error_buf[512] = {};
    ssize_t ret = blob_read("account", "container", "blob.bin", buf, 0,
                            static_cast<size_t>(SSIZE_MAX) + 1, error_buf, sizeof(error_buf));
    EXPECT_EQ(ret, -1);
    EXPECT_NE(std::string(error_buf).find("SSIZE_MAX"), std::string::npos);
}

TEST_F(BlobSiWrapperTest, OffsetExceedsMaxReturnsError)
{
    char buf[10];
    char error_buf[512] = {};
    ssize_t ret = blob_read("account", "container", "blob.bin", buf,
                            SIZE_MAX, 10, error_buf, sizeof(error_buf));
    EXPECT_EQ(ret, -1);
    EXPECT_NE(std::string(error_buf).find("offset"), std::string::npos);
}

TEST_F(BlobSiWrapperTest, ConfigInitFailedReturnsError)
{
    s_config_init_failed = true;
    char buf[64];
    char error_buf[512] = {};
    ssize_t ret = blob_read("account", "container", "blob.bin", buf, 0, 10,
                            error_buf, sizeof(error_buf));
    EXPECT_EQ(ret, -1);
    EXPECT_NE(std::string(error_buf).find("config initialization failed"), std::string::npos);
}

TEST_F(BlobSiWrapperTest, ConfigInitFailedNullErrorBuf)
{
    s_config_init_failed = true;
    char buf[64];
    ssize_t ret = blob_read("account", "container", "blob.bin", buf, 0, 10,
                            nullptr, 0);
    EXPECT_EQ(ret, -1);
}

//---------------------------------------------------------------------------------------------------------------------
// Functional tests — use TestBase's mock remote client (real SI backed by local filesystem)
//---------------------------------------------------------------------------------------------------------------------

TEST_F(BlobSiWrapperTest, SuccessfulRead)
{
    if (!m_bUsingMock)
    {
        GTEST_SKIP() << "Requires mock remote client";
    }

    std::string content = "Hello, DACS cache!";
    CreateMockBlob(TEST_ACCOUNT, TEST_CONTAINER, "model/weights.bin", content);

    char buf[64] = {};
    char error_buf[512] = {};
    ssize_t ret = blob_read(TEST_ACCOUNT, TEST_CONTAINER, "model/weights.bin",
                            buf, 0, content.size(), error_buf, sizeof(error_buf));

    EXPECT_EQ(ret, static_cast<ssize_t>(content.size())) << "error: " << error_buf;
    EXPECT_EQ(std::string(buf, content.size()), content);
}

TEST_F(BlobSiWrapperTest, ReadWithOffset)
{
    if (!m_bUsingMock)
    {
        GTEST_SKIP() << "Requires mock remote client";
    }

    std::string content = "0123456789ABCDEF";
    CreateMockBlob(TEST_ACCOUNT, TEST_CONTAINER, "model/shard.safetensors", content);

    char buf[8] = {};
    char error_buf[512] = {};
    ssize_t ret = blob_read(TEST_ACCOUNT, TEST_CONTAINER, "model/shard.safetensors",
                            buf, 4, 8, error_buf, sizeof(error_buf));

    EXPECT_EQ(ret, 8) << "error: " << error_buf;
    EXPECT_EQ(std::string(buf, 8), "456789AB");
}

TEST_F(BlobSiWrapperTest, BlobNotFoundReturnsError)
{
    if (!m_bUsingMock)
    {
        GTEST_SKIP() << "Requires mock remote client";
    }

    char buf[10];
    char error_buf[512] = {};
    ssize_t ret = blob_read(TEST_ACCOUNT, TEST_CONTAINER, "does/not/exist.bin",
                            buf, 0, 10, error_buf, sizeof(error_buf));

    EXPECT_EQ(ret, -1);
    EXPECT_NE(error_buf[0], '\0');
}

TEST_F(BlobSiWrapperTest, MultipleReadsFromSameBlob)
{
    if (!m_bUsingMock)
    {
        GTEST_SKIP() << "Requires mock remote client";
    }

    std::string content = "AABBCCDDEE";
    CreateMockBlob(TEST_ACCOUNT, TEST_CONTAINER, "model/multi.bin", content);

    char buf1[4] = {};
    char buf2[4] = {};
    char error_buf[512] = {};

    ssize_t ret1 = blob_read(TEST_ACCOUNT, TEST_CONTAINER, "model/multi.bin",
                             buf1, 0, 4, error_buf, sizeof(error_buf));
    EXPECT_EQ(ret1, 4) << "error: " << error_buf;
    EXPECT_EQ(std::string(buf1, 4), "AABB");

    ssize_t ret2 = blob_read(TEST_ACCOUNT, TEST_CONTAINER, "model/multi.bin",
                             buf2, 6, 4, error_buf, sizeof(error_buf));
    EXPECT_EQ(ret2, 4) << "error: " << error_buf;
    EXPECT_EQ(std::string(buf2, 4), "DDEE");
}

//---------------------------------------------------------------------------------------------------------------------
