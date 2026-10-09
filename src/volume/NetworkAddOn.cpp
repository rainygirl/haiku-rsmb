// C++98: Network preferences uses the primary ABI on hybrid Haiku.
#include <NetworkSettingsAddOn.h>
#include "Paths.h"
#include "Strings.h"
#include <Alert.h>
#include <Button.h>
#include <ControlLook.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Message.h>
#include <Messenger.h>
#include <Roster.h>
#include <StringView.h>
#include <TextView.h>
#include <errno.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
using namespace BNetworkKit;
extern char** environ;
static const uint32 kToggle = 'rtgl', kToggled = 'rtgd', kSettings = 'rcfg';
static bool Mounted()
{
    struct stat a, b;
    return stat("/R SMB Network", &a) == 0 && stat("/", &b) == 0 && a.st_dev != b.st_dev;
}
// Same look as the system service items: name on the left, on/off on the right.
class StatusItem : public BStringItem {
public:
    bool fEnabled;
    StatusItem()
        : BStringItem("R SMB"), fEnabled(Mounted())
    {
    }
    void DrawItem(BView* owner, BRect frame, bool complete)
    {
        owner->PushState();
        rgb_color low = owner->LowColor();
        if (IsSelected() || complete) {
            owner->SetHighColor(IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR) : low);
            owner->FillRect(frame);
        }
        font_height fh;
        owner->GetFontHeight(&fh);
        float y = frame.top + (frame.Height() + fh.ascent - fh.descent) / 2;
        float space = be_control_look->DefaultLabelSpacing();
        rgb_color text = ui_color(IsSelected() ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
        owner->SetDrawingMode(B_OP_OVER);
        owner->SetHighColor(text);
        owner->DrawString(Text(), BPoint(frame.left + space, y));
        const char* state = rsmb::T(fEnabled ? "on" : "off");
        if (!fEnabled)
            owner->SetHighColor(tint_color(text,
                text.red + text.green + text.blue > 128 * 3 ? B_DARKEN_1_TINT : B_LIGHTEN_1_TINT));
        owner->DrawString(state, BPoint(frame.right - owner->StringWidth(state) - space, y));
        owner->PopState();
    }
};
struct Job {
    BMessenger target;
    bool enable;
};
static int32 RunToggle(void* data)
{
    Job* job = (Job*)data;
    const char* program = rsmb::ProgramPath();
    const char* args[] = { program, job->enable ? "--enable" : "--disable", NULL };
    pid_t pid;
    int status = 1;
    if (posix_spawn(&pid, program, NULL, NULL, (char* const*)args, environ) == 0) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    BMessage done(kToggled);
    done.AddBool("enable", job->enable);
    done.AddInt32("status", status);
    job->target.SendMessage(&done);
    delete job;
    return 0;
}
class SettingsView : public BView {
    StatusItem* fItem;
    BStringView* fStatus;
    BButton* fSettings;
    BButton* fToggle;
    BListView* fList;

