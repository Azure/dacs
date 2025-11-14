//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <string>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
class BaseHasher {
public:
    virtual ~BaseHasher() = default;
    virtual std::string GetServer(const std::string& key) const = 0;
    virtual void UpdateServers(const std::vector<std::string>& servers) = 0;
protected:
    std::vector<std::string> m_servers;
};
//---------------------------------------------------------------------------------------------------------------------