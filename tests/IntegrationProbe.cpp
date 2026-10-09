#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <Roster.h>
#include <Entry.h>
#include <cstdio>
int main(int argc,char**argv){
 BApplication app("application/x-vnd.R-SMB-IntegrationProbe");
 if(argc==2){
  entry_ref ref; if(get_ref_for_path(argv[1],&ref)!=B_OK)return 2;
  BMessage msg(B_REFS_RECEIVED);msg.AddRef("refs",&ref);
  status_t e=BMessenger("application/x-vnd.Be-TRAK").SendMessage(&msg,(BHandler*)nullptr,2000000);
  printf("Tracker path delivery: %ld\n",(long)e);return e!=B_OK;
 }
 BMessage get(B_GET_PROPERTY),reply;
 get.AddSpecifier("Messenger");get.AddSpecifier("View","R SMB");get.AddSpecifier("View","add-on shell");get.AddSpecifier("Window",int32(0));
 status_t e=BMessenger("application/x-vnd.Haiku-Network").SendMessage(&get,&reply,2000000,2000000);
 BMessenger pane;if(e!=B_OK||reply.FindMessenger("result",&pane)!=B_OK){reply.PrintToStream();return 3;}
 BMessage open('rcfg');e=pane.SendMessage(&open,(BHandler*)nullptr,2000000);
 printf("Network settings action delivery: %ld\n",(long)e);return e!=B_OK;
}
