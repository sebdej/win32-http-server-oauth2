#include <spdlog/spdlog.h>

#include <iostream>

#include "http_server.h"

int wmain(int argc, wchar_t** argv) {
  spdlog::set_level(spdlog::level::debug);

  http::ServerContext serverContext;

  if (serverContext.Start()) {
    spdlog::info("HTTP server is running on port http://localhost:{}/. Press enter key to stop.", serverContext.m_listeningPort);

    std::cin.get();

    serverContext.Stop();
  }

  spdlog::info("Done.");

  return 0;
}