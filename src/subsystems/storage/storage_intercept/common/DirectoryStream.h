///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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
