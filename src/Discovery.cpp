#include "Core.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <ifaddrs.h>
#include <map>
#include <mutex>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <set>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
namespace smb {
static std::string IP(uint32_t ip)
{
    in_addr address;
    address.s_addr = htonl(ip);
    char text[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &address, text, sizeof(text));
    return text;
}
Network ParseNetwork(const std::string& cidr)
{
    auto slash = cidr.find('/');
    in_addr ip;
    if (slash == std::string::npos || inet_pton(AF_INET, cidr.substr(0, slash).c_str(), &ip) != 1)
        throw std::runtime_error("Enter an IPv4 CIDR such as 192.168.1.0/24.");
    std::string suffix = cidr.substr(slash + 1);
    if (suffix.empty() || suffix.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Invalid network prefix.");
    int bits = std::stoi(suffix);
    if (bits < 20 || bits > 32)
        throw std::runtime_error("Choose /20 through /32 (at most 4096 addresses).");
    uint32_t mask = bits == 32 ? 0xffffffffu : (0xffffffffu << (32 - bits));
    uint32_t first = ntohl(ip.s_addr) & mask, last = first | ~mask;
    if (bits < 31) {
        ++first;
        --last;
    }
    return { cidr, first, last };
}
std::vector<Network> LocalNetworks()
{
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0)
        throw std::runtime_error("Cannot read network interfaces.");
    std::vector<Network> result;
    std::set<uint32_t> seen;
    for (auto* p = interfaces; p; p = p->ifa_next) {
        if (!p->ifa_addr || !p->ifa_netmask || p->ifa_addr->sa_family != AF_INET || !(p->ifa_flags & IFF_UP)
            || (p->ifa_flags & IFF_LOOPBACK))
            continue;
        uint32_t ip = ntohl(((sockaddr_in*)p->ifa_addr)->sin_addr.s_addr);
        uint32_t mask = ntohl(((sockaddr_in*)p->ifa_netmask)->sin_addr.s_addr);
        // Very large LANs are bounded to the local /24; a different CIDR can be entered explicitly.
        if ((~mask) > 4095)
            mask = 0xffffff00u;
        uint32_t first = ip & mask, last = first | ~mask;
        if (!seen.insert(first).second)
            continue;
        unsigned bits = 0;
        for (uint32_t m = mask; m; m <<= 1)
            ++bits;
        auto label = IP(first) + "/" + std::to_string(bits);
        if (bits < 31) {
            ++first;
            --last;
        }
        result.push_back({ label, first, last });
    }
    freeifaddrs(interfaces);
    return result;
}
bool ProbeSMB(const std::string& host, uint16_t port, int timeoutMs)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    struct Guard {
        int fd;
        ~Guard()
        {
            close(fd);
        }
    } guard { fd };
    fcntl(fd, F_SETFL, O_NONBLOCK);
    sockaddr_in address { };
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1)
        return false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto wait = [&](short events) {
        int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
                       .count();
        if (left <= 0)
            return false;
        pollfd p { fd, events, 0 };
        return poll(&p, 1, left) > 0 && (p.revents & events);
    };
    if (connect(fd, (sockaddr*)&address, sizeof(address)) != 0 && errno != EINPROGRESS)
        return false;
    if (!wait(POLLOUT))
        return false;
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) != 0 || error)
        return false;
    // SMB2 NEGOTIATE, dialects 2.0.2, 2.1, 3.0 and 3.0.2. No login or credentials are sent during discovery.
    // Frame: 4-byte length, 64-byte header, 36-byte request body, then the dialects. macOS drops
    // requests whose dialect array is misplaced.
    unsigned char packet[112] { };
    packet[3] = 108;
    packet[4] = 0xfe;
    packet[5] = 'S';
    packet[6] = 'M';
    packet[7] = 'B';
    packet[8] = 64;
    packet[18] = 1;
    packet[68] = 36;
    packet[70] = 4;
    packet[72] = 1;
    packet[104] = 2;
    packet[105] = 2;
    packet[106] = 0x10;
    packet[107] = 2;
    packet[108] = 0;
    packet[109] = 3;
    packet[110] = 2;
    packet[111] = 3;
    size_t sent = 0;
    while (sent < sizeof(packet)) {
        if (!wait(POLLOUT))
            return false;
        ssize_t n = send(fd, packet + sent, sizeof(packet) - sent, 0);
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (n <= 0)
            return false;
        sent += n;
    }
    unsigned char reply[68] { };
    size_t got = 0;
    while (got < sizeof(reply)) {
        if (!wait(POLLIN))
            return false;
        ssize_t n = recv(fd, reply + got, sizeof(reply) - got, 0);
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (n <= 0)
            return false;
        got += n;
    }
    return IsSMBNegotiateResponse(reply, got);
}
bool IsSMBNegotiateResponse(const uint8_t* reply, size_t length)
{
    if (length < 68)
        return false;
    uint32_t frameSize = (uint32_t(reply[1]) << 16) | (uint32_t(reply[2]) << 8) | reply[3];
    return reply[0] == 0 && frameSize >= 64 && !memcmp(reply + 4, "\xfeSMB", 4) && reply[8] == 64
        && reply[9] == 0 && reply[16] == 0 && reply[17] == 0 && (reply[20] & 1) && reply[12] == 0
        && reply[13] == 0 && reply[14] == 0 && reply[15] == 0;
}
void Scan(const std::vector<Network>& networks, std::atomic<bool>& cancel, const Found& found,
    const Progress& progress)
{
    std::set<uint32_t> unique;
    for (auto& n : networks)
        for (uint64_t i = n.first; i <= n.last; ++i)
            unique.insert((uint32_t)i);
    std::vector<uint32_t> addresses(unique.begin(), unique.end());
    std::atomic<size_t> next { 0 };
    size_t done = 0;
    std::mutex callbacks;
    std::vector<std::thread> threads;
    std::atomic<bool> failed { false };
    std::exception_ptr error;
    try {
        for (size_t i = 0; i < std::min(size_t(24), addresses.size()); ++i)
            threads.emplace_back([&] {
                try {
                    while (!cancel && !failed) {
                        size_t index = next++;
                        if (index >= addresses.size())
                            break;
                        std::string host = IP(addresses[index]);
                        bool smb = ProbeSMB(host);
                        std::lock_guard<std::mutex> lock(callbacks);
                        if (smb && !cancel)
                            found(host);
                        progress(++done, addresses.size());
                    }
                } catch (...) {
                    std::lock_guard<std::mutex> lock(callbacks);
                    if (!error)
                        error = std::current_exception();
                    failed = true;
                }
            });
    } catch (...) {
        failed = true;
        for (auto& thread : threads)
            thread.join();
        throw;
    }
    for (auto& thread : threads)
        thread.join();
    if (error)
        std::rethrow_exception(error);
}
static std::string Lower(std::string s)
{
    for (auto& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}
// Reads a DNS name at offset, following compression pointers. Returns false on malformed data.
static bool ReadName(const uint8_t* m, size_t length, size_t& offset, std::string& name)
{
    size_t at = offset;
    bool jumped = false;
    for (int hops = 0; hops < 32; ++hops) {
        if (at >= length)
            return false;
        uint8_t n = m[at];
        if (n == 0) {
            if (!jumped)
                offset = at + 1;
            return true;
        }
        if ((n & 0xc0) == 0xc0) {
            if (at + 1 >= length)
                return false;
            if (!jumped)
                offset = at + 2;
            at = ((n & 0x3f) << 8) | m[at + 1];
            jumped = true;
            continue;
        }
        if (n > 63 || at + 1 + n > length)
            return false;
        if (!name.empty())
            name += '.';
        name.append((const char*)m + at + 1, n);
        at += 1 + n;
    }
    return false;
}
// Sends packet to dest, again every 500 ms, until accept() takes a reply or time runs out.
static bool Ask(const sockaddr_in& dest, const std::vector<uint8_t>& packet, int timeoutMs, bool broadcast,
    const std::function<bool(const uint8_t*, size_t)>& accept)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;
    struct Guard {
        int fd;
        ~Guard()
        {
            close(fd);
        }
    } guard { fd };
    int on = 1;
    if (broadcast)
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    unsigned char ttl = 255;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto next = std::chrono::steady_clock::now();
    for (;;) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return false;
        if (now >= next) {
            sendto(fd, packet.data(), packet.size(), 0, (const sockaddr*)&dest, sizeof(dest));
            next = now + std::chrono::milliseconds(500);
        }
        int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::min(deadline, next) - now)
                       .count();
        pollfd p { fd, POLLIN, 0 };
        if (poll(&p, 1, std::max(left, 1)) <= 0)
            continue;
        uint8_t m[1500];
        ssize_t got = recv(fd, m, sizeof(m), 0);
        if (got > 0 && accept(m, got))
            return true;
    }
}
// One-shot mDNS question (RFC 6762 5.1) with unicast response. Returns the first answer
// for the name: a dotted IPv4 address for A (1), a name for PTR (12).
static std::string MulticastQuery(const std::string& name, uint16_t type, int timeoutMs)
{
    std::vector<uint8_t> query { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 };
    size_t start = 0;
    while (start <= name.size()) {
        size_t dot = name.find('.', start);
        if (dot == std::string::npos)
            dot = name.size();
        size_t n = dot - start;
        if (n == 0 || n > 63)
            return "";
        query.push_back((uint8_t)n);
        query.insert(query.end(), name.begin() + start, name.begin() + dot);
        start = dot + 1;
    }
    query.insert(query.end(), { 0, (uint8_t)(type >> 8), (uint8_t)type, 0x80, 1 });
    sockaddr_in group { };
    group.sin_family = AF_INET;
    group.sin_port = htons(5353);
    group.sin_addr.s_addr = htonl(0xe00000fb); // 224.0.0.251
    std::string result;
    Ask(group, query, timeoutMs, false, [&](const uint8_t* m, size_t length) {
        if (length < 12 || !(m[2] & 0x80))
            return false;
        size_t offset = 12;
        unsigned questions = (m[4] << 8) | m[5];
        unsigned records = ((m[6] << 8) | m[7]) + ((m[8] << 8) | m[9]) + ((m[10] << 8) | m[11]);
        for (unsigned i = 0; i < questions; ++i) {
            std::string ignored;
            if (!ReadName(m, length, offset, ignored) || (offset += 4) > length)
                return false;
        }
        for (unsigned i = 0; i < records; ++i) {
            std::string owner;
            if (!ReadName(m, length, offset, owner) || offset + 10 > length)
                return false;
            unsigned rtype = (m[offset] << 8) | m[offset + 1];
            unsigned size = (m[offset + 8] << 8) | m[offset + 9];
            offset += 10;
            if (offset + size > length)
                return false;
            if (rtype == type && Lower(owner) == name) {
                if (type == 1 && size == 4) {
                    char text[INET_ADDRSTRLEN];
                    if (inet_ntop(AF_INET, m + offset, text, sizeof(text))) {
                        result = text;
                        return true;
                    }
                } else if (type == 12) {
                    size_t at = offset;
                    if (ReadName(m, length, at, result) && !result.empty())
                        return true;
                    result.clear();
                }
            }
            offset += size;
        }
        return false;
    });
    return result;
}
// NetBIOS first-level encoding (RFC 1001 14.1) of a 16-byte name: 0x20, 32 letters, 0.
static std::vector<uint8_t> NetBIOSName(const std::string& name, uint8_t suffix, char pad)
{
    std::string raw = name.substr(0, 15);
    for (auto& c : raw)
        c = (char)std::toupper((unsigned char)c);
    raw.resize(15, pad);
    raw += (char)suffix;
    std::vector<uint8_t> out { 32 };
    for (unsigned char c : raw) {
        out.push_back((uint8_t)('A' + (c >> 4)));
        out.push_back((uint8_t)('A' + (c & 15)));
    }
    out.push_back(0);
    return out;
}
static std::vector<uint8_t> NetBIOSPacket(uint16_t flags, const std::vector<uint8_t>& name, uint16_t type)
{
    std::vector<uint8_t> p { 0x52, 0x53, (uint8_t)(flags >> 8), (uint8_t)flags, 0, 1, 0, 0, 0, 0, 0, 0 };
    p.insert(p.end(), name.begin(), name.end());
    p.insert(p.end(), { (uint8_t)(type >> 8), (uint8_t)type, 0, 1 });
    return p;
}
// NetBIOS node status (RFC 1002 4.2.17) sent to one host: its workstation or server name.
static std::string NetBIOSStatus(const std::string& address, int timeoutMs)
{
    sockaddr_in dest { };
    dest.sin_family = AF_INET;
    dest.sin_port = htons(137);
    if (inet_pton(AF_INET, address.c_str(), &dest.sin_addr) != 1)
        return "";
    std::string result;
    Ask(dest, NetBIOSPacket(0, NetBIOSName("*", 0, '\0'), 0x21), timeoutMs, false,
        [&](const uint8_t* m, size_t length) {
            size_t offset = 12;
            std::string ignored;
            if (length < 12 || m[0] != 0x52 || m[1] != 0x53 || !(m[2] & 0x80) || ((m[6] << 8) | m[7]) == 0
                || !ReadName(m, length, offset, ignored) || offset + 11 > length)
                return false;
            offset += 10; // type, class, TTL, RDLENGTH
            unsigned count = m[offset++];
            for (unsigned i = 0; i < count && offset + 18 <= length; ++i, offset += 18) {
                uint8_t suffix = m[offset + 15];
                bool group = m[offset + 16] & 0x80;
                if (group || (suffix != 0x00 && suffix != 0x20))
                    continue;
                std::string name((const char*)m + offset, 15);
                while (!name.empty() && (name.back() == ' ' || name.back() == '\0'))
                    name.pop_back();
                if (!name.empty() && SafeName(name)) {
                    result = Lower(name);
                    return true;
                }
            }
            return false;
        });
    return result;
}
// NetBIOS name query (RFC 1002 4.2.12) broadcast on the LAN: the host's IPv4 address.
static std::string NetBIOSQuery(const std::string& name, int timeoutMs)
{
    sockaddr_in dest { };
    dest.sin_family = AF_INET;
    dest.sin_port = htons(137);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    std::string result;
    Ask(dest, NetBIOSPacket(0x0110, NetBIOSName(name, 0x20, ' '), 0x20), timeoutMs, true,
        [&](const uint8_t* m, size_t length) {
            size_t offset = 12;
            std::string ignored;
            if (length < 12 || m[0] != 0x52 || m[1] != 0x53 || !(m[2] & 0x80) || (m[3] & 0x0f)
                || ((m[6] << 8) | m[7]) == 0 || !ReadName(m, length, offset, ignored) || offset + 16 > length)
                return false;
            char text[INET_ADDRSTRLEN];
            if (!inet_ntop(AF_INET, m + offset + 12, text, sizeof(text)))
                return false;
            result = text;
            return true;
        });
    return result;
}
// Names learned from discovery, and lookups that failed recently.
static std::mutex namesLock;
static std::map<std::string, std::string> addresses; // lower-case name -> IPv4
static std::map<std::string, std::string> names; // IPv4 -> name as announced
static std::map<std::string, std::chrono::steady_clock::time_point> unresolved;
std::string ResolveHost(const std::string& host, int timeoutMs)
{
    std::string name = Lower(host);
    while (!name.empty() && name.back() == '.')
        name.pop_back();
    in_addr ignored;
    if (name.empty() || inet_pton(AF_INET, name.c_str(), &ignored) == 1)
        return host;
    bool local = name.size() > 6 && name.compare(name.size() - 6, 6, ".local") == 0;
    bool single = name.find('.') == std::string::npos;
    if (!local && !single)
        return host; // ordinary DNS name
    {
        std::lock_guard<std::mutex> l(namesLock);
        auto i = addresses.find(name);
        if (i != addresses.end())
            return i->second;
        auto u = unresolved.find(name);
        if (u != unresolved.end() && std::chrono::steady_clock::now() < u->second)
            return host;
    }
    // Haiku has no mDNS or NetBIOS resolver; unicast DNS would get these wrong.
    std::string address = local ? MulticastQuery(name, 1, timeoutMs) : NetBIOSQuery(name, timeoutMs / 2);
    if (address.empty() && single)
        address = MulticastQuery(name + ".local", 1, timeoutMs / 2);
    std::lock_guard<std::mutex> l(namesLock);
    if (address.empty()) {
        unresolved[name] = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        return host;
    }
    addresses[name] = address;
    return address;
}
std::string HostName(const std::string& address)
{
    {
        std::lock_guard<std::mutex> l(namesLock);
        auto i = names.find(address);
        if (i != names.end())
            return i->second;
    }
    in_addr ip;
    if (inet_pton(AF_INET, address.c_str(), &ip) != 1)
        return address;
    uint32_t a = ntohl(ip.s_addr);
    std::string reverse = std::to_string(a & 255) + "." + std::to_string((a >> 8) & 255) + "."
        + std::to_string((a >> 16) & 255) + "." + std::to_string(a >> 24) + ".in-addr.arpa";
    std::string name = MulticastQuery(reverse, 12, 1500); // Wi-Fi Macs can take over a second
    while (!name.empty() && name.back() == '.')
        name.pop_back();
    std::string lower = Lower(name);
    bool announced = lower.size() > 6 && lower.compare(lower.size() - 6, 6, ".local") == 0 && SafeName(name);
    if (!announced)
        name = NetBIOSStatus(address, 800);
    if (name.empty())
        return address;
    std::lock_guard<std::mutex> l(namesLock);
    addresses[Lower(name)] = address;
    // A Mac on Wi-Fi can miss the mDNS window and answer only NetBIOS ("mac-43c14f");
    // keep asking mDNS next time instead of pinning that name.
    if (announced)
        names[address] = name;
    return name;
}
}
