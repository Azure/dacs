//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "EncryptionUtilTest.h"
#include "StorageInterceptLogger.h"
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
EncryptionUtilTest::EncryptionUtilTest()
    : TestBase()
{
}
//---------------------------------------------------------------------------------------------------------------------
void EncryptionUtilTest::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
void EncryptionUtilTest::GetRandDataBuffer(unsigned char* buffer, size_t bufSize)
{
    std::generate(buffer, buffer + bufSize, []{return rand() % 255;});
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(EncryptionUtilTest, EncryptDecryptTest)
{
    string encKey = "1234567890";
    EncryptionUtil encUtil(true, encKey);

    // 12 bytes IV
    string iv = encUtil.GenerateIV();

    // mac tag should be 16 bytes
    string tag(16, '\0');

    unsigned char origBuffer[100];
    GetRandDataBuffer(origBuffer, 100);

    unsigned char encBuffer[100+16];
    int encLen = encUtil.GcmEncrypt(origBuffer, 100, iv, encBuffer, tag);
    ASSERT_EQ(100, encLen);

    unsigned char decBuffer[100];
    int finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(100, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    // try minimum size 1
    GetRandDataBuffer(origBuffer, 1);
    encLen = encUtil.GcmEncrypt(origBuffer, 1, iv, encBuffer, tag);
    ASSERT_EQ(1, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(1, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    // pass invalid tag
    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, "1234567890123456", iv, decBuffer);
    ASSERT_EQ(-1, finalLen);

    // pass invalid iv
    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, "123456789011", decBuffer);
    ASSERT_EQ(-1, finalLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(1, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    // try invalid key
    EncryptionUtil encUtilBad(true, "1234567891");
    finalLen = encUtilBad.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(-1, finalLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(1, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    // zero length not supported
    encLen = encUtil.GcmEncrypt(origBuffer, 0, iv, encBuffer, tag);
    ASSERT_EQ(-1, encLen);

    // try different lengths to check block size boundaries
    GetRandDataBuffer(origBuffer, 16);
    encLen = encUtil.GcmEncrypt(origBuffer, 16, iv, encBuffer, tag);
    ASSERT_EQ(16, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(16, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    GetRandDataBuffer(origBuffer, 32);
    encLen = encUtil.GcmEncrypt(origBuffer, 32, iv, encBuffer, tag);
    ASSERT_EQ(32, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(32, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    GetRandDataBuffer(origBuffer, 15);
    encLen = encUtil.GcmEncrypt(origBuffer, 15, iv, encBuffer, tag);
    ASSERT_EQ(15, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(15, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    GetRandDataBuffer(origBuffer, 17);
    encLen = encUtil.GcmEncrypt(origBuffer, 17, iv, encBuffer, tag);
    ASSERT_EQ(17, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(17, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    GetRandDataBuffer(origBuffer, 31);
    encLen = encUtil.GcmEncrypt(origBuffer, 31, iv, encBuffer, tag);
    ASSERT_EQ(31, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(31, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    GetRandDataBuffer(origBuffer, 33);
    encLen = encUtil.GcmEncrypt(origBuffer, 33, iv, encBuffer, tag);
    ASSERT_EQ(33, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(33, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    // try large buffer (to match cache chunk size)
    unique_ptr<unsigned char[]>largeOrigBuffer(new unsigned char[4*1024*1024 + 1]);
    GetRandDataBuffer(largeOrigBuffer.get(), 4*1024*1024);

    unique_ptr<unsigned char[]> largeEncBuffer(new unsigned char[4*1024*1024 + 1 + 16]);
    unique_ptr<unsigned char[]> largeDecBuffer(new unsigned char[4*1024*1024 + 1]);
    encLen = encUtil.GcmEncrypt(largeOrigBuffer.get(), 4*1024*1024, iv, largeEncBuffer.get(), tag);
    ASSERT_EQ(encLen, 4*1024*1024);

    finalLen = encUtil.GcmDecrypt(largeEncBuffer.get(), encLen, tag, iv, largeDecBuffer.get());
    ASSERT_EQ(4*1024*1024, finalLen);
    AssertEqArray<unsigned char>(largeOrigBuffer.get(), largeDecBuffer.get(), finalLen);

    GetRandDataBuffer(largeOrigBuffer.get(), 4*1024*1024 + 1);
    encLen = encUtil.GcmEncrypt(largeOrigBuffer.get(), 4*1024*1024 + 1, iv, largeEncBuffer.get(), tag);
    ASSERT_EQ(encLen, 4*1024*1024 + 1);

    finalLen = encUtil.GcmDecrypt(largeEncBuffer.get(), encLen, tag, iv, largeDecBuffer.get());
    ASSERT_EQ(4*1024*1024 + 1, finalLen);
    AssertEqArray<unsigned char>(largeOrigBuffer.get(), largeDecBuffer.get(), finalLen);

    GetRandDataBuffer(largeOrigBuffer.get(), 4*1024*1024 - 1);
    encLen = encUtil.GcmEncrypt(largeOrigBuffer.get(), 4*1024*1024 - 1, iv, largeEncBuffer.get(), tag);
    ASSERT_EQ(encLen, 4*1024*1024 - 1);

    finalLen = encUtil.GcmDecrypt(largeEncBuffer.get(), encLen, tag, iv, largeDecBuffer.get());
    ASSERT_EQ(4*1024*1024 - 1, finalLen);
    AssertEqArray<unsigned char>(largeOrigBuffer.get(), largeDecBuffer.get(), finalLen);
}
//---------------------------------------------------------------------------------------------------------------------
