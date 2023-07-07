//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "ErrorHandlerTests.h"
#include "ErrorHandler.h"

#include <thread>
#include <chrono>
#include <stdlib.h>
//---------------------------------------------------------------------------------------------------------------------
#define ASSERT_IF(condition, stmt)                                                         { if (condition) { stmt; } }
//---------------------------------------------------------------------------------------------------------------------
ErrorHandlerTests::ErrorHandlerTests()
    : TestBase()
    , m_spErrorHandler(nullptr)
{
}
//---------------------------------------------------------------------------------------------------------------------
void ErrorHandlerTests::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ErrorHandlerTests, TestGetLastErrorNotFound)
{
    ErrorHandler::GetInstance().ResetState();
    char* error = ErrorHandler::GetInstance().GetLastError();
    ASSERT_EQ(nullptr, error);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ErrorHandlerTests, TestResetState)
{
    ErrorHandler::GetInstance().ResetState();
    std::string errMessage = "Sample error";
    ErrorHandler::GetInstance().SetError(errMessage);

    ErrorHandler::GetInstance().ResetState();
    char* error = ErrorHandler::GetInstance().GetLastError();
    ASSERT_EQ(nullptr, error);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ErrorHandlerTests, TestSetAndGetErrorSingleThreaded)
{
    ErrorHandler::GetInstance().ResetState();
    std::string errMessage = "Sample error";
    ErrorHandler::GetInstance().SetError(errMessage);

    char* error = ErrorHandler::GetInstance().GetLastError();
    ASSERT_NE(nullptr, error);
    ASSERT_EQ(errMessage, error);
    ::free(error);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ErrorHandlerTests, TestSetAndGetErrorMultiThreaded)
{
    ErrorHandler::GetInstance().ResetState();
    std::string errMessage = "Sample error";
    std::string secondErrMessage = "different error";

    std::thread t1([&](){
        ErrorHandler::GetInstance().SetError(errMessage);
        ErrorHandler::GetInstance().SetError(secondErrMessage);

        char* error = ErrorHandler::GetInstance().GetLastError();
        ASSERT_NE(nullptr, error);
        ASSERT_EQ(secondErrMessage, error);
        ::free(error);
    });

    std::thread t2([&](){
        // Sleep for a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ErrorHandler::GetInstance().SetError(secondErrMessage);
        ErrorHandler::GetInstance().SetError(errMessage);

        char* error = ErrorHandler::GetInstance().GetLastError();
        ASSERT_NE(nullptr, error);
        ASSERT_EQ(errMessage, error);
        ::free(error);
    });

    t1.join();
    t2.join();

    char* error = ErrorHandler::GetInstance().GetLastError();
    ASSERT_EQ(nullptr, error);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ErrorHandlerTests, TestIsErrorSet)
{
    ErrorHandler::GetInstance().ResetState();
    std::string errMessage = "Sample error";
    ErrorHandler::GetInstance().SetError(errMessage);

    bool isErrorSet = ErrorHandler::GetInstance().IsErrorSet();
    ASSERT_TRUE(isErrorSet);

    ErrorHandler::GetInstance().ResetState();
    isErrorSet = ErrorHandler::GetInstance().IsErrorSet();
    ASSERT_FALSE(isErrorSet);
}
//---------------------------------------------------------------------------------------------------------------------