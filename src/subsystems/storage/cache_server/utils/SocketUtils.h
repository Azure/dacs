///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <stdint.h>
#include "cache.pb.h"
//---------------------------------------------------------------------------------------------------------------------
int ReadBytesToArray(int clientFd, char* data, int size);
uint64_t ConvertBytesToInt(char *data, int size);
void ConvertIntToBytes(uint64_t source, char* destination, int index, int size);
bool SendMessageToRemote(int clientFd, const ::google::protobuf::Message& message);
//---------------------------------------------------------------------------------------------------------------------