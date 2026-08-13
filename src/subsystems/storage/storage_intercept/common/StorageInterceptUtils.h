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