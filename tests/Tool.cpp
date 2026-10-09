#include "Core.h"
#include <csignal>
#include <cstdlib>
#include <iostream>
int main(int argc, char** argv)
{
    signal(SIGPIPE, SIG_IGN);
    std::atomic<bool> cancel { false };
    try {
        if (argc < 3) {
            std::cerr << "Usage: smb-tool scan CIDR | probe IPv4 | list URL | get URL LOCAL | put URL "
                         "LOCAL\nCredentials: SMB_USER, SMB_PASSWORD, SMB_DOMAIN environment variables.\n";
            return 2;
        }
        std::string command = argv[1];
        if (command == "scan") {
            smb::Scan(
                { smb::ParseNetwork(argv[2]) }, cancel,
                [](const std::string& host) { std::cout << host << std::endl; }, [](uint64_t, uint64_t) { });
            return 0;
        }
        if (command == "probe")
            return smb::ProbeSMB(argv[2]) ? 0 : 1;
        auto env = [](const char* name) {
            auto* p = getenv(name);
            return std::string(p ? p : "");
        };
        auto location = smb::Location::Parse(argv[2]);
        smb::Client client(location, { env("SMB_USER"), env("SMB_PASSWORD"), env("SMB_DOMAIN") });
        if (command == "list")
            for (auto& e : client.List(location, cancel))
                std::cout << (e.directory ? "d " : "f ") << e.size << " " << e.name << "\n";
        else if (command == "get" && argc == 4)
            client.Download(location.path, argv[3], cancel, [](uint64_t, uint64_t) { });
        else if (command == "put" && argc == 4)
            client.Upload(argv[3], location.path, cancel, [](uint64_t, uint64_t) { });
        else
            return 2;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
