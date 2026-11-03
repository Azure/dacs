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

#include "ConfigUtils.h"
#include "Logger.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <strings.h>
//---------------------------------------------------------------------------------------------------------------------
namespace
{
    template<typename T>
    T GetNumericConfigEnvVar(const char* name, T defaultValue, T minValue, T maxValue)
    {
        if (minValue > maxValue || defaultValue < minValue || defaultValue > maxValue)
        {
            LOG_ERROR("Invalid config bounds/default for {}: default={}, min={}, max={}",
                name, defaultValue, minValue, maxValue);
            throw std::invalid_argument(std::string("Invalid config bounds/default for ") + name);
        }

        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            return defaultValue;
        }

        long long parsedValue;
        try
        {
            parsedValue = std::stoll(value);
        }
        catch (const std::invalid_argument&)
        {
            LOG_ERROR("Invalid integer for config {}='{}'", name, value);
            throw;
        }
        catch (const std::out_of_range&)
        {
            const char* first = value;
            while (std::isspace(static_cast<unsigned char>(*first)))
            {
                ++first;
            }
            parsedValue = *first == '-' ? std::numeric_limits<long long>::min() : std::numeric_limits<long long>::max();
        }

        const long long clampedValue = std::clamp(
            parsedValue, static_cast<long long>(minValue), static_cast<long long>(maxValue));
        if (clampedValue != parsedValue)
        {
            LOG_WARN("Config {}='{}' is outside [{}, {}]; clamping to {}",
                name, value, minValue, maxValue, clampedValue);
        }
        return static_cast<T>(clampedValue);
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool GetConfigEnvVar(const char* name, bool defaultValue)
{
    const char* value = std::getenv(name);
    return value == nullptr ? defaultValue : std::strcmp(value, "1") == 0 || ::strcasecmp(value, "true") == 0;
}
//---------------------------------------------------------------------------------------------------------------------
int GetConfigEnvVar(const char* name, int defaultValue, int minValue, int maxValue)
{
    return GetNumericConfigEnvVar(name, defaultValue, minValue, maxValue);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t GetConfigEnvVar(const char* name, uint32_t defaultValue, uint32_t minValue, uint32_t maxValue)
{
    return GetNumericConfigEnvVar(name, defaultValue, minValue, maxValue);
}
//---------------------------------------------------------------------------------------------------------------------
std::string GetConfigEnvVar(const char* name, const std::string& defaultValue)
{
    const char* value = std::getenv(name);
    return value == nullptr ? defaultValue : std::string(value);
}
//---------------------------------------------------------------------------------------------------------------------
std::string GetConfigEnvVar(const char* name, const char* defaultValue)
{
    return GetConfigEnvVar(name, std::string(defaultValue));
}
//---------------------------------------------------------------------------------------------------------------------
