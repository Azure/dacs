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
#include "BlobAuthorizationRetry.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptConstants.h"
#include "TestBase.h"

#include "gtest/gtest.h"
//---------------------------------------------------------------------------------------------------------------------
TEST(BlobAuthorizationRetryTest, RetriesOnlyAuthorizationPermissionMismatchWithRefreshableCredential)
{
    EXPECT_TRUE(blobauthretry::ShouldRetry(
        403, "AuthorizationPermissionMismatch", true, 1, 3));
    EXPECT_TRUE(blobauthretry::ShouldRetry(
        403, "AuthorizationPermissionMismatch", true, 2, 3));

    EXPECT_FALSE(blobauthretry::ShouldRetry(
        403, "AuthorizationPermissionMismatch", false, 1, 3));
    EXPECT_FALSE(blobauthretry::ShouldRetry(
        403, "AuthenticationFailed", true, 1, 3));
    EXPECT_FALSE(blobauthretry::ShouldRetry(
        403, "AccountIsDisabled", true, 1, 3));
    EXPECT_FALSE(blobauthretry::ShouldRetry(
        401, "AuthorizationPermissionMismatch", true, 1, 3));
}
//---------------------------------------------------------------------------------------------------------------------
TEST(BlobAuthorizationRetryTest, StopsAtConfiguredAttemptLimit)
{
    EXPECT_FALSE(blobauthretry::ShouldRetry(
        403, "AuthorizationPermissionMismatch", true, 3, 3));
    EXPECT_FALSE(blobauthretry::ShouldRetry(
        403, "AuthorizationPermissionMismatch", true, 1, 1));
}
//---------------------------------------------------------------------------------------------------------------------
class BlobAuthorizationRetryConfigTest : public TestBase
{
};
//---------------------------------------------------------------------------------------------------------------------
TEST_F(BlobAuthorizationRetryConfigTest, DefaultsToThreeAttemptsAndSupportsDisablingRetries)
{
    EXPECT_EQ(
        StorageInterceptConfig::GetInstance().GetBlobAuthorizationRetryMaxAttempts(),
        3u);

    OverrideConfig(
        StorageInterceptConstants::ConfigNameBlobAuthorizationRetryMaxAttempts,
        "1");
    EXPECT_EQ(
        StorageInterceptConfig::GetInstance().GetBlobAuthorizationRetryMaxAttempts(),
        1u);
}
//---------------------------------------------------------------------------------------------------------------------
