#include "Settings.h"
#include "Strings.h"
#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <ControlLook.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Message.h>
#include <Messenger.h>
#include <Roster.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <sys/wait.h>
#include <fcntl.h>
#include <OS.h>
namespace {
constexpr uint32 Scan = 'rscn', Configure = 'rcfg', Save = 'rsav', Select = 'rsel', Done = 'rdon';
bool Mounted()
{
    struct stat a { }, b { };
    return stat(rsmb::MountPoint, &a) == 0 && stat("/", &b) == 0 && a.st_dev != b.st_dev;
}
int RunCommand(const std::vector<std::string>& args)
{
    std::vector<char*> a;
    for (auto& s : args)
        a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);
    pid_t pid;
    int e = posix_spawn(&pid, a[0], nullptr, nullptr, a.data(), environ);
    if (e)
        return e;
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return errno;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
int MountVolume()
{
    if (Mounted())
        return 0;
    if (mkdir(rsmb::MountPoint, 0755) != 0 && errno != EEXIST)
        return errno;
    // The app-private runtime sits next to this program, wherever it is installed.
    std::string program = rsmb::ProgramPath();
    std::string privateServer = program.substr(0, program.rfind('/') + 1) + "userlandfs_server";
    const char* server = privateServer.c_str();
    if (access(server, X_OK) != 0)
        server = "/boot/system/servers/userlandfs_server";
    if (find_port("_userlandfs_rsmb_volume") < 0) {
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/boot/home/config/settings/RSMB-mount.log",
            O_WRONLY | O_CREAT | O_TRUNC, 0600);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
        char* args[] = { const_cast<char*>(server), const_cast<char*>("rsmb_volume"), nullptr };
        pid_t pid;
        int e = posix_spawn(&pid, server, &actions, nullptr, args, environ);
        posix_spawn_file_actions_destroy(&actions);
        if (e)
            return e;
        for (int i = 0; i < 100 && find_port("_userlandfs_rsmb_volume") < 0; ++i) {
            int status;
            if (waitpid(pid, &status, WNOHANG) == pid)
                return 1;
            snooze(100000);
        }
        if (find_port("_userlandfs_rsmb_volume") < 0)
            return 1;
    }
    return RunCommand({ "/boot/system/bin/mount", "-t", "userlandfs", "-p", "rsmb_volume", rsmb::MountPoint });
}
int StopVolume()
{
    if (Mounted()) {
        int e = RunCommand({ "/boot/system/bin/unmount", rsmb::MountPoint });
        if (e)
            return e;
    }
    port_info info;
    if (get_port_info(find_port("_userlandfs_rsmb_volume"), &info) == B_OK) {
        BMessenger server(nullptr, info.team);
        server.SendMessage(B_QUIT_REQUESTED);
        for (int i = 0; i < 50 && find_port("_userlandfs_rsmb_volume") >= 0; ++i)
            snooze(100000);
        if (find_port("_userlandfs_rsmb_volume") >= 0)
            return 1;
    }
    return 0;
}
int SetEnabled(bool enabled)
{
    // Unmount first: a busy volume stays enabled and reports failure.
    if (!enabled) {
        int e = StopVolume();
        if (e)
            return e;
    }
    auto s = rsmb::Load();
    s.enabled = enabled;
    if (rsmb::Save(s))
        return 1;
    return enabled ? MountVolume() : 0;
}
class Window : public BWindow {
    BListView* servers;
    BScrollView* serversScroll;
    BTextControl *address, *user, *password, *domain;
    BButton *find, *save;
    BStringView* status;
    rsmb::Settings settings;
    std::thread worker;
    std::atomic<bool> cancel { false };
    bool busy = false;
    void Reload()
    {
        settings = rsmb::Load();
        while (auto* item = servers->RemoveItem(int32(0)))
            delete item;
        // As wide as Network preferences' list, wider for long names such as "name.local".
        float width = servers->StringWidth("Someones-MacBook-Air-2.local");
        for (auto& s : settings.servers) {
            servers->AddItem(new BStringItem(s.host.c_str()));
            width = std::max(width, servers->StringWidth(s.host.c_str()));
        }
        serversScroll->SetExplicitMinSize(BSize(width + be_control_look->DefaultLabelSpacing() * 3
                + B_V_SCROLL_BAR_WIDTH, B_SIZE_UNSET));
    }
    void FindServers()
    {
        if (busy)
            return;
        if (worker.joinable())
            worker.join();
        busy = true;
        cancel = false;
        find->SetEnabled(false);
        status->SetText(rsmb::T("Searching the network…"));
        BMessenger target(this);
        worker = std::thread([this, target] {
            std::string text;
            try {
                auto nets = smb::LocalNetworks(); // subnets of the connected interfaces
                size_t found = 0, locked = 0;
                smb::Scan(
                    nets, cancel,
                    [&](const std::string& address) {
                        auto host = smb::HostName(address); // the name Finder would show
                        auto current = rsmb::Load();
                        if (std::none_of(current.servers.begin(), current.servers.end(),
                                [&](const rsmb::Server& r) { return r.host == host; })) {
                            current.servers.push_back({ host, "", "", "" });
                            if (rsmb::Save(current))
                                throw std::runtime_error("Cannot save discovered server.");
                        }
                        ++found;
                        try {
                            smb::Location l { host, "", "" };
                            smb::Client client(l, rsmb::Auth(host));
                            client.List(l, cancel);
                        } catch (...) {
                            ++locked;
                        }
                    },
                    [](uint64_t, uint64_t) { });
                text = found ? rsmb::TCount("Found 1 server.", "Found %d servers.", found).String()
                             : rsmb::T("No servers on this network. Enter a server address and save it.");
                if (locked)
                    text = text + " "
                        + rsmb::TCount("1 needs a login: select it and enter it.",
                            "%d need a login: select one and enter it.", locked).String();
            } catch (const std::exception& e) {
                text = rsmb::T(e.what());
            }
            BMessage done(Done);
            done.AddString("text", text.c_str());
            target.SendMessage(&done);
        });
    }
    void SaveLogin()
    {
        try {
            auto l = smb::Location::Parse(address->Text());
            if (!l.path.empty())
                throw std::runtime_error("Enter a server or share, without a subfolder.");
            rsmb::Server r { l.host, user->Text(), domain->Text(), l.share };
            settings = rsmb::Load();
            auto it = std::find_if(settings.servers.begin(), settings.servers.end(),
                [&](const rsmb::Server& s) { return s.host == r.host; });
            if (it == settings.servers.end())
                settings.servers.push_back(r);
            else
                *it = r;
            if (*password->Text() && rsmb::Password(r, password->Text()) != B_OK)
                throw std::runtime_error("Could not store the password in Haiku KeyStore.");
            if (rsmb::Save(settings))
                throw std::runtime_error("Could not save the login.");
            password->SetText("");
            Reload();
            status->SetText(rsmb::T("Saved. Open the server in the R SMB volume on the Desktop."));
        } catch (const std::exception& e) {
            status->SetText(rsmb::T(e.what()));
        }
    }

public:
    Window()
        : BWindow(BRect(120, 100, 720, 460), rsmb::T("R SMB settings"), B_TITLED_WINDOW,
              B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
    {
        servers = new BListView("servers");
        servers->SetSelectionMessage(new BMessage(Select));
        address = new BTextControl("address", rsmb::T("Server:"), "", nullptr);
        address->SetToolTip(rsmb::T("Name or IP address, or smb://server/share"));
        user = new BTextControl("user", rsmb::T("User:"), "", nullptr);
        user->SetToolTip(rsmb::T("Leave blank to connect as guest"));
        password = new BTextControl("password", rsmb::T("Password:"), "", nullptr);
        password->TextView()->HideTyping(true);
        domain = new BTextControl("domain", rsmb::T("Domain:"), "", nullptr);
        find = new BButton("find", rsmb::T("Find servers"), new BMessage(Scan));
        save = new BButton("save", rsmb::T("Save login"), new BMessage(Save));
        status = new BStringView("status", rsmb::T("Select a server to enter its login, or add one."));
        BStringView* serversLabel = new BStringView("servers-label", rsmb::T("Servers"));
        serversLabel->SetFont(be_bold_font);
        BStringView* loginLabel = new BStringView("login-label", rsmb::T("Login"));
        loginLabel->SetFont(be_bold_font);
        BLayoutBuilder::Group<>(this, B_VERTICAL)
            .SetInsets(B_USE_WINDOW_SPACING)
            .AddGroup(B_HORIZONTAL, B_USE_BIG_SPACING)
                .AddGroup(B_VERTICAL)
                    .Add(serversLabel)
                    .Add(serversScroll = new BScrollView("scroll", servers, 0, false, true), 1)
                    .Add(find)
                .End()
                .AddGroup(B_VERTICAL)
                    .Add(loginLabel)
                    .AddGrid()
                        .AddTextControl(address, 0, 0)
                        .AddTextControl(user, 0, 1)
                        .AddTextControl(password, 0, 2)
                        .AddTextControl(domain, 0, 3)
                    .End()
                    .AddGroup(B_HORIZONTAL)
                        .AddGlue()
                        .Add(save)
                    .End()
                    .Add(new BStringView("password-note", rsmb::T("Passwords are kept in Haiku KeyStore.")))
                    .AddGlue()
                .End()
            .End()
            .Add(status);
        save->MakeDefault(true);
        Reload();
        CenterOnScreen();
        Show();
    }
    ~Window()
    {
        cancel = true;
        if (worker.joinable())
            worker.join();
    }
    bool QuitRequested() override
    {
        cancel = true;
        return true;
    }
    void MessageReceived(BMessage* m) override
    {
        switch (m->what) {
        case Scan:
            FindServers();
            break;
        case Select: {
            int32 i = servers->CurrentSelection();
            if (i >= 0 && size_t(i) < settings.servers.size()) {
                auto& s = settings.servers[i];
                address->SetText(smb::Location { s.host, s.share, "" }.URL().c_str());
                user->SetText(s.user.c_str());
                domain->SetText(s.domain.c_str());
                password->SetText("");
                password->MakeFocus(true);
            }
            break;
        }
        case Save:
            if (!busy)
                SaveLogin();
            break;
        case Done: {
            busy = false;
            find->SetEnabled(true);
            const char* s;
            if (m->FindString("text", &s) == B_OK)
                status->SetText(s);
            Reload();
            break;
        }
        default:
            BWindow::MessageReceived(m);
        }
    }
};
class Application : public BApplication {
    Window* window = nullptr;

public:
    Application()
        : BApplication(rsmb::Signature)
    {
    }
    void ReadyToRun() override
    {
        if (!window)
            window = new Window;
    }
    void MessageReceived(BMessage* m) override
    {
        if (m->what == Configure) {
            if (!window)
                window = new Window;
            window->Activate();
        } else
            BApplication::MessageReceived(m);
    }
};
}
int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--auth") {
        BApplication helper("application/x-vnd.R-SMB-auth");
        auto a = rsmb::Auth(argv[2]);
        // libbe may print warnings to stdout first; the volume reads only the "auth" line.
        printf("auth %s %s %s\n", rsmb::Hex(a.user).c_str(), rsmb::Hex(a.password).c_str(),
            rsmb::Hex(a.domain).c_str());
        return 0;
    }
    if (argc >= 2) {
        std::string a = argv[1];
        if (a == "--is-mounted")
            return Mounted() ? 0 : 1;
        if (a == "--restore")
            return rsmb::Load().enabled ? MountVolume() : 0;
        if (a == "--mount")
            return MountVolume();
        if (a == "--unmount")
            return StopVolume();
        if (a == "--enable")
            return SetEnabled(true);
        if (a == "--installed") {
            // Package post-install: turn on for a first install, keep the user's choice on upgrade.
            struct stat st { };
            if (stat(rsmb::ConfigPath().c_str(), &st) != 0)
                return SetEnabled(true);
            return rsmb::Load().enabled ? MountVolume() : 0;
        }
        if (a == "--disable")
            return SetEnabled(false);
        if (a == "--add-server" && argc == 3) {
            auto l = smb::Location::Parse(argv[2]);
            auto s = rsmb::Load();
            s.servers.push_back({ l.host, "", "", l.share });
            return rsmb::Save(s);
        }
    }
    Application app;
    app.Run();
    return 0;
}
