// Verify real Haiku application integration, not the former standalone browser.
#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <Roster.h>
#include <Entry.h>
#include <cstdio>
int main(int argc,char** argv){
 BApplication app("application/x-vnd.R-SMB-VolumeProbe");
 if(argc!=2)return 2;
 entry_ref ref;if(get_ref_for_path(argv[1],&ref)!=B_OK)return 3;
 BMessage m(B_REFS_RECEIVED);m.AddRef("refs",&ref);
 status_t e=be_roster->Launch("application/x-vnd.Be-TRAK",&m);
 if(e!=B_OK&&e!=B_ALREADY_RUNNING)return 4;
 printf("PASS: Tracker accepted mounted path %s\n",argv[1]);return 0;
}
