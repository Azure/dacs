///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <stdint.h>
#include "cache.pb.h"
#include "IConnection.h"
//---------------------------------------------------------------------------------------------------------------------
int ReadBytesToArray(IConnection& clientConnection, char* data, int size);
uint64_t ConvertBytesToInt(char *data, int size);
void ConvertIntToBytes(uint64_t source, char* destination, int index, int size);

// We pass the requestid separately since the response protobuf message has no field for requestid
// this approach is simpler, and avoids modifying the protobuf as well as templatizing the below methods
bool SendMessageToRemote(IConnection& clientConnection, const ::google::protobuf::Message& message, const std::string& requestid);
bool SendMessageToRemoteFinal(IConnection& clientConnection, const ::google::protobuf::Message& message, const std::string& requestid);
//---------------------------------------------------------------------------------------------------------------------