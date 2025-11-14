//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "BaseHasher.h"
//---------------------------------------------------------------------------------------------------------------------
class StaticModuloHasher : public BaseHasher {
public:
    std::string GetServer(const std::string& key) const override;
    void UpdateServers(const std::vector<std::string>& servers) override;
};
//---------------------------------------------------------------------------------------------------------------------