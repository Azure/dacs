///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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