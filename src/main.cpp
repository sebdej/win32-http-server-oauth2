#include "http_server.h"

#include <objbase.h>
#include <shellapi.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
  spdlog::set_level(spdlog::level::debug);

  http::ServerContext serverContext;

  if (serverContext.Start()) {
    spdlog::info("HTTP server is running on port http://localhost:{}/. Press enter key to stop.", serverContext.m_listeningPort);

    std::string const redirectUri = fmt::format("http://localhost:{}/auth/code", serverContext.m_listeningPort);

    std::string const tokenEndpoint("https://accounts.google.com/o/oauth2/v2/auth");
    std::string const clientID(std::getenv("CLIENT_ID"));
    std::string const scope("openid%20email");

    std::string const authUrl = fmt::format("{}?client_id={}&redirect_uri={}&response_type=code&scope={}&state={}", tokenEndpoint, clientID, redirectUri, scope,
                                            serverContext.GetState());

    spdlog::info("Open browser to {} to log in", authUrl);

    ShellExecuteA(nullptr, "open", authUrl.c_str(), nullptr, nullptr, 0);

    for (int i = 0; i < 60 * 5; i++) {
      if (serverContext.GetShutdown()) {
        break;
      }

      Sleep(1000);
    }

    std::string code = serverContext.GetCode();

    if (!code.empty()) {
      spdlog::info("OAuth2 code: {}", code);
    } else {
      spdlog::error("Timeout");
    }

    serverContext.Stop();
  }

  spdlog::info("Done.");

  return 0;
}