#include "Core.h"
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
static void Invalid(const std::string& text)
{
    bool rejected = false;
    try {
        smb::Location::Parse(text);
    } catch (...) {
        rejected = true;
    }
    assert(rejected);
}
static void ProbeTest(bool valid, bool fragmented, bool shortReply)
{
    int server = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    sockaddr_in addr { };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(server, (sockaddr*)&addr, sizeof(addr)) == 0);
    assert(listen(server, 1) == 0);
    socklen_t len = sizeof(addr);
    assert(getsockname(server, (sockaddr*)&addr, &len) == 0);
    std::thread peer([&] {
        int fd = accept(server, nullptr, nullptr);
        assert(fd >= 0);
        unsigned char request[112];
        size_t got = 0;
        while (got < sizeof(request)) {
            ssize_t n = recv(fd, request + got, sizeof(request) - got, 0);
            assert(n > 0);
            got += n;
        }
        assert(!memcmp(request + 4, "\xfeSMB", 4));
        assert(request[3] == 108);
        assert(request[68] == 36 && request[70] == 4);
        // Dialects follow the 36-byte request body: 0x0202 first, 0x0302 last.
        assert(request[104] == 2 && request[105] == 2 && request[110] == 2 && request[111] == 3);
        unsigned char response[68] { };
        response[3] = 64;
        memcpy(response + 4, valid ? "\xfeSMB" : "HTTP", 4);
        response[8] = 64;
        response[20] = 1;
        size_t count = shortReply ? 20 : sizeof(response);
        if (fragmented)
            for (size_t i = 0; i < count; ++i) {
                send(fd, response + i, 1, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        else
            send(fd, response, count, 0);
        close(fd);
    });
    bool result = smb::ProbeSMB("127.0.0.1", ntohs(addr.sin_port), 500);
    assert(result == (valid && !shortReply));
    peer.join();
    close(server);
}
int main(int argc, char** argv)
{
    signal(SIGPIPE, SIG_IGN);
    auto p = smb::Location::Parse("smb://nas/Shared%20Files/한글/a%23b.txt");
    assert(p.host == "nas" && p.share == "Shared Files" && p.path == "한글/a#b.txt");
    assert(smb::Location::Parse(p.URL()).path == p.path);
    assert(p.Parent().path == "한글");
    assert(p.Parent().Parent().path.empty());
    assert(p.Parent().Parent().Parent().share.empty());
    assert(smb::Location::Parse("nas").Child("Share").Child("A B").URL() == "smb://nas/Share/A%20B");
    for (auto text : { "", "smb://", "smb://user:secret@nas/share", "nas/share/..", "nas/share/%2fetc",
             "nas/share/%00", "nas/share/%", "nas/share/%5cfoo" })
        Invalid(text);
    auto n = smb::ParseNetwork("192.168.8.77/24");
    assert(n.first == 0xc0a80801 && n.last == 0xc0a808fe);
    auto one = smb::ParseNetwork("127.0.0.1/32");
    assert(one.first == one.last);
    auto two = smb::ParseNetwork("192.168.0.0/31");
    assert(two.last - two.first == 1);
    for (auto text : { "1.2.3.4/0", "1.2.3.4/33", "1.2.3.4/24oops", "bad/24" }) {
        bool rejected = false;
        try {
            smb::ParseNetwork(text);
        } catch (...) {
            rejected = true;
        }
        assert(rejected);
    }
    uint8_t response[68] { };
    response[3] = 64;
    memcpy(response + 4, "\xfeSMB", 4);
    response[8] = 64;
    response[20] = 1;
    assert(smb::IsSMBNegotiateResponse(response, sizeof(response)));
    for (size_t n = 0; n < 68; ++n)
        assert(!smb::IsSMBNegotiateResponse(response, n));
    response[12] = 1;
    assert(!smb::IsSMBNegotiateResponse(response, 68));
    response[12] = 0;
    response[20] = 0;
    assert(!smb::IsSMBNegotiateResponse(response, 68));
    response[20] = 1;
    response[16] = 3;
    assert(!smb::IsSMBNegotiateResponse(response, 68));
    response[16] = 0;
    response[3] = 10;
    assert(!smb::IsSMBNegotiateResponse(response, 68));
    if (argc > 1 && std::string(argv[1]) == "--network") {
        ProbeTest(true, false, false);
        ProbeTest(true, true, false);
        ProbeTest(false, false, false);
        ProbeTest(true, false, true);
        std::cout << "PASS: loopback negotiation, fragmented and truncated responses\n";
    }
    std::atomic<bool> cancel { true };
    int found = 0;
    smb::Scan({ one }, cancel, [&](const std::string&) { ++found; }, [](uint64_t, uint64_t) { });
    assert(found == 0);
    std::cout << "PASS: URL validation, Unicode, traversal rejection, subnet bounds, SMB response "
                 "validation, cancellation\n";
}
