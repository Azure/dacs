// Copyright (c) Microsoft Corporation.
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