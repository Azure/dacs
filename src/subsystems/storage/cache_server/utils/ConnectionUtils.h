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
bool SendMessageToRemote(IConnection& clientConnection, const ::google::protobuf::Message& message);
bool SendMessageToRemoteFinal(IConnection& clientConnection, const ::google::protobuf::Message& message);
//---------------------------------------------------------------------------------------------------------------------