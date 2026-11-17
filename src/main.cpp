#include "ApiServer.hpp"
#include "Config.hpp"
#include "ConsoleAndTests.hpp"
#include "MealWalletService.hpp"

#include <iostream>
#include <string>

using namespace mealwallet;

int main(int argc, char* argv[]) {
    Config::loadFromEnv();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--test" || arg == "--tests") {
            return runAllAutomatedTests();
        }
        if (arg == "--console") {
            return runConsoleDemoFlow();
        }
        if (arg == "--port" && i + 1 < argc) {
            Config::setServerPort(std::stoi(argv[++i]));
        }
        if (arg == "--data-dir" && i + 1 < argc) {
            Config::setDataDir(argv[++i]);
        }
    }

    MealWalletService service(Config::dataDir(), Config::repositoryMode());
    service.initialize();

    int port = Config::serverPort();
    std::string lanIp = httplib::Server::detectLanIp();

    std::cout << "\n========================================================================\n";
    std::cout << "  MEAL WALLET — Hostel Mess Refund System\n";
    std::cout << "========================================================================\n";
    std::cout << "  Data Directory : " << Config::dataDir() << "\n";
    std::cout << "  Refund Policy  : " << service.refundPolicy().description() << "\n";
    std::cout << "  Cutoff Window  : " << Config::cutoffHoursBeforeMeal() << " hours before meal start\n";
    std::cout << "  Monthly Cap    : " << Config::monthlySkipCap() << " skips / month\n";
    std::cout << "------------------------------------------------------------------------\n";
    std::cout << "  Local URL      : http://localhost:" << port << "\n";
    std::cout << "  LAN / Hotspot  : http://" << lanIp << ":" << port << "  <-- Open on 2nd device!\n";
    std::cout << "  Health Check   : http://localhost:" << port << "/healthz\n";
    std::cout << "------------------------------------------------------------------------\n";
    std::cout << "  Admin login    : admin / admin123\n";
    std::cout << "  Mess Staff     : messstaff / staff123\n";
    std::cout << "  Student Portal : bt25cse147 / student123\n";
    std::cout << "========================================================================\n\n" << std::flush;

    ApiServer server(service, "./web");
    if (!server.start("0.0.0.0", port)) {
        std::cerr << "\n[ERROR] Failed to bind HTTP server on port " << port << "!\n";
        std::cerr << "Another instance of mealwallet.exe is already running, or port " << port << " is busy.\n";
        std::cerr << "Close any other running command windows and try again.\n\n" << std::flush;
        return 1;
    }
    return 0;
}
