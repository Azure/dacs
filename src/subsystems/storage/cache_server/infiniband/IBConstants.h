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

// Baseline capacity of the IB completion queue.
constexpr int MAX_REQUESTS = 1024;

// Default number of receive buffers, which limits concurrent incoming requests.
constexpr uint32_t DEFAULT_RECEIVE_BUFFERS = 256;
constexpr uint32_t MIN_IB_RECEIVE_BUFFERS = 1;
constexpr uint32_t MAX_IB_RECEIVE_BUFFERS = 1024;

// Default number of request workers, one thread handles one request.
constexpr uint32_t DEFAULT_IB_RECEIVE_THREADS = 128;
constexpr uint32_t MIN_IB_RECEIVE_THREADS = 1;
constexpr uint32_t MAX_IB_RECEIVE_THREADS = 2048;

// Encode the buffer index and operation type in WR IDs so failed completions can be classified without wc.opcode.
constexpr uint64_t IB_SEND_WR_ID_FLAG = 1;

constexpr bool IsValidIBBufferIndex(uint32_t bufferIndex, uint32_t receiveBufferCount)
{
	return bufferIndex < receiveBufferCount;
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