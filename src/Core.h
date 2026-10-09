#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace smb {
struct Location {
    std::string host, share, path;
    std::string URL() const;
    Location Parent() const;
    Location Child(const std::string& name) const;
    static Location Parse(const std::string& text);
};
bool SafeName(const std::string& name);
struct Entry {
    std::string name;
    bool directory = false;
    uint64_t size = 0, modified = 0;
};
struct Credentials {
    std::string user, password, domain;
};
struct Network {
    std::string label;
    uint32_t first, last;
};
std::vector<Network> LocalNetworks();
Network ParseNetwork(const std::string& cidr);
using Found = std::function<void(const std::string&)>;
using Progress = std::function<void(uint64_t, uint64_t)>;
void Scan(const std::vector<Network>& networks, std::atomic<bool>& cancel, const Found& found,
    const Progress& progress);
bool IsSMBNegotiateResponse(const uint8_t* bytes, size_t length);
bool ProbeSMB(const std::string& host, uint16_t port = 445, int timeoutMs = 650);
// Haiku has no mDNS resolver, so a Mac's "name.local" would go to unicast DNS.
// Returns the IPv4 address from an mDNS query, or the host unchanged.
std::string ResolveHost(const std::string& host, int timeoutMs = 1500);
// Name of a discovered IPv4 host, as Finder would show it: "name.local" from an mDNS
// reverse lookup (Macs, Linux), else the NetBIOS name (Windows). Returns the address
// itself when neither answers. Names found here also resolve through ResolveHost.
std::string HostName(const std::string& address);
// Opens an SMB2 session (a libsmb2 smb2_context*) on host/share. Tries NTLM, then
// Kerberos for Mac accounts without "Windows File Sharing". On failure returns
// nullptr with a negative errno in status and the server's message in error.
void* Connect(const std::string& host, const std::string& share, const Credentials& auth, int& status,
    std::string& error);
class Client {
public:
    Client(const Location&, const Credentials&);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    std::vector<Entry> List(const Location&, std::atomic<bool>& cancel);
    void Download(const std::string& remote, const std::string& local, std::atomic<bool>& cancel,
        const Progress& progress);
    void Upload(const std::string& local, const std::string& remote, std::atomic<bool>& cancel,
        const Progress& progress);

private:
    void* context = nullptr;
};
}
