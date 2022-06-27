#ifndef _HTTP_SERVER_H_
#define _HTTP_SERVER_H_

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <http.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace http {
// Structure for handling I/O context parameters

class IoContext {
 public:
  typedef void (*CompletionFunction)(IoContext*, TP_IO*, ULONG);

  IoContext(class ServerContext* serverContext, CompletionFunction completionFunction);

  OVERLAPPED m_overlapped = {0};

  // Structure associated with the url and server directory
  class ServerContext* const m_serverContext;

  // Pointer to the completion function
  CompletionFunction const m_completionFunction;
};

// Structure for handling I/O context parameters

class IoRequest {
 public:
  IoRequest(class ServerContext* serverContext);

  IoContext m_ioContext;
  HTTP_REQUEST* m_httpRequest = {nullptr};
  char m_requestBuffer[4096] = {0};
};

// Structure for handling I/O context parameters

class IoResponse {
 public:
  IoResponse(class ServerContext* serverContext);
  ~IoResponse();

  IoContext m_ioContext;

  // Structure associated with the specific response
  HTTP_RESPONSE m_httpResponse;

  // Structure represents an individual block of data either in memory,
  // in a file, or in the HTTP Server API response-fragment cache.
  HTTP_DATA_CHUNK m_httpDataChunk;
};

// Structure for handling http server context data

class ServerContext {
 public:
  bool Start();
  void Stop();

  // Server directory
  std::filesystem::path m_rootDirectory;

  // Session Id
  HTTP_SERVER_SESSION_ID m_sessionId = {0};

  // URL group
  HTTP_URL_GROUP_ID m_urlGroupId = {0};

  // Request queue handle
  HANDLE m_requestQueue = {nullptr};

  // IO object
  TP_IO* m_io = {nullptr};

  // TRUE, when we receive a user command to stop the server
  bool m_stopServer = {false};

  int m_listeningPort = {0};

  std::wstring m_callbackUrl;

 public:
  IoResponse* CreateMessageResponse(USHORT code, char* reason, char* message);

  IoResponse* CreateFileResponse(HANDLE file);

  void PostNewReceive(TP_IO* io);

  void ProcessReceiveAndPostResponse(IoRequest* request, TP_IO* io, ULONG result);

protected:
  virtual ULONG AddUrlsToUrlGroup();
};
}  // namespace http

#endif