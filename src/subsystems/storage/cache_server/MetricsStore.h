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

#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>
#include "ResponseCode.h"
#include "cache.pb.h"

using namespace cache;

class MetricsStore
{
  public:
    std::shared_ptr<prometheus::Exposer> m_pExposer;
    std::shared_ptr<prometheus::Registry> m_spRegistry;
    std::string m_serverHostName;
    std::string m_sExposerAddress;
    prometheus::Gauge* m_pActiveConnectionsGauge;
    prometheus::Gauge* m_pCacheSizeGauge;
    std::vector<prometheus::Histogram*> m_vecSuccessfulRequestLatencies;
    std::vector<std::vector<prometheus::Counter*>> m_vecRequestCounter;
    prometheus::Histogram* m_pDownloadRequestSizeHistogram;
    prometheus::Histogram* m_pUploadRequestSizeHistogram;
    constexpr static std::array<const char*, (int)Request::TypeCase::kEndmarker> m_arrRequestTypes = {
        "Unknown",
        "Lock",
        "Upload",
        "Download",
        "Reserved4",
        "Reserved5",
        "Delete",
        "Ibinit",
        "Getcachelastaccesstimedistributionrequest",
        "Getcacheservers",
    };
    constexpr static std::array<const char*, (int)ResponseCode::END_MARKER> m_arrResponseCodes = {
      "Success",
      "InvalidTransition",
      "BadRequest",
      "InternalError",
      "NetworkError",
      "NotSet"
  };
    static MetricsStore s_instance;
};