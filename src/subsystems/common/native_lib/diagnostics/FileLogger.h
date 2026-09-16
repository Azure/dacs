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

#include "ILogger.h"
#include <iostream>
#include <fstream>

class FileLogger: public ILogger
{
public:
    FileLogger(std::string outputFileName, std::string errorFileName, /*[in]*/ bool bIgnoreRankInFileName = false);

    ~FileLogger() override;

    void Shutdown() override;
    void Flush() override;
    void LogOutput(std::string logContent) override;
    void LogError(std::string logContent) override;
    void FlushCachedLogInfo() override;
private:
    void FormatFileName(/*[inout]*/ std::string& strFileName, /*[in]*/ bool bIgnoreRankInFileName = false);

    std::ofstream m_outputStream;
    std::ofstream m_errStream;
};