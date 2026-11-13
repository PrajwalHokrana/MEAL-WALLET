#include "ApiServer.hpp"
#include "Config.hpp"
#include "ConsoleAndTests.hpp"
#include "MealWalletService.hpp"

#include <iostream>
#include <string>

using namespace mealwallet;

namespace {

void printCredentialsTable(const std::vector<MealWalletService::SeedCredential>& creds) {
    std::cout << "\n+--------------+--------------+--------------+----------------------------------+--------------------------------+\n";
    std::cout << "| ROLE         | USERNAME     | PASSWORD     | FULL NAME                        | PLAN / NOTES                   |\n";
    std::cout << "+--------------+--------------+--------------+----------------------------------+--------------------------------+\n";
    for (const auto& c : creds) {
        std::printf("| %-12s | %-12s | %-12s | %-32s | %-30s |\n",
                    c.role.c_str(), c.username.c_str(), c.password.c_str(),
                    c.fullName.c_str(), c.planInfo.c_str());
    }
    std::cout << "+--------------+--------------+--------------+----------------------------------+--------------------------------+\n\n";
}

} // anonymous namespace

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
        if (arg == "--seed-demo") {
            MealWalletService svc(Config::dataDir(), Config::repositoryMode());
            auto creds = svc.resetAndSeedDemoData();
            std::cout << "\n[MEAL WALLET] Demo data seeded into DATA_DIR='" << Config::dataDir() << "'\n";
            printCredentialsTable(creds);
            return 0;
        }
        if (arg == "--port" && i + 1 < argc) {
            Config::setServerPort(std::stoi(argv[++i]));
        }
        if (arg == "--data-dir" && i + 1 < argc) {
            Config::setDataDir(argv[++i]);
        }
    }

    MealWalletService service(Config::dataDir(), Config::repositoryMode());
    service.initialize(true);

    int port = Config::serverPort();
    std::string lanIp = httplib::Server::detectLanIp();

    std::cout << "\n========================================================================\n";
    std::cout << "  MEAL WALLET — C++17 HOSTEL MESS REFUND SYSTEM\n";
    std::cout << "========================================================================\n";
    std::cout << "  Repository Mode : " << Config::repositoryMode() << " (DATA_DIR=" << Config::dataDir() << ")\n";
    std::cout << "  Refund Policy   : " << service.refundPolicy().description() << "\n";
    std::cout << "  Cutoff Window   : " << Config::cutoffHoursBeforeMeal() << " hours before meal start\n";
    std::cout << "  Monthly Skip Cap: " << Config::monthlySkipCap() << " meals / month\n";
    std::cout << "------------------------------------------------------------------------\n";
    std::cout << "  Local URL       : http://localhost:" << port << "\n";
    std::cout << "  LAN / Hotspot   : http://" << lanIp << ":" << port << "  <-- Open on 2nd Laptop!\n";
    std::cout << "  Health Check    : http://localhost:" << port << "/healthz\n";
    std::cout << "------------------------------------------------------------------------\n";
    std::cout << "  Demo Accounts   : admin / " << Config::adminSeedPassword() << " (Admin Console)\n";
    std::cout << "                    aarav / student123 (Student - Veg Plan)\n";
    std::cout << "                    priya / student123 (Student - Non-Veg Plan)\n";
    std::cout << "                    rohan / student123 (Student - Premium, Near Month-End)\n";
    std::cout << "                    sneha / student123 (Student - Fraud Case Demo)\n";
    std::cout << "                    vikram / firstlogin (Student - Forced Password Change)\n";
    std::cout << "========================================================================\n\n";

    ApiServer server(service, "./web");
    if (!server.start("0.0.0.0", port)) {
        std::cerr << "[ERROR] Failed to bind HTTP server on 0.0.0.0:" << port << "\n";
        return 1;
    }
    return 0;
}
