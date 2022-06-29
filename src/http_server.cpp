#include "http_server.h"

#include <shlwapi.h>
#include <spdlog/fmt/xchar.h>
#include <spdlog/spdlog.h>

#include <iostream>

namespace http {
static int constexpr g_okCode = 200;
static char g_okReason[] = "OK";

static int constexpr g_badRequestCode = 400;
static char g_badRequestReason[] = "Bad Request";
static char g_badRequestMessage[] = "Bad request";

static int constexpr g_fileNotFoundCode = 404;
static char g_fileNotFoundReason[] = "Not Found";
static char g_fileNotFoundMessage[] = "File not found";
static char g_fileNotAccessibleMessage[] = "File could not be opened";

static int constexpr g_notImplementedCode = 501;
static char g_notImplementedReason[] = "Not Implemented";
static char g_notImplementedMessage[] = "Server only supports GET";

static int constexpr g_entityTooLargeCode = 413;
static char g_entityTooLargeReason[] = "Request Entity Too Large";
static char g_entityTooLargeMessage[] = "Large buffer support is not implemented";

//
// Routine Description:
//
//     The callback function to be called each time an overlapped I/O operation
//     completes on the file. This callback is invoked by the system threadpool.
//     Calls the corresponding I/O completion function.
//
//
// Arguments:
//
//     Instance - Ignored.
//
//     pContext - Ignored.
//
//     Overlapped  - A pointer to a variable that receives the address of the
//                   OVERLAPPED structure that was specified when the
//                   completed I/O operation was started.
//
//     IoResult - The result of the I/O operation. If the I/O is successful,
//                this parameter is NO_ERROR. Otherwise, this parameter is
//                one of the system error codes.
//
//     NumberOfBytesTransferred - Ignored.
//
//     Io - A TP_IO structure that defines the I/O completion object that
//          generated the callback.
//
// Return Value:
//
//     N/A
//

static void CALLBACK IoCompletionCallback(PTP_CALLBACK_INSTANCE instance, PVOID context, PVOID overlapped, ULONG ioResult, ULONG_PTR numberOfBytesTransferred,
                                          PTP_IO io) {
  spdlog::debug("IoCompletionCallback");

  auto const httpIoContext = CONTAINING_RECORD(overlapped, IoContext, m_overlapped);

  httpIoContext->m_completionFunction(httpIoContext, io, ioResult);
}

//
// Routine Description:
//
//     Initializes the Url and server directory using command line parameters,
//     accesses the HTTP Server API driver, creates a server session, creates
//     a Url Group under the specified server session, adds the specified Url to
//     the Url Group.
//
//
// Arguments:
//     pwszUrlPathToListenFor - URL path the user wants this sample to listen
//                              on.
//
//     pwszRootDirectory - Root directory on this host to which we will map
//                         incoming URLs
//
// Return Value:
//
//     true, if http server was initialized successfully,
//     otherwise returns false.
//

bool ServerContext::Start() {
  m_rootDirectory = std::filesystem::current_path();

  GUID guid;
  if (SUCCEEDED(CoCreateGuid(&guid))) {
    m_state = fmt::format("{:0>8x}{:0>4x}{:0>4x}{:0>2x}{:0>2x}{:0>2x}{:0>2x}{:0>2x}{:0>2x}{:0>2x}{:0>2x}", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0],
                          guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

    m_stateW = fmt::format(L"{:0>8x}{:0>4x}{:0>4x}{:0>2x}{:0>2x}{:0>2x}{:0>2x}{:0>2x}{:0>2x}{:0>2x}{:0>2x}", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0],
                           guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
  }

  ULONG errCode = HttpInitialize(HTTPAPI_VERSION_2, HTTP_INITIALIZE_SERVER, nullptr);

  if (errCode == NO_ERROR) {
    errCode = HttpCreateServerSession(HTTPAPI_VERSION_2, &m_sessionId, 0);

    if (errCode == NO_ERROR) {
      errCode = HttpCreateUrlGroup(m_sessionId, &m_urlGroupId, 0);

      if (errCode == NO_ERROR) {
        errCode = AddUrlsToUrlGroup();

        if (errCode == NO_ERROR) {
          errCode = HttpCreateRequestQueue(HTTPAPI_VERSION_2, L"http_server", nullptr, 0, &m_requestQueue);

          if (errCode == NO_ERROR) {
            HTTP_BINDING_INFO httpBindingInfo = {0};
            httpBindingInfo.Flags.Present = 1;
            httpBindingInfo.RequestQueueHandle = m_requestQueue;

            errCode = HttpSetUrlGroupProperty(m_urlGroupId, HttpServerBindingProperty, &httpBindingInfo, sizeof(httpBindingInfo));

            if (errCode == NO_ERROR) {
              m_io = CreateThreadpoolIo(m_requestQueue, IoCompletionCallback, nullptr, nullptr);

              if (errCode == NO_ERROR) {
                bool success = true;

                for (int i = 0; i < 4; ++i) {
                  auto request = new IoRequest(this);

                  StartThreadpoolIo(m_io);

                  errCode = HttpReceiveHttpRequest(m_requestQueue, HTTP_NULL_ID, HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY, request->m_httpRequest,
                                                   sizeof(request->m_requestBuffer), nullptr, &request->m_ioContext.m_overlapped);

                  if (errCode != ERROR_IO_PENDING && errCode != NO_ERROR) {
                    CancelThreadpoolIo(m_io);

                    if (errCode == ERROR_MORE_DATA) {
                      ProcessReceiveAndPostResponse(request, m_io, ERROR_MORE_DATA);
                    }

                    delete request;

                    spdlog::error("HttpReceiveHttpRequest failed with code {}", errCode);

                    success = false;

                    break;
                  }
                }

                if (success) {
                  return true;
                }

                errCode = HttpShutdownRequestQueue(m_requestQueue);

                if (errCode != NO_ERROR) {
                  spdlog::error("HttpShutdownRequestQueue failed with code {}", errCode);
                }

                // This call will block until all IO complete their callbacks.
                WaitForThreadpoolIoCallbacks(m_io, FALSE);

                CloseThreadpoolIo(m_io);
                m_io = nullptr;
              } else {
                spdlog::error("CreateThreadpoolIo failed with code {}", errCode);
              }
            } else {
              spdlog::error("HttpSetUrlGroupProperty failed with code {}", errCode);
            }

            errCode = HttpCloseRequestQueue(m_requestQueue);

            if (errCode != NO_ERROR) {
              spdlog::error("HttpCloseRequestQueue failed with code {}", errCode);
            }

            m_requestQueue = nullptr;
          } else {
            spdlog::error("HttpCreateRequestQueue failed with code {}", errCode);
          }
        } else {
          spdlog::error("HttpAddUrlToUrlGroup failed with code {}", errCode);
        }

        errCode = HttpCloseUrlGroup(m_urlGroupId);

        if (errCode != NO_ERROR) {
          spdlog::error("HttpCloseUrlGroup failed with code {}", errCode);
        }

        m_urlGroupId = 0;
      } else {
        spdlog::error("HttpCreateUrlGroup failed with code {}", errCode);
      }

      errCode = HttpCloseServerSession(m_sessionId);

      if (errCode != NO_ERROR) {
        spdlog::error("HttpCloseServerSession failed with code {}", errCode);
      }

      m_sessionId = 0;
    } else {
      spdlog::error("HttpCreateServerSession failed with code {}", errCode);
    }

    errCode = HttpTerminate(HTTP_INITIALIZE_SERVER, nullptr);

    if (errCode != NO_ERROR) {
      spdlog::error("HttpTerminate failed with code {}", errCode);
    }
  } else {
    spdlog::error("HttpInitialize failed with code {}", errCode);
  }

  m_rootDirectory.clear();

  return false;
}

ULONG ServerContext::AddUrlsToUrlGroup() {
  ULONG errCode = NO_ERROR;

  for (int port = 49215; port <= 65535; port++) {
    std::wstring const url = fmt::format(L"http://localhost:{}/", port);

    errCode = HttpAddUrlToUrlGroup(m_urlGroupId, url.c_str(), HTTP_URL_CONTEXT(0), 0);

    if (errCode == NO_ERROR) {
      m_listeningPort = port;
      m_callbackUrl = fmt::format(L"http://localhost:{}/auth/code", port);

      return errCode;
    }
  }

  return errCode;
}

void ServerContext::Stop() {
  m_stopServer = true;

  ULONG errCode = HttpShutdownRequestQueue(m_requestQueue);

  if (errCode != NO_ERROR) {
    spdlog::error("HttpShutdownRequestQueue failed with code {}", errCode);
  }

  // This call will block until all IO complete their callbacks.
  WaitForThreadpoolIoCallbacks(m_io, FALSE);

  CloseThreadpoolIo(m_io);

  m_io = nullptr;

  errCode = HttpCloseRequestQueue(m_requestQueue);

  if (errCode != NO_ERROR) {
    spdlog::error("HttpCloseRequestQueue failed with code {}", errCode);
  }

  m_requestQueue = nullptr;

  errCode = HttpCloseUrlGroup(m_urlGroupId);

  if (errCode != NO_ERROR) {
    spdlog::error("HttpCloseUrlGroup failed with code {}", errCode);
  }

  m_urlGroupId = 0;

  errCode = HttpCloseServerSession(m_sessionId);

  if (errCode != NO_ERROR) {
    spdlog::error("HttpCloseServerSession failed with code {}", errCode);
  }

  m_sessionId = 0;

  errCode = HttpTerminate(HTTP_INITIALIZE_SERVER, nullptr);

  if (errCode != NO_ERROR) {
    spdlog::error("HttpTerminate failed with code {}", errCode);
  }

  m_rootDirectory.clear();
}

//
// Routine Description:
//
//     Creates an http response if the requested file was not found.
//
// Arguments:
//
//     pServerContext - Pointer to the http server context structure.
//
//     code - The error code to use in the response
//
//     pReason - The reason string to send back to the client
//
//     pMessage - The more verbose message to send back to the client
//
// Return Value:
//
//     Return a pointer to the HTTP_IO_RESPONSE structure
//

IoResponse* ServerContext::CreateMessageResponse(USHORT code, char* reason, char* message) {
  auto const response = new IoResponse(this);

  // Can not find the requested file
  response->m_httpResponse.StatusCode = code;
  response->m_httpResponse.pReason = reason;
  response->m_httpResponse.ReasonLength = (USHORT)strlen(reason);

  auto const chunk = &response->m_httpResponse.pEntityChunks[0];
  chunk->DataChunkType = HttpDataChunkFromMemory;
  chunk->FromMemory.pBuffer = message;
  chunk->FromMemory.BufferLength = (ULONG)strlen(message);

  return response;
}

//
// Routine Description:
//
//     Creates a response for a successful get, the content is served
//     from a file.
//
// Arguments:
//
//     pServerContext - Pointer to the http server context structure.
//
//     hFile - Handle to the specified file.
//
// Return Value:
//
//     Return a pointer to the HTTP_IO_RESPONSE structure.
//

IoResponse* ServerContext::CreateFileResponse(HANDLE file) {
  auto const response = new IoResponse(this);

  response->m_httpResponse.StatusCode = g_okCode;
  response->m_httpResponse.pReason = g_okReason;
  response->m_httpResponse.ReasonLength = (USHORT)strlen(g_okReason);

  auto const chunk = &response->m_httpResponse.pEntityChunks[0];
  chunk->DataChunkType = HttpDataChunkFromFileHandle;
  chunk->FromFileHandle.ByteRange.Length.QuadPart = HTTP_BYTE_RANGE_TO_EOF;
  chunk->FromFileHandle.ByteRange.StartingOffset.QuadPart = 0;
  chunk->FromFileHandle.FileHandle = file;

  return response;
}

IoResponse* ServerContext::CreateRedirectResponse(char* redirectTo) {
  auto const response = new IoResponse(this);

  response->m_httpResponse.StatusCode = 302;
  response->m_httpResponse.pReason = "Found";
  response->m_httpResponse.ReasonLength = (USHORT)strlen(response->m_httpResponse.pReason);

  response->m_httpResponse.EntityChunkCount = 0;
  response->m_httpResponse.pEntityChunks = nullptr;

  auto const contentTypeHeader = &response->m_httpResponse.Headers.KnownHeaders[HttpHeaderContentType];
  contentTypeHeader->pRawValue = nullptr;
  contentTypeHeader->RawValueLength = 0;

  auto const locationHeader = &response->m_httpResponse.Headers.KnownHeaders[HttpHeaderLocation];
  locationHeader->pRawValue = redirectTo;
  locationHeader->RawValueLength = (USHORT)strlen(locationHeader->pRawValue);

  return response;
}

//
// Routine Description:
//
//     Retrieves the next available HTTP request from the specified request
//     queue asynchronously. If HttpReceiveHttpRequest call failed inline checks
//     the reason and cancels the Io if necessary. If our attempt to receive
//     an HTTP Request failed with ERROR_MORE_DATA the client is misbehaving
//     and we should return it error 400 back. Pretend that the call
//     failed asynchronously.
//
// Arguments:
//
//     pServerContext - context for the server
//
//     Io - Structure that defines the I/O object.
//
// Return Value:
//
//     N/A
//

void ServerContext::PostNewReceive(PTP_IO io) {
  auto const request = new IoRequest(this);

  StartThreadpoolIo(io);

  ULONG errCode = HttpReceiveHttpRequest(m_requestQueue, HTTP_NULL_ID, HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY, request->m_httpRequest,
                                         sizeof(request->m_requestBuffer), nullptr, &request->m_ioContext.m_overlapped);

  if (errCode != ERROR_IO_PENDING && errCode != NO_ERROR) {
    CancelThreadpoolIo(io);

    spdlog::error("HttpReceiveHttpRequest failed with code {}", errCode);

    if (errCode == ERROR_MORE_DATA) {
      ProcessReceiveAndPostResponse(request, io, ERROR_MORE_DATA);
    }

    delete request;
  }
}

char const* QueryStringNext(char const* qs, char const** name, int* nameLength, char const** value, int* valueLength) {
  if (!qs || !*qs) {
    *name = nullptr;
    *nameLength = 0;
    *value = nullptr;
    *valueLength = 0;

    return nullptr;
  }

  char const* cursor = qs;

  // Le nom

  while (*cursor != '=') {
    if (*cursor == '\0' || *cursor == '&') {
      *name = qs;
      *nameLength = cursor - qs;

      *value = "";
      *valueLength = 0;

      return cursor;
    }

    ++cursor;
  }

  *name = qs;
  *nameLength = cursor - qs;

  *value = ++cursor;

  while (*cursor != '\0' && *cursor != '&') {
    ++cursor;
  }

  *valueLength = cursor - *value;

  if (*cursor == '\0') {
    return cursor;
  }

  return cursor + 1;
}

//
// Routine Description:
//
//     This routine processes the received request, builds an HTTP response,
//     and sends it using HttpSendHttpResponse.
//
// Arguments:
//
//     IoContext - The HTTP_IO_CONTEXT tracking this operation.
//
//     Io - Structure that defines the I/O object.
//
//     IoResult - The result of the I/O operation. If the I/O is successful,
//         this parameter is NO_ERROR. Otherwise, this parameter is one of
//         the system error codes.
//
// Return Value:
//
//     N/A
//

void ServerContext::ProcessReceiveAndPostResponse(IoRequest* request, PTP_IO io, ULONG result) {
  IoResponse* response = nullptr;

  std::string redirectUrl;
  bool shutdown = false;

  switch (result) {
    case NO_ERROR: {
      if (request->m_httpRequest->Verb != HttpVerbGET) {
        response = CreateMessageResponse(g_notImplementedCode, g_notImplementedReason, g_notImplementedMessage);
        break;
      }

      std::string_view path;
      std::string code, state;

      if (char const* questionMark = strchr(request->m_httpRequest->pRawUrl, '?')) {
        path = std::string_view(request->m_httpRequest->pRawUrl, questionMark - request->m_httpRequest->pRawUrl);

        char const* name = nullptr;
        int nameLength = 0;
        char const* value = nullptr;
        int valueLength = 0;

        char const* cursor = questionMark + 1;

        while (cursor = QueryStringNext(cursor, &name, &nameLength, &value, &valueLength)) {
          std::cout << "# " << std::string_view(name, nameLength) << ": " << std::string_view(value, valueLength) << std::endl;

          char unescaped[2048];
          DWORD unescapedLength = sizeof(unescaped);

          HRESULT errCode = UrlUnescapeA((char*)std::string(value, valueLength).c_str(), unescaped, &unescapedLength, 0);

          if (SUCCEEDED(errCode)) {
            if (!strncmp("code", name, nameLength)) {
              code = std::string_view(unescaped, unescapedLength);
            } else if (!strncmp("state", name, nameLength)) {
              state = std::string_view(unescaped, unescapedLength);
            }
          }
        }
      } else {
        path = request->m_httpRequest->pRawUrl;
      }

      if (!path.compare("/auth/code")) {
        if (m_state == state) {
          m_code = code;

          redirectUrl = fmt::format("http://localhost:{}/shutdown", m_listeningPort);

          response = CreateRedirectResponse((char*)redirectUrl.c_str());
        } else {
          response = CreateMessageResponse(g_badRequestCode, g_badRequestReason, g_badRequestMessage);
        }
      } else if (!path.compare("/shutdown")) {
        shutdown = true;

        response = CreateMessageResponse(g_okCode, g_okReason, g_okReason);
      }

      break;
    }

    case ERROR_MORE_DATA: {
      response = CreateMessageResponse(g_entityTooLargeCode, g_entityTooLargeReason, g_entityTooLargeMessage);
      break;
    }

    default:
      // If the HttpReceiveHttpRequest call failed asynchronously
      // with a different error than ERROR_MORE_DATA, the error is fatal
      // There's nothing this function can do
      return;
  }

  if (!response) {
    return;
  }

  StartThreadpoolIo(m_io);

  ULONG errCode = HttpSendHttpResponse(m_requestQueue, request->m_httpRequest->RequestId, 0, &response->m_httpResponse, nullptr, nullptr, nullptr, 0,
                                       &response->m_ioContext.m_overlapped, nullptr);

  if (errCode != NO_ERROR && errCode != ERROR_IO_PENDING) {
    CancelThreadpoolIo(m_io);

    spdlog::error("HttpSendHttpResponse failed with code {}", errCode);

    delete response;
  }

  m_shutdown = shutdown;
}

IoContext::IoContext(ServerContext* serverContext, CompletionFunction completionFunction)
    : m_serverContext(serverContext)
    , m_completionFunction(completionFunction) {
}

//
// Routine Description:
//
//     Completion routine for the asynchronous HttpReceiveHttpRequest
//     call. Check if the user asked us to stop the server. If not, send a
//     response and post a new receive to HTTPAPI.
//
// Arguments:
//
//     IoContext - The HTTP_IO_CONTEXT tracking this operation.
//
//     Io - Structure that defines the I/O object.
//
//     IoResult - The result of the I/O operation. If the I/O is successful,
//         this parameter is NO_ERROR. Otherwise, this parameter is one of
//         the system error codes.
//
// Return Value:
//
//     N/A
//

static void ReceiveCompletionCallback(IoContext* context, PTP_IO io, ULONG result) {
  spdlog::debug("ReceiveCompletionCallback");

  auto const request = CONTAINING_RECORD(context, IoRequest, m_ioContext);

  auto const serverContext = request->m_ioContext.m_serverContext;

  if (!serverContext->m_stopServer) {
    serverContext->ProcessReceiveAndPostResponse(request, io, result);

    serverContext->PostNewReceive(io);
  }

  delete request;
}

//
// Routine Description:
//
//     Allocates an HTTP_IO_REQUEST block, initializes some members
//
// Arguments:
//
//     pServerContext - Pointer to the http server context structure.
//
// Return Value:
//
//     Returns a pointer to the newly initialized HTTP_IO_REQUEST.
//     NULL upon failure.
//

IoRequest::IoRequest(ServerContext* serverContext)
    : m_ioContext(serverContext, ReceiveCompletionCallback) {
  m_httpRequest = (HTTP_REQUEST*)m_requestBuffer;
}

//
// Routine Description:
//
//     Completion routine for the asynchronous HttpSendHttpResponse
//     call. This sample doesn't process the results of its send operations.
//
// Arguments:
//
//     IoContext - The HTTP_IO_CONTEXT tracking this operation.
//
//     Io - Ignored
//
//     IoResult - Ignored
//
// Return Value:
//
//     N/A
//

static void SendCompletionCallback(IoContext* context, PTP_IO io, ULONG result) {
  spdlog::debug("SendCompletionCallback");

  auto const response = CONTAINING_RECORD(context, IoResponse, m_ioContext);

  delete response;
}

//
// Routine Description:
//
//     Allocates an HTTP_IO_RESPONSE block, setups a couple HTTP_RESPONSE members
//     for the response function, gives them 1 EntityChunk, which has a default
//     buffer if needed and increments the I/O counter.
//
// Arguments:
//
//     pServerContext - Pointer to the http server context structure.
//
// Return Value:
//
//     Returns a pointer to the newly initialized HTTP_IO_RESPONSE.
//     NULL upon failure.
//

IoResponse::IoResponse(ServerContext* serverContext)
    : m_ioContext(serverContext, SendCompletionCallback) {
  memset(&m_httpResponse, 0, sizeof(HTTP_RESPONSE));
  memset(&m_httpDataChunk, 0, sizeof(HTTP_DATA_CHUNK));

  m_httpResponse.EntityChunkCount = 1;
  m_httpResponse.pEntityChunks = &m_httpDataChunk;

  auto const contentTypeHeader = &m_httpResponse.Headers.KnownHeaders[HttpHeaderContentType];
  contentTypeHeader->pRawValue = "text/html";
  contentTypeHeader->RawValueLength = (USHORT)strlen(contentTypeHeader->pRawValue);
}

//
// Routine Description:
//
//     Cleans the structure associated with the specific response.
//     Releases this structure, and decrements the I/O counter.
//
// Arguments:
//
//     pIoResponse - Pointer to the structure associated with the specific
//                   response.
//
// Return Value:
//
//     N/A
//

IoResponse::~IoResponse() {
  for (int i = 0; i < m_httpResponse.EntityChunkCount; ++i) {
    auto const dataChunk = &m_httpResponse.pEntityChunks[i];

    if (dataChunk->DataChunkType == HttpDataChunkFromFileHandle) {
      if (dataChunk->FromFileHandle.FileHandle) {
        CloseHandle(dataChunk->FromFileHandle.FileHandle);
        dataChunk->FromFileHandle.FileHandle = nullptr;
      }
    }
  }
}
}  // namespace http