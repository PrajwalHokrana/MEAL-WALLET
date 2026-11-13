#ifndef MEALWALLET_API_SERVER_HPP
#define MEALWALLET_API_SERVER_HPP

#include "MealWalletService.hpp"
#include "httplib.h"
#include <string>

namespace mealwallet {

/**
 * HTTP/JSON Web Layer (src/api/ApiServer.cpp):
 * Translates HTTP requests and nlohmann::json payloads into calls on
 * MealWalletService. Domain classes never know about HTTP or JSON.
 */
class ApiServer {
private:
    MealWalletService& service_;
    std::string webRootDir_;
    httplib::Server svr_;

    void registerRoutes();

public:
    ApiServer(MealWalletService& service, std::string webRootDir = "./web");

    bool start(const std::string& host, int port);
    void stop();
};

} // namespace mealwallet

#endif // MEALWALLET_API_SERVER_HPP
