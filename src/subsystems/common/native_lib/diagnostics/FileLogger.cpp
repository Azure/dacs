//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
// This file implements the LoggingApi
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "FileLogger.h"
//-------------------------------------------------------------------------------------------------------------------------------------------
FileLogger::FileLogger(std::string outputFileName, std::string errorFileName)
{
    m_outputStream.open(outputFileName);
    if (m_outputStream.fail())
    {
      throw std::iostream::failure("Cannot open file: " + outputFileName);
    }

    m_errStream.open(errorFileName);
    if (m_errStream.fail())
    {
      throw std::iostream::failure("Cannot open file: " + errorFileName);
    }
}
//-------------------------------------------------------------------------------------------------------------------------------------------
FileLogger::~FileLogger()
{
    Shutdown();
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void FileLogger::Shutdown()
{
    Flush();

    if (m_outputStream.is_open())
    {
        m_outputStream.close();
    }
    
    if(m_errStream.is_open())
    {
        m_errStream.close();
    }
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void FileLogger::Flush()
{
    if (m_outputStream.is_open())
    {
        m_outputStream.flush();
    }
    
    if(m_errStream.is_open())
    {
        m_errStream.flush();
    }
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void FileLogger::LogOutput(std::string logContent)
{
    if (m_outputStream.is_open())
    {
        m_outputStream << logContent << std::endl;
    }
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void FileLogger::LogError(std::string logContent)
{
    if (m_errStream.is_open())
    {
        m_errStream << logContent << std::endl;
    }
}
//-------------------------------------------------------------------------------------------------------------------------------------------
