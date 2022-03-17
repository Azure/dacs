//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "ILogger.h"

class StandardLogger: public ILogger
{
public:
    StandardLogger();

    void Shutdown();
    void Flush();
    void LogOutput(std::string logContent);
    void LogError(std::string logContent);
};