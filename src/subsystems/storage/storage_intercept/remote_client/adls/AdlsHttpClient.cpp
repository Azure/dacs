/// Copyright (c) Microsoft Corporation.
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

#include "AdlsHttpClient.h"
#include "Request.h"
#include <iostream>
#include <sstream>
#include <dirent.h>
#include "AdlsHttpClientException.h"
//---------------------------------------------------------------------------------------------------------------------
#define ASSERT_PARSE(condition, message, ...) \
if (!condition) { LOG_ERROR(message); throw AdlsHttpClientException(message); }

#define TRY_EXECUTE(stmt) { try { stmt; } catch(CurlClientException e) { throw AdlsHttpClientException( e.what() ); } }
//---------------------------------------------------------------------------------------------------------------------
AdlsHttpClient::AdlsHttpClient(const std::string& host)
  : m_spHttpClient(nullptr)
  , m_spUrlBuilder(std::make_unique<AdlsHttpUrlBuilder>(host))
{
}
//---------------------------------------------------------------------------------------------------------------------
AdlsHttpClient::~AdlsHttpClient() = default;
//---------------------------------------------------------------------------------------------------------------------
AdlsHttpClient::AdlsHttpClient(AdlsHttpClient &&) = default;
//---------------------------------------------------------------------------------------------------------------------
AdlsHttpClient& AdlsHttpClient::operator=(AdlsHttpClient &&)=default;
//---------------------------------------------------------------------------------------------------------------------
void AdlsHttpClient::InitHttpClient(bool reuseConnection)
{
  m_spHttpClient = std::make_unique<CurlHttpClient>(reuseConnection);
  m_spHttpClient->SetConnectionTimeout(AdlsHttpClient::m_nConnectTimeoutSec);
  m_spHttpClient->SetDataTransferTimeout(AdlsHttpClient::m_nDataTransferTimeoutSec);
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsHttpClient::GetAuthToken(
  const std::string& tokenUrl,
  const std::string& clientId,
  const std::string& clientSecret,
  AdlsAuthToken& authToken)
{
  Request req;
  req.type = Request::Type::POST;
  req.url = tokenUrl;
  req.expectedResponseCode = 200L;
  req.followRedirect = true;
  std::ostringstream oss;
  req.pDataSink = &oss;
  req.multipartForm["grant_type"] = "client_credentials";
  req.multipartForm["resource"] = "https://management.core.windows.net/";
  req.multipartForm["client_id"] = clientId;
  req.multipartForm["client_secret"] = clientSecret;

  /* We don't want to reuse this connection, as auth request goes to a different host */
  InitHttpClient(false);
  LOG_DEBUG("Sending API request to get Adls auth token");
  TRY_EXECUTE(m_spHttpClient->Make(req));

  Json::Value authResponse;
  ASSERT_PARSE(TryParseJson(oss.str(), authResponse), "Can't parse auth token response");
  authToken.type = authResponse["token_type"].asString();
  authToken.authToken = authResponse["access_token"].asString();
  sscanf(authResponse["expires_on"].asString().c_str(), "%zu", &authToken.expiryTime);
  LOG_DEBUG("Refreshed Adls auth token from {}", tokenUrl);
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsHttpClient::ReadChunk(
  const AdlsAuthToken& authToken,
  const std::string& remoteFilePath,
  std::ostream& dataSink,
  size_t size,
  off_t offset)
{
  std::map<std::string, std::string> requestOptions;
  requestOptions["offset"] = std::to_string(offset);
  requestOptions["length"] = std::to_string(size);

  Request req;
  req.type = Request::Type::GET;
  req.url = m_spUrlBuilder->MakeUrl(remoteFilePath, "OPEN", requestOptions);
  req.authToken = authToken;
  req.followRedirect = true;
  req.pDataSink = &dataSink;
  req.expectedResponseCode = 200L;

  InitHttpClient();
  LOG_DEBUG("Sending API request to read file chunk");
  TRY_EXECUTE(m_spHttpClient->Make(req));
  LOG_DEBUG("Received read file chunk API response");
}
//---------------------------------------------------------------------------------------------------------------------
std::string AdlsHttpClient::ListDirectory(
  const AdlsAuthToken& authToken,
  const std::string& remoteDirPath,
  const std::string& continuationToken,
  std::vector<DirectoryEntry>& dirEntries)
{
  std::map<std::string, std::string> requestOptions;
  requestOptions["api-version"]= "2018-09-01"; // needed for paginated response (listAfter param)
  requestOptions["listSize"] = std::to_string(10000); // TODO: pick from constant
  if (!continuationToken.empty())
  {
    requestOptions["listAfter"] = continuationToken;
  }

  Request req;
  req.type = Request::Type::GET;
  req.url = m_spUrlBuilder->MakeUrl(remoteDirPath, "LISTSTATUS", requestOptions);
  req.authToken = authToken;
  req.followRedirect = true;
  req.expectedResponseCode = 200L;
  std::ostringstream oss;
  req.pDataSink = &oss;

  InitHttpClient();
  LOG_DEBUG("Sending API request to list directory entries");
  TRY_EXECUTE(m_spHttpClient->Make(req));

  std::string newContinuationToken = "";
  Json::Value listingValue;
  ASSERT_PARSE(TryParseJson(oss.str(), listingValue), "Can't parse list status response: ", oss.str());
  
  auto items = listingValue["FileStatuses"]["FileStatus"];
  for (auto it = items.begin(); it != items.end(); ++it)
  {
    const auto &statusValue = *it;

    dirEntries.push_back(DirectoryEntry());
    dirEntries.back().d_ino = D_INO_VAL;
    dirEntries.back().d_off = static_cast<off64_t>(dirEntries.size() - 1);
    dirEntries.back().d_reclen = sizeof(DirectoryEntry);
    dirEntries.back().d_type = statusValue["type"].asString().compare("DIRECTORY") == 0 ? DT_DIR : DT_REG;
    dirEntries.back().d_size = static_cast<off64_t>(statusValue["length"].asInt64());
    dirEntries.back().d_mtime = static_cast<time_t>(statusValue["modificationTime"].asInt64() / 1000);
    dirEntries.back().d_atime = static_cast<time_t>(statusValue["accessTime"].asInt64() / 1000);
    strncpy(dirEntries.back().d_name, statusValue["pathSuffix"].asCString(), D_NAME_LEN);
    dirEntries.back().d_version[0] = '\0';
  }
  newContinuationToken = listingValue["FileStatuses"]["continuationToken"].asString();
  LOG_DEBUG("Received the list directory API response");
  return newContinuationToken;
}
//---------------------------------------------------------------------------------------------------------------------
int AdlsHttpClient::GetFileOrDirectoryStatus(
  const AdlsAuthToken& authToken,
  const std::string& remotePath,
  FileAttributes& stats)
{
  std::map<std::string, std::string> requestOptions;
  Request req;
  req.type = Request::Type::GET;
  req.url = m_spUrlBuilder->MakeUrl(remotePath, "GETFILESTATUS", requestOptions);
  req.authToken = authToken;
  req.followRedirect = true;
  req.expectedResponseCode = 200L;
  std::ostringstream oss;
  req.pDataSink = &oss;

  /* To-do: Include the correct directory size,
  /  currently we set it to 100 as this doesn't matter to the pytorch client */
  static const int DIR_SIZE = 100;

  InitHttpClient();
  LOG_DEBUG("Sending API request to get path attributes");
  TRY_EXECUTE(m_spHttpClient->Make(req));

  Json::Value statusBody;
  ASSERT_PARSE(TryParseJson(oss.str(), statusBody), "Can't parse the getfilestatus API response: ", oss.str());
  auto statusValue = statusBody["FileStatus"];
  stats.isDir = statusValue["type"].asString().compare("DIRECTORY") == 0;
  if (stats.isDir)
  {
    stats.size = static_cast<off64_t>(DIR_SIZE);
  }
  else
  {
    stats.size = static_cast<off64_t>(statusValue["length"].asInt64());
  }
  stats.mTime.tv_sec = static_cast<time_t>(statusValue["modificationTime"].asInt64() / 1000);
  stats.aTime.tv_sec = static_cast<time_t>(statusValue["accessTime"].asInt64() / 1000);
  stats.cTime = stats.mTime;
  LOG_DEBUG("Received the get path attributes API response");
  return 0;
}
//---------------------------------------------------------------------------------------------------------------------
bool AdlsHttpClient::TryParseJson(const std::string& jsonStr, Json::Value& parsedJson)
{
  Json::Reader reader;
  constexpr auto collectComments = false;
  return reader.parse(jsonStr, parsedJson, collectComments);
}
//---------------------------------------------------------------------------------------------------------------------
