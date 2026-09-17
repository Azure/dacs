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
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
class BaseHasher {
public:
    virtual ~BaseHasher() = default;
    virtual std::string GetServer(std::string_view key) const = 0;
    virtual void UpdateServers(const std::vector<std::string>& servers) = 0;
protected:
    std::vector<std::string> m_servers;
};
//---------------------------------------------------------------------------------------------------------------------