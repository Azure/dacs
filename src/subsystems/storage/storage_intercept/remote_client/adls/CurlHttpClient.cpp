//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CurlHttpClient.h"
#include <jsoncpp/json/json.h>
#include <sstream>
//---------------------------------------------------------------------------------------------------------------------
#define CURL_THROW_ERROR(curlCode) \
const char* errinfo = curl_easy_strerror((CURLcode) curlCode); \
LOG_ERROR("Curl error: {}", errinfo); \
throw CurlClientException("Curl Error: " + std::string(errinfo))

#define CHECK_CURL(curlCode) { if ( (CURLcode) curlCode != CURLE_OK) { CURL_THROW_ERROR(curlCode); } }
//---------------------------------------------------------------------------------------------------------------------
CurlHttpClient::CurlHttpClient(bool reuseConnection)
  : m_bReuseConnection(reuseConnection)
  , m_activeHttpHeaders()
  , m_curlHandle(nullptr)
  , m_curl(NULL)
{
  if (!m_bReuseConnection)
  {
    m_curlHandle = std::make_unique<CurlHandle>();
  }
  else
  {
    m_curlHandle = std::move(CurlConnectionManager::GetInstance().GetCurlHandle());
  }

  m_curl = m_curlHandle->pHandle;
  CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1));
  CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_USERAGENT, "libcurl-agent/1.0"));
}
//---------------------------------------------------------------------------------------------------------------------
CurlHttpClient::~CurlHttpClient()
{
  if (m_activeHttpHeaders)
  {
    /* we don't want to call the delete explictily, curl lib does it */
    curl_slist* headers = m_activeHttpHeaders.release();
    curl_slist_free_all(headers);
  }

  if (m_bReuseConnection && m_curlHandle)
  {
    CurlConnectionManager::GetInstance().ReleaseCurlHandle(std::move(m_curlHandle));
  }
}
//---------------------------------------------------------------------------------------------------------------------
Reply CurlHttpClient::Make(const Request &req)
{
  Reply reply;
  ReplyHandler replyHandler{reply, req.expectedResponseCode, req.pDataSink, m_curl};
  //curl_easy_setopt(m_curl, CURLOPT_VERBOSE, 1L);
  CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_URL, req.url.c_str()));
  CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, req.followRedirect ? 1L : 0L));
  CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_CUSTOMREQUEST, NULL));
  switch (req.type)
  {
    case Request::Type::GET:
      CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_HTTPGET, 1L));
      SetHttpHeaders(req.authToken, {"Expect:"});
      break;

    case Request::Type::PUT:
      CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_UPLOAD, 1L));
      if (req.pDataSource == nullptr)
      {
        CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_INFILESIZE, 0));
        SetHttpHeaders(req.authToken, {"Expect:", "Transfer-Encoding:"});
      }
      else
      {
        CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_INFILESIZE, -1));
        SetHttpHeaders(req.authToken, {"Expect:", "Transfer-Encoding: chunked"});
      }
      break;

    case Request::Type::POST:
      if (req.multipartForm.empty())
      { 
        throw CurlClientException("Post multipart fields not present");
      }
      else
      {
        curl_mime* multipart = curl_mime_init(m_curl);
        for (auto it = req.multipartForm.cbegin(); it != req.multipartForm.cend(); ++it)
        {
          curl_mimepart* part = curl_mime_addpart(multipart);
          curl_mime_name(part, it->first.c_str());
          curl_mime_data(part, it->second.c_str(), CURL_ZERO_TERMINATED);
        }
        CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_MIMEPOST, multipart));
        SetHttpHeaders(req.authToken, {"Expect:"});
      }
      break;

    case Request::Type::DELETE:
      CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_HTTPGET, 1L));
      CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_CUSTOMREQUEST, "DELETE"));
      SetHttpHeaders(req.authToken, {"Expect:", "Transfer-Encoding:", "Content-Length:"});
      break;
  }
  
  CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, ReplyHandler::WriteCallback));
  CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &replyHandler));
  if (req.pDataSource != nullptr)
  {
    CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_READFUNCTION, CurlHttpClient::StreamReadCallback));
    CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_READDATA, req.pDataSource));
  }

  auto curlCode = curl_easy_perform(m_curl);

  if (curlCode != CURLE_OK)
  {
    // special errors handling to catch errors in client callbacks, indicated by
    // RESPONSE_CODE_CLIENT_ERROR response code.
    if (reply.responseCode == Reply::RESPONSE_CODE_CLIENT_ERROR)
    {
      throw CurlClientException(reply.clientError);
    }
    // main error handling
    else
    {
      CHECK_CURL(curlCode);
    }
  }

  CHECK_CURL(curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &reply.responseCode));
  if (!req.followRedirect)
  {
    const char *redirectUrl;
    CHECK_CURL(curl_easy_getinfo(m_curl, CURLINFO_REDIRECT_URL, &redirectUrl));
    if (redirectUrl)
    {
      reply.redirectUrl = redirectUrl;
    }
  }

  // check remote error
  if (reply.responseCode != req.expectedResponseCode)
  {
    RemoteError remoteError;
    if (TryParseRemoteError(reply.unexpectedResponseContent, remoteError))
    {
      throw CurlClientException("remote error: " + remoteError.message);
    }
    else
    {
      std::stringstream err;
      err << "unexpected server response code: " << reply.responseCode;
      if (!reply.unexpectedResponseContent.empty())
      {
        err << " (" << reply.unexpectedResponseContent << ")";
      }
      throw CurlClientException(err.str());
    }
  }
  return reply;
}
//---------------------------------------------------------------------------------------------------------------------
void CurlHttpClient::SetHttpHeaders(
    const AdlsAuthToken& authToken,
    const std::vector<std::string> &headers)
{
  std::vector<std::string> headersIncludingAuth = headers;
  headersIncludingAuth.push_back("Authorization: " + authToken.type + " " + authToken.authToken);

  // reset headers to default ones
  CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, nullptr));

  curl_slist *headersRaw = NULL;
  for (const auto &header : headersIncludingAuth)
  {
    headersRaw = curl_slist_append(headersRaw, header.c_str());
    if (headersRaw == nullptr)
    {
      throw CurlClientException("libcurl request headers setup failed");
    }
  }
  m_activeHttpHeaders = std::move(std::unique_ptr<curl_slist>(headersRaw));
  CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, m_activeHttpHeaders.get()));
}
//---------------------------------------------------------------------------------------------------------------------
size_t CurlHttpClient::StreamReadCallback(char *buffer, size_t size, size_t nitems, void *userdata)
{
  auto pIStream = static_cast<std::istream *>(userdata);
  const auto dataSize = size * nitems;
  pIStream->read(buffer, dataSize);
  return pIStream->gcount();
}
//---------------------------------------------------------------------------------------------------------------------
bool CurlHttpClient::TryParseRemoteError(const std::string &s, RemoteError &remoteError)
{
  Json::Value remoteErrorValue;
  Json::Reader reader;
  constexpr auto collectComments = false;
  if (reader.parse(s, remoteErrorValue, collectComments) && remoteErrorValue.isMember("RemoteException"))
  {
    const auto exceptionValue = remoteErrorValue["RemoteException"];
    remoteError.type = exceptionValue.get("exception", "Unknown").asString();
    remoteError.message = exceptionValue.get("message", "").asString();
    return true;
  }
  return false;
}
//---------------------------------------------------------------------------------------------------------------------
void CurlHttpClient::SetConnectionTimeout(int seconds)
{
  CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT, seconds));
}
//---------------------------------------------------------------------------------------------------------------------
void CurlHttpClient::SetDataTransferTimeout(int seconds)
{
  CHECK_CURL(curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, seconds));
}
//---------------------------------------------------------------------------------------------------------------------
