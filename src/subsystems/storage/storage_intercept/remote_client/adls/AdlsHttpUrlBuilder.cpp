///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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