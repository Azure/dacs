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

#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include <vector>
#include "DirectoryEntry.h"
//---------------------------------------------------------------------------------------------------------------------
// A directory stream (Returned by opendir())
struct DirectoryStream
{
    DirectoryStream()
        : Offset(0)
        , AllEntriesFetched(false)
    {}

    // Mount name
    std::string MountName;

    // Remote path of the directory
    std::string RemotePath;

    // Read directory offset (index of the entry that needs to be returned with next readdir())
    off64_t Offset;

    // All the directory entries fetched till now.
    std::vector<DirectoryEntry> Entries;

    // Flag to indicate if all the directory entries have been fetched.
    bool AllEntriesFetched;

    // The continuation token to fetch next set of directory entries from backend.
    std::string ContinuationToken;
};
//---------------------------------------------------------------------------------------------------------------------
