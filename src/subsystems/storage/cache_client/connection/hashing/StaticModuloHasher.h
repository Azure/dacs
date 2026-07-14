//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
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