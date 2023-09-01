///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#define SET_RESULT(responseObject, responseVariable, response) \
do { \
    responseObject.set_result(response); \
    responseVariable = TranslateResponseCode(response); \
} while (0);
//---------------------------------------------------------------------------------------------------------------------
enum class ResponseCode
{
    SUCCESS = 0,
    INVALID_TRANSITION = 1,
    BAD_REQUEST = 2,
    INTERNAL_ERROR = 3,
    NETWORK_ERROR = 4,
    NOT_SET = 5,
    END_MARKER =6
};
//----------------------------------------------------------------------------------------------------------------------