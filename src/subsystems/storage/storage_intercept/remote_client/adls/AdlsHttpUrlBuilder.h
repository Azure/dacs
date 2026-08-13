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

#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include <map>
//---------------------------------------------------------------------------------------------------------------------
class AdlsHttpUrlBuilder
{
  public:
    AdlsHttpUrlBuilder(/*[in]*/ const std::string& host)
      : m_strPrefix(std::string("https://") + host + "/webhdfs/v1/")
    {
    }

  std::string MakeUrl(/*[in]*/ const std::string& remotePath, /*[in]*/ const std::string& operation);

  std::string MakeUrl(
    /*[in]*/ const std::string& remotePath,
    /*[in]*/ const std::string& operation,
    /*[in]*/ const std::map<std::string, std::string>& opts);

  private:
    static std::string UrlEncode(/*[in]*/ const std::string& value);

  private:
    std::string                     m_strPrefix;
};
//---------------------------------------------------------------------------------------------------------------------