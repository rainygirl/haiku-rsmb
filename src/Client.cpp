#include "Core.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef LIBSMB2_SRVSVC_V2
#include <smb2/libsmb2-share-enum.h>
#else
#include <smb2/libsmb2-dcerpc-srvsvc.h>
#endif
namespace smb {
static void CheckCancel(std::atomic<bool>& cancel)
{
    if (cancel)
        throw std::runtime_error("Canceled.");
}
static void Error(smb2_context* c, const char* operation)
{
    throw std::runtime_error(std::string(operation) + ": " + smb2_get_error(c));
}
static smb2_context* Attempt(const std::string& address, const std::string& share, const Credentials& auth,
    int method, int& status, std::string& error)
{
    auto* c = smb2_init_context();
    if (!c) {
        status = -1; // Haiku's ENOMEM is negative in some builds; -1 means "failed" to callers
        error = "Cannot initialize SMB client.";
        return nullptr;
    }
    smb2_set_timeout(c, 12);
    // libsmb2 4.0 keeps the authentication enum private: NTLMSSP is 1, Kerberos 2.
    smb2_set_authentication(c, method);
    smb2_set_security_mode(c, SMB2_NEGOTIATE_SIGNING_ENABLED);
    smb2_set_user(c, auth.user.empty() ? "guest" : auth.user.c_str());
    smb2_set_password(c, auth.password.c_str());
    smb2_set_domain(c, auth.domain.c_str());
    status = smb2_connect_share(c, address.c_str(), share.c_str(), nullptr);
    if (status == 0)
        return c;
    error = smb2_get_error(c);
    smb2_destroy_context(c);
    return nullptr;
}
void* Connect(const std::string& host, const std::string& share, const Credentials& auth, int& status,
    std::string& error)
{
    auto address = ResolveHost(host);
    auto* c = Attempt(address, share, auth, 1, status, error);
    if (c || auth.user.empty() || auth.password.empty())
        return c;
    // macOS keeps no NTLM hash unless "Windows File Sharing" is on for the account.
    int kerberosStatus;
    std::string kerberosError;
    c = Attempt(address, share, auth, 2, kerberosStatus, kerberosError);
    if (c)
        fprintf(stderr, "R SMB: %s/%s: Mac Kerberos login\n", host.c_str(), share.c_str());
    if (!c && kerberosError.find("no Mac login service") == std::string::npos) {
        status = kerberosStatus;
        error = kerberosError;
    }
    return c;
}
Client::Client(const Location& location, const Credentials& auth)
{
    int status;
    std::string error;
    context = Connect(location.host, location.share.empty() ? "IPC$" : location.share, auth, status, error);
    if (!context)
        throw std::runtime_error(
            "Connection failed: " + error + "\nCheck the server address, share name and login.");
}
Client::~Client()
{
    if (context)
        smb2_destroy_context((smb2_context*)context);
}
struct Enumeration {
    bool done = false;
    std::string error;
    std::vector<Entry> entries;
};
static void Shares(smb2_context* c, int status, void* data, void* opaque)
{
    auto& result = *static_cast<Enumeration*>(opaque);
    if (status != 0 || !data)
        result.error = smb2_get_error(c);
    else {
#ifdef LIBSMB2_SRVSVC_V2
        auto* reply = static_cast<smb2_share_enum_reply*>(data);
        for (uint32_t i = 0; i < reply->entries_read; ++i) {
            auto& share = reply->share_info.info_1[i];
            if ((share.type & 3) == 0 && share.netname && SafeName(share.netname))
                result.entries.push_back({ share.netname, true, 0, 0 });
        }
#else
        auto* reply = static_cast<srvsvc_netshareenumall_rep*>(data);
        if (reply->status != 0)
            result.error = "Server refused to list shares (status " + std::to_string(reply->status)
                + "). Enter smb://server/share directly.";
        else if (reply->ctr)
            for (uint32_t i = 0; i < reply->ctr->ctr1.count; ++i) {
                auto& share = reply->ctr->ctr1.array[i];
                if ((share.type & 3) == 0 && share.name && SafeName(share.name))
                    result.entries.push_back({ share.name, true, 0, 0 });
            }
#endif
    }
    if (data)
        smb2_free_data(c, data);
    if (status != 0 && result.error.empty())
        result.error = "Share enumeration failed.";
    result.done = true;
}
std::vector<Entry> Client::List(const Location& location, std::atomic<bool>& cancel)
{
    auto* c = (smb2_context*)context;
    std::vector<Entry> entries;
    CheckCancel(cancel);
    if (location.share.empty()) {
        Enumeration result;
#ifdef LIBSMB2_SRVSVC_V2
        int status = smb2_share_enum_async(c, SMB2_SHARE_INFO_1, Shares, &result);
#else
        int status = smb2_share_enum_async(c, Shares, &result);
#endif
        if (status != 0)
            Error(c, "Cannot list shares");
        try {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (!result.done) {
                CheckCancel(cancel);
                if (std::chrono::steady_clock::now() > deadline)
                    throw std::runtime_error("Share listing timed out.");
                pollfd p { smb2_get_fd(c), (short)smb2_which_events(c), 0 };
                if (poll(&p, 1, 100) < 0 && errno != EINTR)
                    throw std::runtime_error("SMB socket polling failed.");
                if (smb2_service(c, p.revents) < 0)
                    Error(c, "Cannot list shares");
            }
        } catch (...) {
            // Destroy while callback storage is alive: pending callbacks can fire during destruction.
            smb2_destroy_context(c);
            context = nullptr;
            throw;
        }
        if (!result.error.empty())
            throw std::runtime_error(result.error);
        entries = std::move(result.entries);
    } else {
        smb2dir* dir = smb2_opendir(c, location.path.c_str());
        if (!dir)
            Error(c, "Cannot open folder");
        try {
            while (auto* item = smb2_readdir(c, dir)) {
                CheckCancel(cancel);
                if (SafeName(item->name))
                    entries.push_back({ item->name, item->st.smb2_type == SMB2_TYPE_DIRECTORY,
                        item->st.smb2_size, item->st.smb2_mtime });
            }
        } catch (...) {
            smb2_closedir(c, dir);
            throw;
        }
        smb2_closedir(c, dir);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory)
            return a.directory > b.directory;
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return entries;
}
void Client::Download(
    const std::string& remote, const std::string& local, std::atomic<bool>& cancel, const Progress& progress)
{
    auto* c = (smb2_context*)context;
    smb2_stat_64 st { };
    if (smb2_stat(c, remote.c_str(), &st) != 0)
        Error(c, "Cannot read file information");
    if (st.smb2_type != SMB2_TYPE_FILE)
        throw std::runtime_error("Only regular files can be downloaded.");
    auto* file = smb2_open(c, remote.c_str(), O_RDONLY);
    if (!file)
        Error(c, "Cannot open remote file");
    // Reserve the final name exclusively. Never truncate an existing local file or follow its symlink.
    int fd = open(local.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        smb2_close(c, file);
        throw std::runtime_error(std::string("Cannot create local file: ") + strerror(errno));
    }
    try {
        uint8_t buffer[65536];
        uint64_t total = 0;
        for (;;) {
            CheckCancel(cancel);
            int n = smb2_read(c, file, buffer, sizeof(buffer));
            if (n < 0)
                Error(c, "Download failed");
            if (n == 0)
                break;
            int offset = 0;
            while (offset < n) {
                ssize_t written = write(fd, buffer + offset, n - offset);
                if (written < 0 && errno == EINTR)
                    continue;
                if (written <= 0)
                    throw std::runtime_error("Cannot write local file (disk full or I/O error).");
                offset += written;
            }
            total += n;
            progress(total, st.smb2_size);
        }
        if (total != st.smb2_size)
            throw std::runtime_error("Remote file changed during download. Please retry.");
        if (fsync(fd) != 0)
            throw std::runtime_error("Cannot flush local file.");
        int closed = close(fd);
        fd = -1;
        if (closed != 0)
            throw std::runtime_error("Cannot close local file.");
        int status = smb2_close(c, file);
        file = nullptr;
        if (status != 0)
            Error(c, "Cannot close remote file");
    } catch (...) {
        if (fd >= 0)
            close(fd);
        if (file)
            smb2_close(c, file);
        unlink(local.c_str());
        throw;
    }
}
void Client::Upload(
    const std::string& local, const std::string& remote, std::atomic<bool>& cancel, const Progress& progress)
{
    auto* c = (smb2_context*)context;
    int fd = open(local.c_str(), O_RDONLY);
    if (fd < 0)
        throw std::runtime_error("Cannot open local file.");
    struct stat st { };
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        throw std::runtime_error("Choose a regular file.");
    }
    auto* file = smb2_open(c, remote.c_str(), O_WRONLY | O_CREAT | O_EXCL);
    if (!file) {
        close(fd);
        Error(c, "Cannot create remote file (existing files are never overwritten)");
    }
    try {
        uint8_t buffer[65536];
        uint64_t total = 0;
        for (;;) {
            CheckCancel(cancel);
            ssize_t n = read(fd, buffer, sizeof(buffer));
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                throw std::runtime_error("Cannot read local file.");
            if (n == 0)
                break;
            int offset = 0;
            while (offset < n) {
                CheckCancel(cancel);
                int written = smb2_write(c, file, buffer + offset, n - offset);
                if (written <= 0)
                    Error(c, "Upload failed");
                offset += written;
            }
            total += n;
            progress(total, st.st_size);
        }
        if (total != (uint64_t)st.st_size)
            throw std::runtime_error("Local file changed during upload.");
        int status = smb2_close(c, file);
        file = nullptr;
        if (status != 0)
            Error(c, "Cannot finish upload");
        close(fd);
    } catch (...) {
        close(fd);
        if (file)
            smb2_close(c, file);
        smb2_unlink(c, remote.c_str());
        throw;
    }
}
}
