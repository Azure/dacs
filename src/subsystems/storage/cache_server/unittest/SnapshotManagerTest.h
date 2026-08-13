// Copyright (c) Microsoft Corporation.
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