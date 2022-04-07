//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "TestBase.h"
#include "EncryptionUtil.h"
#include <string>
//---------------------------------------------------------------------------------------------------------------------
class EncryptionUtilTest : public TestBase
{
    protected:
        EncryptionUtilTest();
        void SetUp() override;

        void GetRandDataBuffer(unsigned char* buffer, size_t bufSize);

        int GcmEncryptWrapper(EncryptionUtil& encUtil, unsigned char *plaintext, int plaintextLen,
                    const std::string& iv,
                    unsigned char *ciphertext,
                    std::string& tag);
        int GcmDecryptWrapper(EncryptionUtil& encUtil, unsigned char *ciphertext, int ciphertextLen,
                    const std::string& tag,
                    const std::string& iv,
                    unsigned char *plaintext);
        std::string GenerateIVWrapper(EncryptionUtil& encUtil);

        template <typename T>
        void AssertEqArray(T* left, T* right, int arraySize)
        {
            for (int i = 0; i < arraySize; i++)
            {
                ASSERT_EQ(left[i], right[i]);
            }
        }
};
//---------------------------------------------------------------------------------------------------------------------