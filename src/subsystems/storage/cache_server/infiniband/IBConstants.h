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

// Maximum requests that can be added to the IB send queue.
constexpr int MAX_REQUESTS = 1024;

// Maximum number of receive buffers available for incoming requests.
// This is the maximum number of concurrent requests the server can handle.
constexpr int MAX_RECEIVE_BUFFERS = 256;

// Number of workers to handle client requests, one thread handles one request.
constexpr int IB_RECEIVE_THREADS = 128;
//---------------------------------------------------------------------------------------------------------------------