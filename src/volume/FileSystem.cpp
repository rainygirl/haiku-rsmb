// R SMB virtual volume: /server/share/path -> SMB2/3. No local file mirroring.
#define FUSE_USE_VERSION 26
#include <fuse.h>
#include "Settings.h"
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <sys/statvfs.h>
#include <fs_info.h>

namespace {
struct Path {
    std::vector<std::string> parts;
    explicit Path(const char* s)
    {
        std::string p = s ? s : "";
        size_t i = 0;
        while (i < p.size()) {
            if (p[i] == '/') {
                ++i;
                continue;
            }
            auto j = p.find('/', i);
            auto part = p.substr(i, j - i);
            if (!smb::SafeName(part) || part.find('\\') != std::string::npos)
                throw std::invalid_argument("path");
            parts.push_back(part);
            if (j == std::string::npos)
                break;
            i = j + 1;
        }
    }
    std::string remote() const
    {
        std::string r;
        for (size_t i = 2; i < parts.size(); ++i) {
            if (!r.empty())
                r += '/';
            r += parts[i];
        }
        return r;
    }
    std::string host() const
    {
        return parts.at(0);
    }
    std::string share() const
    {
        return parts.at(1);
    }
};
struct Connection {
    smb2_context* c = nullptr;
    std::mutex lock; // libsmb2 contexts are not thread-safe
    Connection(const Path& p)
    {
        int e;
        std::string error;
        c = (smb2_context*)smb::Connect(p.host(), p.share(), rsmb::Auth(p.host()), e, error);
        if (!c) {
            fprintf(stderr, "R SMB connect %s/%s failed (%d): %s\n", p.host().c_str(), p.share().c_str(), e,
                error.c_str());
            fflush(stderr);
            throw e;
        }
    }
    ~Connection()
    {
        if (c)
            smb2_destroy_context(c);
    }
};
struct Handle {
    std::shared_ptr<Connection> connection;
    smb2fh* file;
    int flags;
    struct stat directory {};
};
// state guards the maps only. Network I/O holds a per-connection lock, so a slow
// share or directory never blocks cached lookups from Tracker's window thread.
std::mutex state, hostsLock;
std::map<std::string, std::string> discovered; // shown name -> IPv4 address
std::set<std::string> covered; // addresses of saved servers: those are listed once, under the saved name
std::atomic<bool> stopping(false);
std::thread scanner;
std::mutex sleepLock;
std::condition_variable wake;
std::map<std::string, std::shared_ptr<Connection>> connections;
struct CachedStat { struct stat value; bigtime_t until; };
// Short-lived directory metadata prevents Tracker's per-entry paint/lookup from
// repeating SMB round trips. All access is under io; mutations clear the cache.
std::map<std::string, CachedStat> attributes;
void Remember(const std::string& path, const struct stat& st)
{
    std::lock_guard<std::mutex> l(state);
    if (attributes.size() >= 4096) attributes.clear();
    attributes[path] = { st, system_time() + 2000000 };
}
bool Cached(const std::string& path, struct stat* st)
{
    std::lock_guard<std::mutex> l(state);
    auto i = attributes.find(path);
    if (i == attributes.end() || i->second.until <= system_time())
        return false;
    *st = i->second.value;
    return true;
}
void Forget()
{
    std::lock_guard<std::mutex> l(state);
    attributes.clear();
}
ino_t settingsNode = 0;
std::shared_ptr<Connection> Connect(const Path& p)
{
    std::string key = p.host() + "/" + p.share();
    struct stat config { };
    stat(rsmb::ConfigPath().c_str(), &config); // Save() renames a new file: new inode
    {
        std::lock_guard<std::mutex> l(state);
        if (config.st_ino != settingsNode) {
            settingsNode = config.st_ino; // saved login: new sessions use it; open files keep theirs
            connections.clear();
        }
        auto i = connections.find(key);
        if (i != connections.end())
            return i->second;
    }
    auto c = std::make_shared<Connection>(p); // may take seconds; keep state free
    std::lock_guard<std::mutex> l(state);
    auto i = connections.find(key);
    if (i != connections.end())
        return i->second;
    if (connections.size() > 64)
        connections.clear();
    connections[key] = c;
    return c;
}
int Error(int e)
{
    // On Haiku, POSIX-positive errno values are not Linux's small integers.
    if (e == 0)
        return 0;
    if (e == -1 || e == -EIO || e == -ECONNRESET || e == -ETIMEDOUT || e == -EPIPE || e == -ENOTCONN) {
        std::lock_guard<std::mutex> l(state);
        connections.clear(); // Never reuse a broken session. Do not retry mutations.
    }
    return e < 0 && e != -1 ? e : -EIO;
}
template <class F> int Guard(F f)
{
    try {
        return f();
    } catch (int e) {
        return Error(e);
    } catch (const std::invalid_argument&) {
        return -EINVAL;
    } catch (const std::bad_alloc&) {
        return -ENOMEM;
    } catch (...) {
        return -EIO;
    }
}
void Directory(struct stat* st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0755;
    st->st_nlink = 2;
    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_blksize = 65536;
}
void Stat(const smb2_stat_64& s, struct stat* st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = (s.smb2_type == SMB2_TYPE_DIRECTORY ? S_IFDIR | 0755 : S_IFREG | 0644);
    st->st_nlink = s.smb2_nlink ? s.smb2_nlink : 1;
    st->st_size = s.smb2_size;
    st->st_ino = s.smb2_ino;
    st->st_atime = s.smb2_atime;
    st->st_mtime = s.smb2_mtime;
    st->st_ctime = s.smb2_ctime;
    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_blksize = 65536;
    st->st_blocks = (st->st_size + 511) / 512;
}
void ScanLoop()
{
    while (!stopping) {
        try {
            auto settings = rsmb::Load();
            std::vector<std::string> found;
            auto nets = smb::LocalNetworks(); // subnets of the connected interfaces
            smb::Scan(
                nets, stopping,
                [&](const std::string& h) {
                    found.push_back(h); // Scan serializes this callback
                    std::lock_guard<std::mutex> l(hostsLock);
                    bool named = false;
                    for (auto& d : discovered)
                        named |= d.second == h;
                    if (!named)
                        discovered[h] = h; // shown by address until its name is known
                },
                [](uint64_t, uint64_t) { });
            // Finder shows names, not addresses: mDNS for Macs and Linux, NetBIOS for Windows.
            for (auto& address : found) {
                if (stopping)
                    break;
                auto name = smb::HostName(address);
                if (name == address)
                    continue;
                std::lock_guard<std::mutex> l(hostsLock);
                for (auto i = discovered.begin(); i != discovered.end();)
                    i = i->second == address && i->first != name ? discovered.erase(i) : std::next(i);
                discovered[name] = address;
            }
            std::set<std::string> saved;
            for (auto& s : settings.servers) {
                saved.insert(s.host);
                saved.insert(smb::ResolveHost(s.host));
            }
            std::lock_guard<std::mutex> l(hostsLock);
            covered = saved;
        } catch (...) {
        }
        std::unique_lock<std::mutex> l(sleepLock);
        wake.wait_for(l, std::chrono::seconds(60), [] { return stopping.load(); });
    }
}
int GetAttr(const char* path, struct stat* st)
{
    return Guard([&] {
        Path p(path);
        if (p.parts.size() < 2) {
            if (p.parts.size() == 1) {
                bool known;
                {
                    std::lock_guard<std::mutex> l(hostsLock);
                    known = discovered.count(p.host()) != 0;
                    for (auto& d : discovered)
                        known |= d.second == p.host(); // a discovered machine opened by address
                }
                for (const auto& server : rsmb::Load().servers)
                    if (server.host == p.host())
                        known = true;
                if (!known)
                    return -ENOENT;
            }
            Directory(st);
            return 0;
        }
        if (Cached(path, st))
            return 0;
        auto c = Connect(p);
        std::lock_guard<std::mutex> l(c->lock);
        smb2_stat_64 s { };
        int e = smb2_stat(c->c, p.remote().c_str(), &s);
        if (e) {
            fprintf(stderr, "R SMB stat failed (%d): %s\n", e, smb2_get_error(c->c));
            fflush(stderr);
            return Error(e);
        }
        Stat(s, st);
        Remember(path, *st);
        return 0;
    });
}
int FGetAttr(const char*, struct stat* st, fuse_file_info* fi)
{
    return Guard([&] {
        auto* h = (Handle*)fi->fh;
        if (!h->file) {
            *st = h->directory;
            return 0;
        }
        std::lock_guard<std::mutex> l(h->connection->lock);
        smb2_stat_64 s { };
        int e = smb2_fstat(h->connection->c, h->file, &s);
        if (e)
            return Error(e);
        Stat(s, st);
        return 0;
    });
}
struct Dir {
    std::mutex lock;
    std::string path;
    bool loaded = false;
    std::vector<std::pair<std::string, struct stat>> entries;
};
int LoadDirectory(Dir* d)
{
    Path p(d->path.c_str());
    const char* path = d->path.c_str();
    d->entries.clear();
        struct stat st;
        Directory(&st);
        d->entries.emplace_back(".", st);
        d->entries.emplace_back("..", st);
        if (p.parts.empty()) {
            std::set<std::string> names;
            {
                std::lock_guard<std::mutex> l(hostsLock);
                for (auto& d : discovered)
                    if (!covered.count(d.first) && !covered.count(d.second))
                        names.insert(d.first);
            }
            for (auto& s : rsmb::Load().servers)
                names.insert(s.host);
            for (auto& n : names)
                d->entries.emplace_back(n, st);
        } else if (p.parts.size() == 1) {
            smb::Location location { p.host(), "", "" };
            std::atomic<bool> cancel(false);
            std::set<std::string> names;
            for (auto& s : rsmb::Load().servers)
                if (s.host == p.host() && !s.share.empty())
                    names.insert(s.share);
            try {
                smb::Client client(location, rsmb::Auth(p.host()));
                for (auto& e : client.List(location, cancel))
                    if (e.name != "IPC$")
                        names.insert(e.name);
            } catch (const std::exception& e) {
                fprintf(stderr, "R SMB share list %s failed: %s\n", p.host().c_str(), e.what());
                fflush(stderr);
                if (names.empty())
                    return -EACCES;
            }
            for (auto& n : names)
                d->entries.emplace_back(n, st);
        } else {
            auto c = Connect(p);
            std::lock_guard<std::mutex> l(c->lock);
            auto* dir = smb2_opendir(c->c, p.remote().c_str());
            if (!dir)
                return -EIO;
            try {
                while (auto* e = smb2_readdir(c->c, dir))
                    if (smb::SafeName(e->name)) {
                        Stat(e->st, &st);
                        std::string child(path);
                        if (child.back() != '/') child += '/';
                        Remember(child + e->name, st);
                        d->entries.emplace_back(e->name, st);
                    }
            } catch (...) {
                smb2_closedir(c->c, dir);
                throw;
            }
            smb2_closedir(c->c, dir);
        }
    d->loaded = true;
    return 0;
}
int OpenDir(const char* path, fuse_file_info* fi)
{
    // Tracker initializes the iterator while holding its window lock. Defer SMB
    // enumeration to readdir, which its population worker calls without that lock.
    try {
        Path validate(path);
        std::unique_ptr<Dir> d(new Dir);
        d->path = path;
        fi->fh = (uint64_t)d.release();
        return 0;
    } catch (const std::bad_alloc&) { return -ENOMEM; }
      catch (...) { return -EINVAL; }
}

int ReadDir(const char*, void* buf, fuse_fill_dir_t fill, off_t offset, fuse_file_info* fi)
{
    return Guard([&] {
        auto* d = (Dir*)fi->fh;
        std::lock_guard<std::mutex> l(d->lock);
        if (offset < 0)
            return -EINVAL;
        if (!d->loaded) {
            int error = LoadDirectory(d);
            if (error) return error;
        }
        for (size_t i = offset; i < d->entries.size(); ++i)
            if (fill(buf, d->entries[i].first.c_str(), &d->entries[i].second, i + 1))
                break;
        return 0;
    });
}
int ReleaseDir(const char*, fuse_file_info* fi)
{
    delete (Dir*)fi->fh;
    fi->fh = 0;
    return 0;
}
int OpenFile(const char* path, fuse_file_info* fi)
{
    return Guard([&] {
        Path p(path);
        struct stat known;
        bool directory = p.parts.size() < 2;
        if (directory)
            Directory(&known);
        else if (Cached(path, &known) && S_ISDIR(known.st_mode))
            directory = true; // Tracker opens folders it just listed: no round trip
        if (directory) {
            if ((fi->flags & O_ACCMODE) != O_RDONLY || (fi->flags & (O_TRUNC | O_CREAT)))
                return -EISDIR;
            fi->fh = (uint64_t)new Handle { nullptr, nullptr, fi->flags, known };
            return 0;
        }
        auto c = Connect(p);
        std::lock_guard<std::mutex> l(c->lock);
        smb2_stat_64 info {};
        int e = smb2_stat(c->c, p.remote().c_str(), &info);
        if (e && e != -ENOENT)
            return Error(e);
        if (!e && info.smb2_type == SMB2_TYPE_DIRECTORY) {
            if ((fi->flags & O_ACCMODE) != O_RDONLY || (fi->flags & (O_TRUNC | O_CREAT)))
                return -EISDIR;
            auto* h = new Handle { nullptr, nullptr, fi->flags };
            Stat(info, &h->directory);
            fi->fh = (uint64_t)h;
            return 0;
        }
        if (fi->flags & (O_CREAT | O_TRUNC)) Forget();
        auto* f = smb2_open(c->c, p.remote().c_str(), fi->flags);
        if (!f) {
            fprintf(stderr, "R SMB open failed (flags=%x, errno=%d): %s\n", fi->flags, errno,
                smb2_get_error(c->c));
            fflush(stderr);
            return errno > 0 ? -errno : -EIO;
        }
        try {
            fi->fh = (uint64_t)new Handle { c, f, fi->flags };
        } catch (...) {
            smb2_close(c->c, f);
            throw;
        }
        fi->direct_io = 1;
        return 0;
    });
}
int Create(const char* p, mode_t, fuse_file_info* fi)
{
    fi->flags |= O_CREAT;
    return OpenFile(p, fi);
}
int Release(const char*, fuse_file_info* fi)
{
    return Guard([&] {
        std::unique_ptr<Handle> h((Handle*)fi->fh);
        fi->fh = 0;
        if (!h->file)
            return 0;
        std::lock_guard<std::mutex> l(h->connection->lock);
        return Error(smb2_close(h->connection->c, h->file));
    });
}
int Read(const char*, char* data, size_t size, off_t offset, fuse_file_info* fi)
{
    return Guard([&] {
        if (offset < 0)
            return -EINVAL;
        auto* h = (Handle*)fi->fh;
        if (!h->file) return -EISDIR;
        std::lock_guard<std::mutex> l(h->connection->lock);
        int n = smb2_pread(h->connection->c, h->file, (uint8_t*)data, std::min<size_t>(size, 65536), offset);
        return n < 0 ? Error(n) : n;
    });
}
int Write(const char*, const char* data, size_t size, off_t offset, fuse_file_info* fi)
{
    return Guard([&] {
        Forget();
        if (offset < 0)
            return -EINVAL;
        auto* h = (Handle*)fi->fh;
        if (!h->file) return -EISDIR;
        std::lock_guard<std::mutex> l(h->connection->lock);
        if (h->flags & O_APPEND) {
            smb2_stat_64 info { };
            int e = smb2_fstat(h->connection->c, h->file, &info);
            if (e)
                return Error(e);
            offset = info.smb2_size;
        }
        size_t done = 0;
        while (done < size) {
            int n = smb2_pwrite(h->connection->c, h->file, (const uint8_t*)data + done,
                std::min<size_t>(size - done, 65536), offset + done);
            if (n <= 0)
                return done ? (int)done : (n ? Error(n) : -EIO);
            done += n;
        }
        return (int)done;
    });
}
int Sync(const char*, int, fuse_file_info* fi)
{
    return Guard([&] {
        auto* h = (Handle*)fi->fh;
        if (!h->file)
            return 0;
        std::lock_guard<std::mutex> l(h->connection->lock);
        return Error(smb2_fsync(h->connection->c, h->file));
    });
}
int Flush(const char* p, fuse_file_info* fi)
{
    return Sync(p, 0, fi);
}
int Truncate(const char* path, off_t size)
{
    return Guard([&] {
        Forget();
        Path p(path);
        if (p.parts.size() < 3)
            return -EROFS;
        if (size < 0)
            return -EINVAL;
        auto c = Connect(p);
        std::lock_guard<std::mutex> l(c->lock);
        return Error(smb2_truncate(c->c, p.remote().c_str(), size));
    });
}
int FTruncate(const char*, off_t size, fuse_file_info* fi)
{
    return Guard([&] {
        Forget();
        if (size < 0)
            return -EINVAL;
        auto* h = (Handle*)fi->fh;
        if (!h->file)
            return -EISDIR;
        std::lock_guard<std::mutex> l(h->connection->lock);
        return Error(smb2_ftruncate(h->connection->c, h->file, size));
    });
}
int Mkdir(const char* path, mode_t)
{
    return Guard([&] {
        Forget();
        Path p(path);
        if (p.parts.size() < 3)
            return -EROFS;
        auto c = Connect(p);
        std::lock_guard<std::mutex> l(c->lock);
        return Error(smb2_mkdir(c->c, p.remote().c_str()));
    });
}
int Unlink(const char* path)
{
    return Guard([&] {
        Forget();
        Path p(path);
        if (p.parts.size() < 3)
            return -EROFS;
        auto c = Connect(p);
        std::lock_guard<std::mutex> l(c->lock);
        return Error(smb2_unlink(c->c, p.remote().c_str()));
    });
}
int Rmdir(const char* path)
{
    return Guard([&] {
        Forget();
        Path p(path);
        if (p.parts.size() < 3)
            return -EROFS;
        auto c = Connect(p);
        std::lock_guard<std::mutex> l(c->lock);
        auto* dir = smb2_opendir(c->c, p.remote().c_str());
        if (!dir)
            return -EIO;
        bool nonempty = false;
        while (auto* entry = smb2_readdir(c->c, dir))
            if (smb::SafeName(entry->name)) {
                nonempty = true;
                break;
            }
        smb2_closedir(c->c, dir);
        if (nonempty)
            return -ENOTEMPTY;
        return Error(smb2_rmdir(c->c, p.remote().c_str()));
    });
}
int Rename(const char* from, const char* to)
{
    return Guard([&] {
        Forget();
        Path a(from), b(to);
        if (a.parts.size() < 3 || b.parts.size() < 3)
            return -EROFS;
        if (a.host() != b.host() || a.share() != b.share())
            return -EXDEV;
        auto c = Connect(a);
        std::lock_guard<std::mutex> l(c->lock);
        return Error(smb2_rename(c->c, a.remote().c_str(), b.remote().c_str()));
    });
}
int StatFS(const char* path, struct statvfs* st)
{
    return Guard([&] {
        memset(st, 0, sizeof(*st));
        st->f_bsize = st->f_frsize = 4096;
        st->f_namemax = 255;
        Path p(path);
        // Virtual network root spans servers: no meaningful aggregate free-space figure.
        if (p.parts.size() < 2) {
            st->f_blocks = st->f_bfree = st->f_bavail = UINT32_MAX;
            return 0;
        }
        auto c = Connect(p);
        std::lock_guard<std::mutex> l(c->lock);
        struct smb2_statvfs s { };
        int e = smb2_statvfs(c->c, p.remote().c_str(), &s);
        if (e)
            return Error(e);
        st->f_bsize = s.f_bsize;
        st->f_frsize = s.f_frsize;
        st->f_blocks = s.f_blocks;
        st->f_bfree = s.f_bfree;
        st->f_bavail = s.f_bavail;
        return 0;
    });
}
int DriveInfo(const char*, int command, void* arg, fuse_file_info*, unsigned int, void*)
{
    if (command != FUSE_HAIKU_GET_DRIVE_INFO)
        return -ENOTTY;
    auto* info = static_cast<fs_info*>(arg);
    if (!info)
        return -EINVAL;
    memset(info, 0, sizeof(*info));
    info->flags = B_FS_IS_PERSISTENT | B_FS_IS_SHARED;
    info->block_size = 4096;
    info->io_size = 65536;
    // Tracker requires a positive free-space estimate before copying to a virtual volume.
    // The server's real per-share quota is enforced by each SMB write.
    info->total_blocks = info->free_blocks = UINT32_MAX;
    strcpy(info->volume_name, "R SMB");
    strcpy(info->fsh_name, "rsmb");
    return 0;
}
void* Init(fuse_conn_info* connection)
{
    if (connection->capable & FUSE_CAP_HAIKU_FUSE_EXTENSIONS)
        connection->want |= FUSE_CAP_HAIKU_FUSE_EXTENSIONS;
    stopping = false;
    scanner = std::thread(ScanLoop);
    return nullptr;
}
void Destroy(void*)
{
    stopping = true;
    wake.notify_all();
    if (scanner.joinable())
        scanner.join();
    std::lock_guard<std::mutex> l(state);
    connections.clear();
    attributes.clear();
}
}
int main(int argc, char** argv)
{
    fuse_operations op { };
    op.getattr = GetAttr;
    op.fgetattr = FGetAttr;
    op.opendir = OpenDir;
    op.readdir = ReadDir;
    op.releasedir = ReleaseDir;
    op.open = OpenFile;
    op.create = Create;
    op.release = Release;
    op.read = Read;
    op.write = Write;
    op.flush = Flush;
    op.fsync = Sync;
    op.truncate = Truncate;
    op.ftruncate = FTruncate;
    op.mkdir = Mkdir;
    op.unlink = Unlink;
    op.rmdir = Rmdir;
    op.rename = Rename;
    op.statfs = StatFS;
    op.ioctl = DriveInfo;
    op.init = Init;
    op.destroy = Destroy;
    return fuse_main(argc, argv, &op, nullptr);
}
