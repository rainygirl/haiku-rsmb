#pragma once
#include "Core.h"
#include "Paths.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
#ifndef RSMB_FUSE
#include <KeyStore.h>
#endif
namespace rsmb {
constexpr const char* Signature = "application/x-vnd.R-SMB";
constexpr const char* MountPoint = "/R SMB Network";
struct Server {
    std::string host, user, domain, share;
};
struct Settings {
    bool enabled = false;
    std::vector<Server> servers;
};
inline std::string ConfigPath()
{
    return "/boot/home/config/settings/RSMB_volume.conf";
}
inline std::string Hex(const std::string& s)
{
    const char* d = "0123456789abcdef";
    std::string o;
    for (unsigned char c : s) {
        o += d[c >> 4];
        o += d[c & 15];
    }
    return o.empty() ? "-" : o;
}
inline std::string Unhex(const std::string& s)
{
    if (s == "-")
        return "";
    if (s.size() % 2)
        throw std::invalid_argument("config");
    std::string o;
    for (size_t i = 0; i < s.size(); i += 2) {
        auto a = std::string("0123456789abcdef").find(s[i]),
             b = std::string("0123456789abcdef").find(s[i + 1]);
        if (a > 15 || b > 15)
            throw std::invalid_argument("config");
        o += char(a * 16 + b);
    }
    return o;
}
inline Settings Load()
{
    Settings s;
    std::ifstream f(ConfigPath());
    std::string line;
    while (std::getline(f, line)) {
        try {
            std::istringstream r(line);
            std::string k, a, b, c, d;
            r >> k;
            if (k == "enabled") {
                r >> a;
                s.enabled = a == "1";
            } else if (k == "server" && r >> a >> b >> c >> d) {
                Server v { Unhex(a), Unhex(b), Unhex(c), Unhex(d) };
                if (smb::SafeName(v.host))
                    s.servers.push_back(v);
            }
        } catch (...) {
        }
    }
    return s;
}
inline int Save(const Settings& s)
{
    std::string tmp = ConfigPath() + ".XXXXXX";
    int fd = mkstemp(&tmp[0]);
    if (fd < 0)
        return -1;
    FILE* f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(tmp.c_str());
        return -1;
    }
    fprintf(f, "enabled %d\n", s.enabled);
    for (auto& r : s.servers)
        fprintf(f, "server %s %s %s %s\n", Hex(r.host).c_str(), Hex(r.user).c_str(), Hex(r.domain).c_str(),
            Hex(r.share).c_str());
    bool ok = fflush(f) == 0;
    if (ok)
        ok = fsync(fileno(f)) == 0;
    if (fclose(f) != 0)
        ok = false;
    int result = ok ? rename(tmp.c_str(), ConfigPath().c_str()) : -1;
    if (result)
        unlink(tmp.c_str());
    return result;
}
#ifndef RSMB_FUSE
inline smb::Credentials Auth(const std::string& host)
{
    smb::Credentials a;
    for (auto& r : Load().servers)
        if (r.host == host) {
            a.user = r.user;
            a.domain = r.domain;
            break;
        }
    if (!a.user.empty()) {
        BPasswordKey key;
        BKeyStore store;
        std::string id = "RSMB:" + host;
        if (store.GetKey(B_KEY_TYPE_PASSWORD, id.c_str(), a.user.c_str(), key) == B_OK && key.Password())
            a.password = key.Password();
    }
    return a;
}
inline status_t Password(const Server& r, const char* password)
{
    BKeyStore store;
    std::string id = "RSMB:" + r.host;
    BPasswordKey old;
    if (store.GetKey(B_KEY_TYPE_PASSWORD, id.c_str(), r.user.c_str(), old) == B_OK) {
        auto e = store.RemoveKey(old);
        if (e != B_OK)
            return e;
    }
    BPasswordKey key(password, B_KEY_PURPOSE_NETWORK, id.c_str(), r.user.c_str());
    return store.AddKey(key);
}
#else
// Credential helper has the native Interface Kit ABI. No password in argv or files.
// The helper uses the Interface Kit; the volume stays in its own process.
inline smb::Credentials Auth(const std::string& host)
{
    smb::Credentials a;
    for (auto& r : Load().servers)
        if (r.host == host) {
            a.user = r.user;
            a.domain = r.domain;
            break;
        }
    if (a.user.empty())
        return a;
    int fd[2];
    if (pipe(fd))
        throw std::runtime_error("credential pipe");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, fd[0]);
    posix_spawn_file_actions_addclose(&actions, fd[1]);
    const char* program = ProgramPath();
    char* args[] = { const_cast<char*>(program), const_cast<char*>("--auth"), const_cast<char*>(host.c_str()),
        nullptr };
    pid_t pid;
    int e = posix_spawn(&pid, program, &actions, nullptr, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fd[1]);
    if (e) {
        close(fd[0]);
        throw std::runtime_error("credential helper");
    }
    std::string output;
    char buf[256];
    ssize_t n;
    while ((n = read(fd[0], buf, sizeof(buf))) > 0 && output.size() < 65536)
        output.append(buf, n);
    close(fd[0]);
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    if (!WIFEXITED(status) || WEXITSTATUS(status))
        throw std::runtime_error("credential access denied");
    std::istringstream lines(output);
    std::string line, tag, u, p, d;
    bool found = false;
    while (!found && std::getline(lines, line)) {
        std::istringstream r(line);
        found = r >> tag >> u >> p >> d && tag == "auth";
    }
    if (!found)
        throw std::runtime_error("credential response");
    a.user = Unhex(u);
    a.password = Unhex(p);
    a.domain = Unhex(d);
    return a;
}
#endif
}
