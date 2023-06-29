//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "ErrorHandlerTests.h"
#include "ErrorHandler.h"

#include <thread>
#include <chrono>
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
    std::string lastErrorMessage;
    GetErrorResponse getErrResponse = ErrorHandler::GetInstance().GetLastError(lastErrorMessage);
    ASSERT_EQ(GetErrorResponse::NotFound, getErrResponse);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ErrorHandlerTests, TestResetState)
{
    ErrorHandler::GetInstance().ResetState();
    std::string errMessage = "Sample error";
    SetErrorResponse setErrResponse = ErrorHandler::GetInstance().SetError(errMessage);
    ASSERT_EQ(SetErrorResponse::Success, setErrResponse);

    std::string secondErrMessage = "different error";
    setErrResponse = ErrorHandler::GetInstance().SetError(secondErrMessage);
    ASSERT_EQ(SetErrorResponse::ErrorAlreadySet, setErrResponse);

    ErrorHandler::GetInstance().ResetState();
    setErrResponse = ErrorHandler::GetInstance().SetError(secondErrMessage);
    ASSERT_EQ(SetErrorResponse::Success, setErrResponse);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ErrorHandlerTests, TestSetErrorFailed)
{
    ErrorHandler::GetInstance().ResetState();
    std::string errMessage;
    SetErrorResponse setErrResponse = ErrorHandler::GetInstance().SetError(errMessage);
    ASSERT_EQ(SetErrorResponse::Failed, setErrResponse);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ErrorHandlerTests, TestSetAndGetErrorSingleThreaded)
{
    ErrorHandler::GetInstance().ResetState();
    std::string errMessage = "Sample error";
    SetErrorResponse setErrResponse = ErrorHandler::GetInstance().SetError(errMessage);
    ASSERT_EQ(SetErrorResponse::Success, setErrResponse);

    std::string lastErrMessage;
    GetErrorResponse getErrResponse = ErrorHandler::GetInstance().GetLastError(lastErrMessage);
    ASSERT_EQ(GetErrorResponse::Found, getErrResponse);
    ASSERT_EQ(errMessage, lastErrMessage);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ErrorHandlerTests, TestSetAndGetErrorMultiThreaded)
{
    ErrorHandler::GetInstance().ResetState();
    std::string errMessage = "Sample error";
    std::string secondErrMessage = "different error";

    std::thread t1([&](){
        SetErrorResponse setErrResponse = ErrorHandler::GetInstance().SetError(errMessage);
        ASSERT_EQ(SetErrorResponse::Success, setErrResponse);

        setErrResponse = ErrorHandler::GetInstance().SetError(secondErrMessage);
        ASSERT_EQ(SetErrorResponse::ErrorAlreadySet, setErrResponse);

        std::string lastErrMessage;
        GetErrorResponse getErrResponse = ErrorHandler::GetInstance().GetLastError(lastErrMessage);
        ASSERT_EQ(GetErrorResponse::Found, getErrResponse);
        ASSERT_EQ(errMessage, lastErrMessage);
    });

    std::thread t2([&](){
        // Sleep for a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        SetErrorResponse setErrResponse = ErrorHandler::GetInstance().SetError(secondErrMessage);
        ASSERT_EQ(SetErrorResponse::Success, setErrResponse);

        setErrResponse = ErrorHandler::GetInstance().SetError(errMessage);
        ASSERT_EQ(SetErrorResponse::ErrorAlreadySet, setErrResponse);

        std::string lastErrMessage;
        GetErrorResponse getErrResponse = ErrorHandler::GetInstance().GetLastError(lastErrMessage);
        ASSERT_EQ(GetErrorResponse::Found, getErrResponse);
        ASSERT_EQ(secondErrMessage, lastErrMessage);
    });

    t1.join();
    t2.join();

    std::string lastErrMessage;
    GetErrorResponse getErrResponse = ErrorHandler::GetInstance().GetLastError(lastErrMessage);
    ASSERT_EQ(GetErrorResponse::NotFound, getErrResponse);
    ASSERT_TRUE(lastErrMessage.empty());
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ErrorHandlerTests, TestIsErrorSet)
{
    ErrorHandler::GetInstance().ResetState();
    std::string errMessage = "Sample error";
    SetErrorResponse setErrResponse = ErrorHandler::GetInstance().SetError(errMessage);
    ASSERT_EQ(SetErrorResponse::Success, setErrResponse);

    bool isErrorSet = ErrorHandler::GetInstance().IsErrorSet();
    ASSERT_TRUE(isErrorSet);

    ErrorHandler::GetInstance().ResetState();
    isErrorSet = ErrorHandler::GetInstance().IsErrorSet();
    ASSERT_FALSE(isErrorSet);
}
//---------------------------------------------------------------------------------------------------------------------