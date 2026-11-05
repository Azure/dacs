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

#pragma once
#include <cstdint>
#include <string>
//---------------------------------------------------------------------------------------------------------------------
// All overloads use defaultValue when unset. Boolean values are true for "1" or case-insensitive "true".
bool GetConfigEnvVar(const char* name, bool defaultValue);
// Numeric overloads clamp to inclusive bounds and log a warning when clamping.
// Numeric prefix parsing is preserved; invalid text, bounds, or defaults throw std::invalid_argument.
int GetConfigEnvVar(const char* name, int defaultValue, int minValue, int maxValue);
uint32_t GetConfigEnvVar(const char* name, uint32_t defaultValue, uint32_t minValue, uint32_t maxValue);
std::string GetConfigEnvVar(const char* name, const std::string& defaultValue);
std::string GetConfigEnvVar(const char* name, const char* defaultValue);
//---------------------------------------------------------------------------------------------------------------------