    void Update()
    {
        fItem->fEnabled = Mounted();
        fToggle->SetLabel(fItem->fEnabled ? rsmb::T("Disable") : rsmb::T("Enable"));
        BListView* list = fList;
        if (list != NULL && list->IndexOf(fItem) >= 0)
            list->InvalidateItem(list->IndexOf(fItem));
    }

public:
    SettingsView(StatusItem* item)
        : BView("R SMB", 0), fItem(item), fList(NULL)
    {
        SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
        BStringView* title = new BStringView("title", "R SMB");
        title->SetFont(be_bold_font);
        title->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
        BTextView* description = new BTextView("description");
        description->SetText(rsmb::T("Shared folders of Windows PCs and NAS devices appear in the "
                                     "R SMB volume on the Desktop. Open them in Tracker like local folders."));
        description->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
        description->MakeEditable(false);
        description->MakeSelectable(false);
        fStatus = new BStringView("status", "");
        fSettings = new BButton("settings", rsmb::T("Settings…"), new BMessage(kSettings));
        fToggle = new BButton("toggler", rsmb::T("Enable"), new BMessage(kToggle));
        BLayoutBuilder::Group<>(this, B_VERTICAL)
            .Add(title)
            .Add(description)
            .Add(fStatus)
            .AddGlue()
            .AddGroup(B_HORIZONTAL)
                .AddGlue()
                .Add(fSettings)
                .Add(fToggle)
            .End();
        SetExplicitMinSize(BSize(200, B_SIZE_UNSET));
    }
    void AttachedToWindow()
    {
        fSettings->SetTarget(this);
        fToggle->SetTarget(this);
        fList = NULL;
        for (BView* v = Window() ? Window()->ChildAt(0) : NULL; v != NULL;) {
            // The service list is the window's only BListView.
            BListView* l = dynamic_cast<BListView*>(v);
            if (l != NULL) {
                fList = l;
                break;
            }
            if (v->ChildAt(0) != NULL)
                v = v->ChildAt(0);
            else {
                while (v != NULL && v->NextSibling() == NULL)
                    v = v->Parent();
                if (v != NULL)
                    v = v->NextSibling();
            }
        }
        Update();
    }
    void MessageReceived(BMessage* m)
    {
        switch (m->what) {
        case kSettings: {
            entry_ref ref;
            status_t e = get_ref_for_path(rsmb::ProgramPath(), &ref);
            if (e == B_OK)
                e = be_roster->Launch(&ref, m);
            if (e != B_OK && e != B_ALREADY_RUNNING)
                fStatus->SetText(rsmb::T("R SMB could not be started. Reinstall R SMB."));
            break;
        }
        case kToggle: {
            Job* job = new Job;
            job->target = BMessenger(this);
            job->enable = !Mounted();
            fToggle->SetEnabled(false);
            fStatus->SetText(job->enable ? rsmb::T("Connecting…") : rsmb::T("Disconnecting…"));
            thread_id t = spawn_thread(RunToggle, "rsmb toggle", B_NORMAL_PRIORITY, job);
            if (t < 0 || resume_thread(t) != B_OK) {
                delete job;
                fToggle->SetEnabled(true);
                fStatus->SetText("");
            }
            break;
        }
        case kToggled: {
            bool enable = false;
            int32 status = 1;
            m->FindBool("enable", &enable);
            m->FindInt32("status", &status);
            fToggle->SetEnabled(true);
            if (status == 0)
                fStatus->SetText("");
            else
                fStatus->SetText(enable ? rsmb::T("Could not connect. Reinstall R SMB if this persists.")
                                        : rsmb::T("In use. Close R SMB files and windows, then try again."));
            Update();
            break;
        }
        default:
            BView::MessageReceived(m);
        }
    }
};
class SettingsItem : public BNetworkSettingsItem {
    StatusItem* item;
    SettingsView* view;

public:
    SettingsItem()
    {
        item = new StatusItem;
        view = new SettingsView(item);
    }
    BNetworkSettingsType Type() const
    {
        return B_NETWORK_SETTINGS_TYPE_SERVICE;
    }
    BListItem* ListItem()
    {
        return item;
    }
    BView* View()
    {
        return view;
    }
    bool IsRevertable()
    {
        return false;
    }
    status_t Revert()
    {
        return B_OK;
    }
};
class AddOn : public BNetworkSettingsAddOn {
public:
    AddOn(image_id i, BNetworkSettings& s)
        : BNetworkSettingsAddOn(i, s)
    {
    }
    BNetworkSettingsItem* CreateNextItem(uint32& c)
    {
        if (c++ == 0)
            return new SettingsItem;
        return NULL;
    }
};
extern "C" BNetworkSettingsAddOn* instantiate_network_settings_add_on(image_id i, BNetworkSettings& s)
{
    return new AddOn(i, s);
}
