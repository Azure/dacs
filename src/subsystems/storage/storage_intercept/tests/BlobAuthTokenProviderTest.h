// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

#include "TestBase.h"
#include <string>
 
 class BlobAuthTokenProviderTest : public TestBase
 {
  public:
    BlobAuthTokenProviderTest();
    ~BlobAuthTokenProviderTest() override = default;

    void SetUp() override;
    void TearDown() override;
 };