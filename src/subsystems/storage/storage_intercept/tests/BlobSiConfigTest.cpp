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

// Unit tests for BlobSiWrapper config generation (generate_si_config)
//---------------------------------------------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unistd.h>

// Declared in BlobSiWrapper.cpp (extern "C" with default visibility)
extern "C" bool generate_si_config(const char* config_path);

//---------------------------------------------------------------------------------------------------------------------
// Test fixture
//---------------------------------------------------------------------------------------------------------------------
class ConfigTest : public ::testing::Test
{
protected:
    std::string config_path_;

    // All env vars that init_si_config / generate_si_config reads
    static constexpr const char* env_vars_[] = {
        "STORAGE_INTERCEPT_CONFIG_FILEPATH",
        "STORAGE_INTCPT_MOUNT_PATH",
        "USE_SAS_TOKEN",
        "STORAGE_INTCPT_SAS_TOKEN",
        "SI_azBlobAccessToken",
        "AZURE_CLIENT_ID",
        "AZURE_FEDERATED_TOKEN_FILE",
        "AZURE_TENANT_ID",
        "AZURE_AUTHORITY_HOST",
        "MSI_ENDPOINT",
        "MSI_SECRET",
        "CACHE_SERVER_LIST",
        "CACHE_SERVER_PORT",
        "CACHE_DISCOVERY_URL",
        "AZURE_BLOB_ENDPOINT_SUFFIX",
    };

    // Save original values (empty string means "was unset")
    std::unordered_map<std::string, std::string> saved_env_;
    std::unordered_map<std::string, bool> was_set_;

    void SetUp() override
    {
        // Save and clear all relevant env vars
        for (const char* var : env_vars_)
        {
            const char* val = getenv(var);
            was_set_[var] = (val != nullptr);
            saved_env_[var] = val ? val : "";
            unsetenv(var);
        }

        // Unique config path per test
        static int test_counter = 0;
        config_path_ = "/tmp/test_si_config_" + std::to_string(getpid()) + "_" + std::to_string(test_counter++);
    }

    void TearDown() override
    {
        // Restore all env vars to original state
        for (const char* var : env_vars_)
        {
            if (was_set_[var])
                setenv(var, saved_env_[var].c_str(), 1);
            else
                unsetenv(var);
        }

        // Clean up config file
        unlink(config_path_.c_str());
    }

    // Parse config file into key-value map (first whitespace-separated token = key, rest = value)
    std::unordered_map<std::string, std::string> read_config()
    {
        std::unordered_map<std::string, std::string> kv;
        FILE* f = fopen(config_path_.c_str(), "r");
        if (!f) return kv;
        char line[1024];
        while (fgets(line, sizeof(line), f))
        {
            std::string l(line);
            // Strip trailing newline
            if (!l.empty() && l.back() == '\n') l.pop_back();
            // Skip comments and empty lines
            if (l.empty() || l[0] == '#') continue;
            // Split at first space
            auto pos = l.find(' ');
            if (pos != std::string::npos)
                kv[l.substr(0, pos)] = l.substr(pos + 1);
            else
                kv[l] = "";
        }
        fclose(f);
        return kv;
    }

    bool config_file_exists()
    {
        return access(config_path_.c_str(), F_OK) == 0;
    }

    bool has_key(const std::unordered_map<std::string, std::string>& kv, const std::string& key)
    {
        return kv.find(key) != kv.end();
    }
};

// --- fopen failure ---
TEST_F(ConfigTest, FopenFailureReturnsFalse)
{
    setenv("AZURE_CLIENT_ID", "test-client", 1);
    // Use an invalid path that fopen cannot write to
    bool ok = generate_si_config("/nonexistent_dir/impossible_path.config");
    EXPECT_FALSE(ok);
}

// --- Auth: no credentials ---
TEST_F(ConfigTest, NoCredentialsFallsBackToAzureIdentitySDK)
{
    // No SAS, no access token, no AZURE_CLIENT_ID — falls back to Azure Identity SDK
    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["azBlobUseAzureIdentitySDK"], "true");
}

