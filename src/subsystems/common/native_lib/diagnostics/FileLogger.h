//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "ILogger.h"
#include <iostream>
#include <fstream>

class FileLogger: public ILogger
{
public:
    FileLogger(std::string outputFileName, std::string errorFileName);

    ~FileLogger();

    void Shutdown();
    void Flush();
    void LogOutput(std::string logContent);
    void LogError(std::string logContent);
private:
    void FormatFileName(/*[inout]*/ std::string& strFileName);

    std::ofstream m_outputStream;
    std::ofstream m_errStream;
};