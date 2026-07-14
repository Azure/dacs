//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "snapshot.pb.h"
#include <iostream>
#include <signal.h>
#include<thread>
#include <experimental/filesystem>
#include <google/protobuf/util/message_differencer.h>

#include "Logger.h"
//---------------------------------------------------------------------------------------------------------------------
class SnapshotManagerTest : public ::testing::Test
{
    protected:
        SnapshotManagerTest();

        void SetUp() override;

        std::shared_ptr<snapshot::Snapshot> CreateMockSnapshot(std::size_t numEntriesPerBucket = 10);
};
//---------------------------------------------------------------------------------------------------------------------