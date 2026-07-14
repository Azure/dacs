//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
// This class is a wrapper for C lib IO operations. These wrappers ensure that operations complete, instead of 
// forcing programmers to add while loops
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include <map>
//---------------------------------------------------------------------------------------------------------------------
class StorageInterceptUtils
{
    public:
        /**
         * Helper to combine two file paths
         */
        static std::string CombinePath(
            /*[in]*/ const std::string& basePath,
            /*[in]*/ const std::string& relativePath);

        /**
         * Get the relative path for path if it lies under basePath
         *
         * Returns true if path lies under base path, false otherwise.
         */
        static bool GetRelativePath(
            /*[in] */ const std::string& basePath,
            /*[in] */ const char* path,
            /*[out]*/ std::string& outRelativePath);

        static std::string GetFirstComponentFromPath(/*[in] */ const std::string& path);

        static std::string RemoveFirstComponentFromPath(/*[in] */ const std::string& path);

            
       /**
        * Get the prefix needed to convert a partial remote path to fully qualified remote path which would
        * make it unique across mounts. Example: prefix <accountName>/<containerName>/ to blobName
        *
        * Note: The prefix returned will have trailing slash.
        */
        static std::string GetRemotePathPrefix(const std::string& mountName);

        /**
         * Generate a random UUID string for versioning.
         */
        static std::string GenerateUUID();

    private:
        static const char SEPARATOR_CHAR = '/';
};
//---------------------------------------------------------------------------------------------------------------------