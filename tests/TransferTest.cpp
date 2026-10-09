// Fault-injection tests for filesystem safety. The SMB transport is fake;
// these tests intentionally make no claim about server interoperability.
#include "Core.h"
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#ifdef LIBSMB2_SRVSVC_V2
#include <smb2/libsmb2-share-enum.h>
#else
#include <smb2/libsmb2-dcerpc-srvsvc.h>
#endif
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <unistd.h>
struct smb2_context { };
struct smb2fh {
    std::string path;
    size_t offset = 0;
};
static std::map<std::string, std::string> remote;
static bool failRead = false, failWrite = false;
extern "C" {
smb2_context* smb2_init_context()
{
    return new smb2_context;
}
void smb2_destroy_context(smb2_context* c)
{
    delete c;
}
void smb2_set_timeout(smb2_context*, int) { }
void smb2_set_authentication(smb2_context*, int) { }
void smb2_set_security_mode(smb2_context*, uint16_t) { }
void smb2_set_user(smb2_context*, const char*) { }
void smb2_set_password(smb2_context*, const char*) { }
void smb2_set_domain(smb2_context*, const char*) { }
int smb2_connect_share(smb2_context*, const char*, const char*, const char*)
{
    return 0;
}
const char* smb2_get_error(smb2_context*)
{
    return "injected failure";
}
void smb2_free_data(smb2_context*, void*) { }
#ifdef LIBSMB2_SRVSVC_V2
int smb2_share_enum_async(smb2_context*, smb2_share_info_level, smb2_command_cb, void*)
{
    return -1;
}
#else
int smb2_share_enum_async(smb2_context*, smb2_command_cb, void*)
{
    return -1;
}
#endif
int smb2_get_fd(smb2_context*)
{
    return -1;
}
int smb2_which_events(smb2_context*)
{
    return 0;
}
int smb2_service(smb2_context*, int)
{
    return -1;
}
smb2dir* smb2_opendir(smb2_context*, const char*)
{
    return nullptr;
}
smb2dirent* smb2_readdir(smb2_context*, smb2dir*)
{
    return nullptr;
}
void smb2_closedir(smb2_context*, smb2dir*) { }
int smb2_stat(smb2_context*, const char* path, smb2_stat_64* st)
{
    if (!remote.count(path))
        return -1;
    st->smb2_type = SMB2_TYPE_FILE;
    st->smb2_size = remote[path].size();
    return 0;
}
smb2fh* smb2_open(smb2_context*, const char* path, int flags)
{
    if (flags & O_CREAT) {
        assert(flags & O_EXCL);
        if (remote.count(path))
            return nullptr;
        remote[path] = "";
    } else if (!remote.count(path))
        return nullptr;
    return new smb2fh { path, 0 };
}
int smb2_close(smb2_context*, smb2fh* f)
{
    delete f;
    return 0;
}
int smb2_read(smb2_context*, smb2fh* f, uint8_t* buf, uint32_t count)
{
    if (failRead && f->offset > 0)
        return -1;
    auto& data = remote[f->path];
    size_t n = std::min({ size_t(count), size_t(7), data.size() - f->offset });
    memcpy(buf, data.data() + f->offset, n);
    f->offset += n;
    return n;
}
int smb2_write(smb2_context*, smb2fh* f, const uint8_t* buf, uint32_t count)
{
    if (failWrite && f->offset > 0)
        return -1;
    size_t n = std::min(size_t(count), size_t(3));
    remote[f->path].append((const char*)buf, n);
    f->offset += n;
    return n;
}
int smb2_unlink(smb2_context*, const char* path)
{
    remote.erase(path);
    return 0;
}
}
static std::string Read(const std::string& path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), { });
}
template <class F> static void Fails(F f)
{
    bool failed = false;
    try {
        f();
    } catch (...) {
        failed = true;
    }
    assert(failed);
}
int main()
{
    char temp[] = "/tmp/haiku-smb-transfer-XXXXXX";
    assert(mkdtemp(temp));
    std::string dir = temp;
    auto location = smb::Location::Parse("smb://test/share");
    smb::Client client(location, { });
    std::atomic<bool> cancel { false };
    auto progress = [](uint64_t, uint64_t) { };
    remote["input"] = "A file with Unicode: 한글 and several partial reads.";
    client.Download("input", dir + "/copy", cancel, progress);
    assert(Read(dir + "/copy") == remote["input"]);
    Fails([&] { client.Download("input", dir + "/copy", cancel, progress); });
    assert(Read(dir + "/copy") == remote["input"]);
    assert(symlink((dir + "/copy").c_str(), (dir + "/link").c_str()) == 0);
    Fails([&] { client.Download("input", dir + "/link", cancel, progress); });
    assert(Read(dir + "/copy") == remote["input"]);
    failRead = true;
    Fails([&] { client.Download("input", dir + "/partial", cancel, progress); });
    assert(access((dir + "/partial").c_str(), F_OK) != 0);
    failRead = false;
    client.Upload(dir + "/copy", "uploaded", cancel, progress);
    assert(remote["uploaded"] == remote["input"]);
    Fails([&] { client.Upload(dir + "/copy", "uploaded", cancel, progress); });
    assert(remote["uploaded"] == remote["input"]);
    failWrite = true;
    Fails([&] { client.Upload(dir + "/copy", "broken", cancel, progress); });
    assert(!remote.count("broken"));
    failWrite = false;
    cancel = true;
    Fails([&] { client.Download("input", dir + "/cancel", cancel, progress); });
    assert(access((dir + "/cancel").c_str(), F_OK) != 0);
    Fails([&] { client.Upload(dir + "/copy", "cancel", cancel, progress); });
    assert(!remote.count("cancel"));
    unlink((dir + "/link").c_str());
    unlink((dir + "/copy").c_str());
    rmdir(dir.c_str());
    std::cout << "PASS: partial I/O, Unicode contents, local/remote no-overwrite, symlink protection, "
                 "error/cancel cleanup\n";
}
