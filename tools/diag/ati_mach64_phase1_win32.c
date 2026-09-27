#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif
#define ATIE1_MAGIC 0x31495441ul
#define ATIE1_PASS 0x000000fful
struct atie1_result { DWORD magic,status,pci_address,command_status;
 DWORD revision_class,bar2,chip_id,config_stat0,gui_stat,scratch_before;
 DWORD pattern_read,scratch_after; };
static void hex(char *text,DWORD value){static const char d[]="0123456789ABCDEF";int i;text[0]='0';text[1]='x';for(i=0;i<8;++i)text[2+i]=d[(value>>((7-i)*4))&15u];text[10]='\0';}
static void line(HANDLE f,const char *k,const char *v){DWORD w;WriteFile(f,k,(DWORD)lstrlenA(k),&w,0);WriteFile(f,"=",1,&w,0);WriteFile(f,v,(DWORD)lstrlenA(v),&w,0);WriteFile(f,"\r\n",2,&w,0);}
static void hexline(HANDLE f,const char *k,DWORD v){char t[11];hex(t,v);line(f,k,t);}
void WINAPI V9xAtiMach64Phase1Entry(void){struct atie1_result r;HANDLE d,f;DWORD n=0ul,pass;char h[]="[AtiMach64Phase1]\r\n";
 d=CreateFileA("\\\\.\\ATIEN.VXD",0,0,0,CREATE_NEW,FILE_FLAG_DELETE_ON_CLOSE,0);if(d==INVALID_HANDLE_VALUE)ExitProcess(2u);
 if(!DeviceIoControl(d,1u,0,0,&r,sizeof(r),&n,0)||n!=sizeof(r)||r.magic!=ATIE1_MAGIC){CloseHandle(d);ExitProcess(3u);}CloseHandle(d);
 CreateDirectoryA("C:\\V9XDIAG",0);f=CreateFileA("C:\\V9XDIAG\\ATIENG1.TXT",GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);if(f==INVALID_HANDLE_VALUE)ExitProcess(4u);
 WriteFile(f,h,(DWORD)lstrlenA(h),&n,0);line(f,"Build",V9X_BUILD_ID);line(f,"Operation","SCRATCH_REG0-write-read-restore");hexline(f,"Status",r.status);hexline(f,"PciConfigAddress",r.pci_address);hexline(f,"PciCommandStatus",r.command_status);hexline(f,"PciRevisionClass",r.revision_class);hexline(f,"PciBar2",r.bar2);hexline(f,"ConfigChipId",r.chip_id);hexline(f,"ConfigStat0",r.config_stat0);hexline(f,"GuiStatBefore",r.gui_stat);hexline(f,"ScratchBefore",r.scratch_before);hexline(f,"PatternRead",r.pattern_read);hexline(f,"ScratchAfter",r.scratch_after);
 pass=r.status==ATIE1_PASS&&r.pattern_read==0x55555555ul&&r.scratch_after==r.scratch_before;line(f,"Result",pass?"PASS":"REVIEW");CloseHandle(f);ExitProcess(pass?0u:1u);}