// --- Auth: WIF env vars present — falls to Azure Identity SDK (handles WIF internally) ---
TEST_F(ConfigTest, WifEnvFallsToAzureIdentitySDK)
{
    setenv("AZURE_CLIENT_ID", "test-client", 1);
    setenv("AZURE_FEDERATED_TOKEN_FILE", "/var/run/secrets/token", 1);
    // No MSI_ENDPOINT/MSI_SECRET, so falls to Azure Identity SDK

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["azBlobUseAzureIdentitySDK"], "true");
    // DefaultAzureCredential will auto-detect WIF from env vars at runtime
}

// --- Auth: SAS token ---
TEST_F(ConfigTest, SASTokenAuth)
{
    setenv("USE_SAS_TOKEN", "true", 1);
    setenv("STORAGE_INTCPT_SAS_TOKEN", "sv=2024-11-04&ss=b&srt=o&sp=r&se=2099-01-01&sig=test", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_NE(kv.find("azBlobSASToken"), kv.end());
    EXPECT_EQ(kv["azBlobSASToken"], "sv=2024-11-04&ss=b&srt=o&sp=r&se=2099-01-01&sig=test");
}

// --- Auth: access token ---
TEST_F(ConfigTest, AccessTokenAuth)
{
    setenv("SI_azBlobAccessToken", "eyJ0eXAiOiJKV1Q...", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["azBlobAccessToken"], "eyJ0eXAiOiJKV1Q...");
}

// --- Auth: WIF env vars — Azure Identity SDK handles WIF via DefaultAzureCredential ---
TEST_F(ConfigTest, WIFAuthUsesAzureIdentitySDK)
{
    setenv("AZURE_CLIENT_ID", "test-client-id", 1);
    setenv("AZURE_FEDERATED_TOKEN_FILE", "/var/run/secrets/token", 1);
    setenv("AZURE_TENANT_ID", "test-tenant-id", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    // No explicit WIF config — Azure Identity SDK reads these env vars at runtime
    EXPECT_EQ(kv["azBlobUseAzureIdentitySDK"], "true");
    EXPECT_FALSE(has_key(kv, "azBlobFederatedTokenFile"));
}

// --- Auth: WIF env with custom authority — still uses Azure Identity SDK ---
TEST_F(ConfigTest, WIFAuthCustomAuthorityUsesAzureIdentitySDK)
{
    setenv("AZURE_CLIENT_ID", "test-client-id", 1);
    setenv("AZURE_FEDERATED_TOKEN_FILE", "/var/run/secrets/token", 1);
    setenv("AZURE_TENANT_ID", "test-tenant-id", 1);
    setenv("AZURE_AUTHORITY_HOST", "https://login.microsoftonline.us", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["azBlobUseAzureIdentitySDK"], "true");
}

// --- Auth: MSI defaults ---
TEST_F(ConfigTest, MSIAuthRequiresEndpointAndSecret)
{
    // AZURE_CLIENT_ID set but no MSI_ENDPOINT/MSI_SECRET — falls to Azure Identity SDK
    setenv("AZURE_CLIENT_ID", "test-client-id", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["azBlobUseAzureIdentitySDK"], "true");
    EXPECT_FALSE(has_key(kv, "azBlobMSIEndpoint"));
}

// --- Auth: MSI custom endpoint ---
TEST_F(ConfigTest, MSIAuthCustomEndpoint)
{
    setenv("AZURE_CLIENT_ID", "test-client-id", 1);
    setenv("MSI_ENDPOINT", "http://custom-msi:8080/token", 1);
    setenv("MSI_SECRET", "my-secret", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["azBlobMSIEndpoint"], "http://custom-msi:8080/token");
    EXPECT_EQ(kv["azBlobMSISecret"], "my-secret");
}

// --- Auth precedence: SAS > access token ---
TEST_F(ConfigTest, PrecedenceSASBeatsAccessToken)
{
    setenv("USE_SAS_TOKEN", "true", 1);
    setenv("STORAGE_INTCPT_SAS_TOKEN", "sv=2024&sig=test", 1);
    setenv("SI_azBlobAccessToken", "should-be-ignored", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_NE(kv.find("azBlobSASToken"), kv.end());
    EXPECT_FALSE(has_key(kv, "azBlobAccessToken"));
}

// --- Auth precedence: access token > MSI/Azure Identity SDK ---
TEST_F(ConfigTest, PrecedenceAccessTokenBeatsOthers)
{
    setenv("SI_azBlobAccessToken", "my-token", 1);
    setenv("AZURE_CLIENT_ID", "test-client-id", 1);
    setenv("MSI_ENDPOINT", "http://msi:8080/token", 1);
    setenv("MSI_SECRET", "secret", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["azBlobAccessToken"], "my-token");
    EXPECT_FALSE(has_key(kv, "azBlobMSIEndpoint"));
    EXPECT_FALSE(has_key(kv, "azBlobUseAzureIdentitySDK"));
}

// --- SAS token requires USE_SAS_TOKEN=true ---
TEST_F(ConfigTest, SASTokenRequiresUseSASTrue)
{
    // STORAGE_INTCPT_SAS_TOKEN set but USE_SAS_TOKEN not "true"
    setenv("STORAGE_INTCPT_SAS_TOKEN", "sv=2024&sig=test", 1);
    setenv("AZURE_CLIENT_ID", "fallback-client", 1);
    setenv("MSI_ENDPOINT", "http://msi:8080/token", 1);
    setenv("MSI_SECRET", "secret", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    // SAS should NOT be used — falls through to MSI
    EXPECT_FALSE(has_key(kv, "azBlobSASToken"));
    EXPECT_EQ(kv["azBlobIdentityClientId"], "fallback-client");
}

// --- Mount path: default ---
TEST_F(ConfigTest, DefaultMountPath)
{
    setenv("AZURE_CLIENT_ID", "test", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["storagePath"], "/mnt/cache");
}

// --- Mount path: custom ---
TEST_F(ConfigTest, CustomMountPath)
{
    setenv("AZURE_CLIENT_ID", "test", 1);
    setenv("STORAGE_INTCPT_MOUNT_PATH", "/custom/path", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["storagePath"], "/custom/path");
}

// --- Performance tuning defaults ---
TEST_F(ConfigTest, PerfTuningDefaults)
{
    setenv("AZURE_CLIENT_ID", "test", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["maxConnsPerBlobClient"], "84");
    EXPECT_EQ(kv["attributesCacheEnable"], "true");
}

// --- Storage basics ---
TEST_F(ConfigTest, StorageBasics)
{
    setenv("AZURE_CLIENT_ID", "test", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["type"], "blob");
    EXPECT_EQ(kv["azBlobDynamicAccount"], "true");
    EXPECT_EQ(kv["azBlobDynamicContainer"], "true");
}

// --- Cache: static server list ---
TEST_F(ConfigTest, CacheStaticServerList)
{
    setenv("AZURE_CLIENT_ID", "test", 1);
    setenv("CACHE_SERVER_LIST", "10.0.0.1#10.0.0.2#10.0.0.3", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["cacheEnable"], "true");
    EXPECT_EQ(kv["cacheServerList"], "10.0.0.1#10.0.0.2#10.0.0.3");
    EXPECT_EQ(kv["cacheServerPort"], "9065");
    EXPECT_EQ(kv["cacheEnableRemote"], "true");
}

// --- Cache: custom port ---
TEST_F(ConfigTest, CacheCustomPort)
{
    setenv("AZURE_CLIENT_ID", "test", 1);
    setenv("CACHE_SERVER_LIST", "10.0.0.1", 1);
    setenv("CACHE_SERVER_PORT", "9999", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["cacheServerPort"], "9999");
}

// --- Cache: discovery URL ---
TEST_F(ConfigTest, CacheDiscoveryUrl)
{
    setenv("AZURE_CLIENT_ID", "test", 1);
    setenv("CACHE_DISCOVERY_URL", "http://scheduler.cache-server:8080/api/v1/servers", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["cacheEnable"], "true");
    EXPECT_EQ(kv["cacheServerDiscoveryEnabled"], "true");
    EXPECT_EQ(kv["cacheServerDiscoveryEndpoint"], "http://scheduler.cache-server:8080/api/v1/servers");
    EXPECT_EQ(kv["cacheServerDiscoveryIntervalInSeconds"], "300");
    EXPECT_FALSE(has_key(kv, "cacheServerList"));
}

// --- Cache precedence: discovery URL > static list ---
TEST_F(ConfigTest, CachePrecedenceDiscoveryUrlBeatsStaticList)
{
    setenv("AZURE_CLIENT_ID", "test", 1);
    setenv("CACHE_DISCOVERY_URL", "http://scheduler:8080/api/v1/servers", 1);
    setenv("CACHE_SERVER_LIST", "10.0.0.1#10.0.0.2", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    // Discovery URL wins — no static list in config
    EXPECT_EQ(kv["cacheServerDiscoveryEnabled"], "true");
    EXPECT_FALSE(has_key(kv, "cacheServerList"));
}

// --- Cache: empty server list — cache disabled ---
TEST_F(ConfigTest, CacheEmptyServerListDisablesCache)
{
    setenv("AZURE_CLIENT_ID", "test", 1);
    setenv("CACHE_SERVER_LIST", "", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    // Empty string → treated as unset, cache disabled
    EXPECT_FALSE(has_key(kv, "cacheEnable"));
}

// --- Cache: empty env vars — cache disabled ---
TEST_F(ConfigTest, CacheEmptyEnvVarsDisablesCache)
{
    setenv("AZURE_CLIENT_ID", "test", 1);
    setenv("CACHE_SERVER_LIST", "", 1);
    setenv("CACHE_DISCOVERY_URL", "", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_FALSE(has_key(kv, "cacheEnable"));
    EXPECT_FALSE(has_key(kv, "cacheServerDiscoveryEnabled"));
}

// --- Cache: no config at all — cache section omitted ---
TEST_F(ConfigTest, NoCacheServers)
{
    setenv("AZURE_CLIENT_ID", "test", 1);
    // No CACHE_SERVER_LIST, no CACHE_DISCOVERY_URL

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_FALSE(has_key(kv, "cacheEnable"));
    EXPECT_FALSE(has_key(kv, "cacheServerList"));
    EXPECT_FALSE(has_key(kv, "cacheEnableRemote"));
    EXPECT_FALSE(has_key(kv, "cacheRetryMaxAttempts"));
}

// --- Cache: tuning settings when cache is enabled ---
TEST_F(ConfigTest, CacheTuningSettings)
{
    setenv("AZURE_CLIENT_ID", "test", 1);
    setenv("CACHE_SERVER_LIST", "10.0.0.1", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    auto kv = read_config();
    EXPECT_EQ(kv["cacheRetryMaxAttempts"], "5");
    EXPECT_EQ(kv["cacheRetryBaseDelayMillis"], "100");
    EXPECT_EQ(kv["cacheRetryMaxDelayMillis"], "1000");
    EXPECT_EQ(kv["cacheMaxUploadBufferSizePerProcessGB"], "400");
    EXPECT_EQ(kv["cacheNumUploadThreadsPerProcess"], "80");
    EXPECT_EQ(kv["attributesCacheUseLocalServer"], "false");
    EXPECT_EQ(kv["attributesCacheUseRemoteServer"], "true");
}

// --- Provenance comment present ---
TEST_F(ConfigTest, ProvenanceComment)
{
    setenv("AZURE_CLIENT_ID", "test", 1);

    bool ok = generate_si_config(config_path_.c_str());
    EXPECT_TRUE(ok);
    // Read raw file to check comment line
    FILE* f = fopen(config_path_.c_str(), "r");
    ASSERT_NE(f, nullptr);
    char line[1024];
    ASSERT_NE(fgets(line, sizeof(line), f), nullptr);
    fclose(f);
    EXPECT_TRUE(strstr(line, "Auto-generated by py_dacs_client") != nullptr);
}

//---------------------------------------------------------------------------------------------------------------------
