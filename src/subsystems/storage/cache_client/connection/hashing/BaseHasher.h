//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
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