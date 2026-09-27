#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif
#define ATI2_MAGIC 0x32495441ul
#define ATI2_PASS_STATUS 0x00001ffful
struct ati2_result { DWORD magic,status,command_status,revision_class,bar0,bar2;
 DWORD chip_id,config_stat0,gui_before,gui_after,mem_before,mem_after;
 DWORD target_offset,dst_off_pitch,color,interior_mismatch,guard_mismatch;
 DWORD restore_mismatch,fifo_sample,loop_count; };
static void hex(char *t,DWORD v){static const char d[]="0123456789ABCDEF";int i;t[0]='0';t[1]='x';for(i=0;i<8;++i)t[2+i]=d[(v>>((7-i)*4))&15u];t[10]='\0';}
static void dec(char *t,DWORD v){char r[11];int n=0,i=0;if(v==0){t[0]='0';t[1]='\0';return;}while(v){r[n++]=(char)('0'+v%10);v/=10;}while(n)t[i++]=r[--n];t[i]='\0';}
static void line(HANDLE f,const char*k,const char*v){DWORD w;WriteFile(f,k,(DWORD)lstrlenA(k),&w,0);WriteFile(f,"=",1,&w,0);WriteFile(f,v,(DWORD)lstrlenA(v),&w,0);WriteFile(f,"\r\n",2,&w,0);}
static void hx(HANDLE f,const char*k,DWORD v){char t[11];hex(t,v);line(f,k,t);}static void dn(HANDLE f,const char*k,DWORD v){char t[11];dec(t,v);line(f,k,t);}
void WINAPI V9xAtiMach64Phase2Entry(void){struct ati2_result r;HANDLE d,f;DWORD n=0,pass;char h[]="[AtiMach64Phase2Fill]\r\n";
 d=CreateFileA("\\\\.\\ATIEN.VXD",0,0,0,CREATE_NEW,FILE_FLAG_DELETE_ON_CLOSE,0);if(d==INVALID_HANDLE_VALUE)ExitProcess(2u);
 if(!DeviceIoControl(d,2u,0,0,&r,sizeof(r),&n,0)||n!=sizeof(r)||r.magic!=ATI2_MAGIC){CloseHandle(d);ExitProcess(3u);}CloseHandle(d);
 CreateDirectoryA("C:\\V9XDIAG",0);f=CreateFileA("C:\\V9XDIAG\\ATI2D0.TXT",GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);if(f==INVALID_HANDLE_VALUE)ExitProcess(4u);
 WriteFile(f,h,(DWORD)lstrlenA(h),&n,0);line(f,"Build",V9X_BUILD_ID);line(f,"Operation","1000-alternating-fills-plus-guarded-rgb565-and-z16-clears");hx(f,"Status",r.status);hx(f,"PciCommandStatus",r.command_status);hx(f,"PciRevisionClass",r.revision_class);hx(f,"PciBar0",r.bar0);hx(f,"PciBar2",r.bar2);hx(f,"ConfigChipId",r.chip_id);hx(f,"ConfigStat0",r.config_stat0);hx(f,"GuiStatBefore",r.gui_before);hx(f,"GuiStatAfter",r.gui_after);hx(f,"MemBufCntlBefore",r.mem_before);hx(f,"MemBufCntlAfterInvalidate",r.mem_after);hx(f,"TargetOffset",r.target_offset);hx(f,"DstOffPitch",r.dst_off_pitch);hx(f,"FinalClearValue",r.color);dn(f,"OperationCount",r.loop_count);dn(f,"InteriorMismatches",r.interior_mismatch);dn(f,"GuardMismatches",r.guard_mismatch);dn(f,"RestoreMismatches",r.restore_mismatch);hx(f,"FifoSample",r.fifo_sample);
 pass=r.status==ATI2_PASS_STATUS&&r.loop_count==1002ul&&r.color==0x0000fffful&&r.interior_mismatch==0ul&&r.guard_mismatch==0ul&&r.restore_mismatch==0ul;line(f,"Result",pass?"PASS":"REVIEW");CloseHandle(f);ExitProcess(pass?0u:1u);}
