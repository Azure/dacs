// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <cstdint>
//---------------------------------------------------------------------------------------------------------------------

// Maximum requests that can be added to the IB send queue.
constexpr int MAX_REQUESTS = 1024;

// Maximum number of receive buffers available for incoming requests.
// This is the maximum number of concurrent requests the server can handle.
constexpr int MAX_RECEIVE_BUFFERS = 256;

// Number of workers to handle client requests, one thread handles one request.
constexpr int IB_RECEIVE_THREADS = 128;

// Encode the buffer index and operation type in WR IDs so failed completions can be classified without wc.opcode.
constexpr uint64_t IB_SEND_WR_ID_FLAG = 1;

constexpr bool IsValidIBBufferIndex(uint32_t bufferIndex)
{
	return bufferIndex < MAX_RECEIVE_BUFFERS;
}

constexpr uint64_t MakeIBReceiveWrId(uint32_t bufferIndex)
{
	return static_cast<uint64_t>(bufferIndex) << 1;
}

constexpr uint64_t MakeIBSendWrId(uint32_t bufferIndex)
{
	return MakeIBReceiveWrId(bufferIndex) | IB_SEND_WR_ID_FLAG;
}

constexpr bool IsIBSendWrId(uint64_t wrId)
{
	return (wrId & IB_SEND_WR_ID_FLAG) != 0;
}

constexpr uint32_t GetIBBufferIndex(uint64_t wrId)
{
	return static_cast<uint32_t>(wrId >> 1);
}
//---------------------------------------------------------------------------------------------------------------------