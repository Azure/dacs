//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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