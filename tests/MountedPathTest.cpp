// Use a dedicated writable share: exercises the actual kernel-mounted paths.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <thread>
static void check(bool ok,const char* step){if(!ok){perror(step);exit(1);}}
static void put(const std::string& p,const std::vector<char>& b){int f=open(p.c_str(),O_CREAT|O_TRUNC|O_WRONLY,0600);check(f>=0,"create");size_t n=0;while(n<b.size()){ssize_t r=write(f,b.data()+n,b.size()-n);check(r>0,"write");n+=r;}check(fsync(f)==0,"fsync");check(close(f)==0,"close");}
static std::vector<char> get(const std::string& p){int f=open(p.c_str(),O_RDONLY);check(f>=0,"open");std::vector<char>b;char x[8192];ssize_t n;while((n=read(f,x,sizeof x))>0)b.insert(b.end(),x,x+n);check(n==0,"read");close(f);return b;}
int main(int argc,char**argv){if(argc!=2)return 2;std::string root=std::string(argv[1])+"/rsmb-arm64-test-"+std::to_string(getpid());check(mkdir(root.c_str(),0700)==0,"mkdir");auto folder=root+"/폴더 with spaces";check(mkdir(folder.c_str(),0700)==0,"Unicode mkdir");auto p=folder+"/한글.txt";std::vector<char>b(262912);for(size_t i=0;i<b.size();i++)b[i]=char(i);put(p,b);check(get(p)==b,"readback");int f=open(p.c_str(),O_RDWR);check(f>=0,"rw open");check(lseek(f,65530,SEEK_SET)==65530,"seek");check(write(f,"random",6)==6,"random write");check(ftruncate(f,131071)==0,"truncate");check(fsync(f)==0,"sync");close(f);auto expected=b;memcpy(expected.data()+65530,"random",6);expected.resize(131071);check(get(p)==expected,"seek/truncate readback");f=open(p.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);check(f<0&&errno==EEXIST,"exclusive create");auto renamed=folder+"/renamed.txt";check(rename(p.c_str(),renamed.c_str())==0,"rename");check(get(renamed)==expected,"rename readback");auto tmp=folder+"/save.tmp";std::vector<char>small={'R',' ','S','M','B','\n'};put(tmp,small);check(rename(tmp.c_str(),renamed.c_str())==0,"atomic replacement");check(get(renamed)==small,"atomic readback");std::string local="/tmp/rsmb-copy-"+std::to_string(getpid());put(local,get(renamed));auto copy=folder+"/copy.txt";put(copy,get(local));check(get(copy)==small,"copy roundtrip");unlink(local.c_str());check(rmdir(folder.c_str())<0&&(errno==ENOTEMPTY||errno==EEXIST),"nonempty rmdir");std::vector<std::thread>threads;for(int i=0;i<4;i++)threads.emplace_back([&,i]{auto q=root+"/parallel-"+std::to_string(i);std::vector<char>d(140001,char(i));put(q,d);check(get(q)==d,"concurrent readback");check(unlink(q.c_str())==0,"concurrent delete");});for(auto&t:threads)t.join();check(unlink(copy.c_str())==0,"delete copy");check(unlink(renamed.c_str())==0,"delete");check(rmdir(folder.c_str())==0,"rmdir");check(rmdir(root.c_str())==0,"cleanup");puts("PASS: mounted Unicode paths, read/write/seek/truncate/fsync, exclusive create, rename, atomic replacement, copy roundtrip, concurrency, deletion");}
