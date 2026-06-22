#include "mcp.h"
#include "memory.h"
#include "mem.h"
#include "platform.h"
#include "state.h"
#include "sprite.h"
#include "tool.h"
#include "workdir.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); failures++; } } while(0)

static void unique_home(char* out,size_t cap){char temp[MAX_PATH];GetTempPathA(MAX_PATH,temp);snprintf(out,cap,"%smoyu-test-%lu-%llu",temp,GetCurrentProcessId(),(unsigned long long)platform_unix_ms());CreateDirectoryA(out,NULL);SetEnvironmentVariableA("MOYU_HOME",out);}

static void test_state_and_memory(void){
  char home[MAX_PATH];unique_home(home,sizeof(home));moyu_workdir wd;state_store state;memory_system memory;
  CHECK(workdir_init(&wd));CHECK(platform_file_exists(wd.soul_path));CHECK(platform_file_exists(wd.memory_path));CHECK(state_open(&state,wd.db_path));CHECK(memory_init(&memory,&wd,&state));
  CHECK(memory_remember(&memory,"## About the human","The human likes quiet mornings."));CHECK(!memory_remember(&memory,"## About the human","The human likes quiet mornings."));CHECK(memory_forget(&memory,"quiet mornings")==1);
  CHECK(!memory_remember(&memory,"## About the human","api_key=secret"));
  CHECK(state_upsert_belief(&state,"test project","looks","forgotten",0.8,"test"));char* context=state_relevant_context(&state,8);CHECK(context&&strstr(context,"test project"));moyu_free(context);
  int64_t id=state_create_intention(&state,"inspect a clue","test",3,platform_unix_ms()+30000);CHECK(id>0);state_intention in;CHECK(state_load_active_intention(&state,&in));CHECK(in.id==id);CHECK(state_update_intention(&state,id,"completed",1,"done"));CHECK(!state_load_active_intention(&state,&in));
  CHECK(state_permission_set(&state,"filesystem.observe","C:\\safe","allow",true));CHECK(state_permission_allowed(&state,"filesystem.observe","C:\\safe","observe"));CHECK(!state_permission_allowed(&state,"filesystem.observe","C:\\safe","mutate"));
  CHECK(state_budget_take(&state,"test",2,1));CHECK(state_budget_take(&state,"test",2,1));CHECK(!state_budget_take(&state,"test",2,1));
  memory_free(&memory);state_close(&state);
  char* bad=platform_join_path(wd.root,"bad.db");CHECK(platform_write_file(bad,"not a database",14));state_store recovered;CHECK(state_open(&recovered,bad));char* version=state_meta_get(&recovered,"schema_version");CHECK(version&&strcmp(version,"1")==0);if(version)moyu_free(version);state_close(&recovered);moyu_free(bad);
  workdir_free(&wd);SetEnvironmentVariableA("MOYU_HOME",NULL);
}

static void test_mcp_stdio(void){
  char* exe=platform_join_path(platform_exe_dir(),"moyu-mcp-git.exe");char cmd[4096];snprintf(cmd,sizeof(cmd),"\"%s\"",exe);mcp_client client;mcp_client_init_stdio(&client,"git-test",cmd,NULL);CHECK(mcp_discover(&client));CHECK(client.tool_count==3);char* result=mcp_call(&client,"git.recent_commits","{\"root\":\".\"}");CHECK(result!=NULL);if(result)moyu_free(result);mcp_client_free(&client);moyu_free(exe);
}

static void test_mcp_http(void){
  char* exe=platform_join_path(platform_exe_dir(),"moyu-mcp-weather.exe");wchar_t wexe[32768];MultiByteToWideChar(CP_UTF8,0,exe,-1,wexe,32768);STARTUPINFOW si={0};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi={0};
  CHECK(CreateProcessW(wexe,NULL,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi));if(pi.hProcess){Sleep(250);mcp_client c;mcp_client_init_http(&c,"weather-test","http://127.0.0.1:7788/mcp",NULL);CHECK(mcp_discover(&c));CHECK(c.tool_count==1);CHECK(c.session_id!=NULL);mcp_client_free(&c);TerminateProcess(pi.hProcess,0);WaitForSingleObject(pi.hProcess,2000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}moyu_free(exe);
}

static void test_unicode_text_raster(void){
  platform_text_bitmap bitmap={0};
  CHECK(platform_render_text("气泡文本正常",14,180,0x302A26FFu,&bitmap));
  CHECK(bitmap.pixels!=NULL);CHECK(bitmap.w>20);CHECK(bitmap.h>=14);
  int visible=0;for(int i=0;i<bitmap.w*bitmap.h;i++)if((bitmap.pixels[i]&0xff)!=0)visible++;
  CHECK(visible>20);platform_text_bitmap_free(&bitmap);
  CHECK(!platform_render_text("\xFF",14,180,0xFFFFFFFFu,&bitmap));
}

static bool dump_skin(const char* path){skin sk;skin_init_default(&sk);int scale=4,w=sk.sheet.frame_w*scale,h=sk.sheet.frame_h*scale,row=(w*3+3)&~3;size_t bytes=54+(size_t)row*h;unsigned char* data=(unsigned char*)calloc(1,bytes);if(!data)return false;data[0]='B';data[1]='M';*(unsigned int*)(data+2)=(unsigned int)bytes;*(unsigned int*)(data+10)=54;*(unsigned int*)(data+14)=40;*(int*)(data+18)=w;*(int*)(data+22)=h;*(unsigned short*)(data+26)=1;*(unsigned short*)(data+28)=24;*(unsigned int*)(data+34)=(unsigned int)(row*h);const uint32_t* px=sprite_frame(&sk.sheet,0);for(int y=0;y<h;y++)for(int x=0;x<w;x++){uint32_t p=px[(y/scale)*sk.sheet.frame_w+x/scale];unsigned char a=(unsigned char)p;unsigned char r=(p>>24)&255,g=(p>>16)&255,b=(p>>8)&255;unsigned char bg=((x/16+y/16)&1)?238:255;if(!a)r=g=b=bg;size_t off=54+(size_t)(h-1-y)*row+x*3;data[off]=b;data[off+1]=g;data[off+2]=r;}FILE* f=fopen(path,"wb");bool ok=f&&fwrite(data,1,bytes,f)==bytes;if(f)fclose(f);free(data);skin_free(&sk);return ok;}

int main(int argc,char** argv){if(argc==3&&!strcmp(argv[1],"--dump-skin"))return dump_skin(argv[2])?0:1;test_state_and_memory();test_mcp_stdio();test_mcp_http();test_unicode_text_raster();if(failures){fprintf(stderr,"%d test(s) failed\n",failures);return 1;}printf("all tests passed\n");return 0;}
