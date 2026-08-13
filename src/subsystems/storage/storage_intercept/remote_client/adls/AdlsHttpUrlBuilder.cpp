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

#include "AdlsHttpUrlBuilder.h"
#include <iostream>
#include <iomanip>
#include <set>
#include <sstream>
//---------------------------------------------------------------------------------------------------------------------
std::string AdlsHttpUrlBuilder::MakeUrl(const std::string& remotePath, const std::string& operation)
{
  std::stringstream oss;
  oss << m_strPrefix << UrlEncode(remotePath);
  oss << "?op=";
  oss << operation;
  return oss.str();
}
//---------------------------------------------------------------------------------------------------------------------
std::string AdlsHttpUrlBuilder::MakeUrl(
  const std::string& remotePath,
  const std::string& operation,
  const std::map<std::string, std::string>& opts)
{
  std::string url = MakeUrl(remotePath, operation);
  for (auto it = opts.cbegin(); it != opts.end(); ++it)
  {
    url += "&" + it->first + "=" + it->second;
  }
  return url;
}
//---------------------------------------------------------------------------------------------------------------------
std::string AdlsHttpUrlBuilder::UrlEncode(const std::string& value)
{
  std::ostringstream escaped;
  escaped.fill('0');
  std::set<char> acceptedChars = {'-', '_', '.', '~', '/'};
  escaped << std::hex;
  for (const auto &c : value)
  {
    // Keep alphanumeric and other accepted characters intact
    if (isalnum(c) || acceptedChars.find(c) != acceptedChars.end())
    {
      escaped << c;
    }
    else
    {
      escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
    }
  }
  return escaped.str();
}
//---------------------------------------------------------------------------------------------------------------------