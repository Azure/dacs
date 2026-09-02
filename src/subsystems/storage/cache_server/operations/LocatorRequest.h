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
#include "cache.pb.h"
#include "IConnection.h"
//---------------------------------------------------------------------------------------------------------------------
bool HandleLocatorRegister(IConnection& clientConnection, const cache::LocatorRegisterRequest& request);
bool HandleLocatorLookup(IConnection& clientConnection, const cache::LocatorLookupRequest& request);
bool HandleLocatorRefresh(IConnection& clientConnection, const cache::LocatorRefreshRequest& request);
bool HandleLocatorUnregister(IConnection& clientConnection, const cache::LocatorUnregisterRequest& request);
bool HandleLocatorPrefixLookup(IConnection& clientConnection,
                               const cache::LocatorPrefixLookupRequest& request);
bool HandleLocatorRegisterChain(IConnection& clientConnection,
                                const cache::LocatorRegisterChainRequest& request);
//---------------------------------------------------------------------------------------------------------------------
