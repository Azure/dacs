//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
// This file implements the LoggingApi
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "FileLogger.h"
#include <unistd.h>
//-------------------------------------------------------------------------------------------------------------------------------------------
FileLogger::FileLogger(std::string outputFileName, std::string errorFileName, /*[in]*/ bool bIgnoreRankInFileName)
{
    FormatFileName(outputFileName, bIgnoreRankInFileName);
    FormatFileName(errorFileName, bIgnoreRankInFileName);

    m_outputStream.open(outputFileName, std::ios_base::app);
    if (m_outputStream.fail())
    {
      throw std::iostream::failure("Cannot open file: " + outputFileName);
    }

    m_errStream.open(errorFileName, std::ios_base::app);
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
void FileLogger::FormatFileName(/*[inout]*/ std::string& strFileName, /*[in]*/ bool bIgnoreRankInFileName)
{
    const std::string strRankMarker = "{{RANK}}";
    std::size_t nRankMarkerPosition = strFileName.find(strRankMarker);
    if (nRankMarkerPosition != std::string::npos)
    {
        if(!bIgnoreRankInFileName)
        {
            char* rank = std::getenv("RANK");
            if(rank == nullptr)
            {
                strFileName.replace(nRankMarkerPosition, strRankMarker.length(), std::to_string(::getpid()));
            }
            else
            {
                strFileName.replace(nRankMarkerPosition, strRankMarker.length(), rank);
            }
        }
        else
        {
            strFileName.replace(nRankMarkerPosition, strRankMarker.length(), "");
        }
    }
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
void FileLogger::FlushCachedLogInfo()
{
}
//-------------------------------------------------------------------------------------------------------------------------------------------