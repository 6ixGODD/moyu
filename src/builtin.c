#include "builtin.h"

#include "agent.h"
#include "cJSON.h"
#include "loop.h"
#include "memory.h"
#include "mem.h"
#include "platform.h"
#include "state.h"
#include "tool.h"
#include "workdir.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static char* json_ok(const char* detail) {
  cJSON* o=cJSON_CreateObject();cJSON_AddBoolToObject(o,"ok",1);
  if(detail)cJSON_AddStringToObject(o,"detail",detail);char* s=cJSON_PrintUnformatted(o);cJSON_Delete(o);return s;
}

static char* arg_string(const char* json,const char* key){
  cJSON* r=cJSON_Parse(json?json:"{}");if(!r)return NULL;cJSON* v=cJSON_GetObjectItem(r,key);
  char* out=(v&&cJSON_IsString(v))?moyu_strdup(v->valuestring):NULL;cJSON_Delete(r);return out;
}

static void safe_name(const char* in,char* out,size_t cap){
  size_t j=0;for(size_t i=0;in&&in[i]&&j+1<cap;i++){unsigned char c=(unsigned char)in[i];
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_')out[j++]=(char)c;else if(c==' ')out[j++]='_';}
  if(j==0){strncpy(out,"untitled",cap-1);j=strlen(out);}out[j]=0;
}

static char* system_now(const char* input,void* user){
  (void)input;(void)user;time_t t=time(NULL);struct tm tmv;
#ifdef _WIN32
  localtime_s(&tmv,&t);
#else
  localtime_r(&t,&tmv);
#endif
  char stamp[64];strftime(stamp,sizeof(stamp),"%Y-%m-%d %H:%M:%S %z",&tmv);
  cJSON* o=cJSON_CreateObject();cJSON_AddStringToObject(o,"local",stamp);cJSON_AddNumberToObject(o,"unix_ms",(double)platform_unix_ms());
  char* s=cJSON_PrintUnformatted(o);cJSON_Delete(o);return s;
}

static char* system_idle(const char* input,void* user){
  (void)input;(void)user;
#ifdef _WIN32
  LASTINPUTINFO li={sizeof(li)};GetLastInputInfo(&li);DWORD idle=GetTickCount()-li.dwTime;char out[96];snprintf(out,sizeof(out),"{\"idle_seconds\":%.3f}",(double)idle/1000.0);return moyu_strdup(out);
#else
  return moyu_strdup("{\"idle_seconds\":0}");
#endif
}

static char* foreground_app(const char* input,void* user){
  (void)input;moyu_app* app=(moyu_app*)user;if(!state_permission_allowed(app->state,"system.foreground_app","*","observe"))return moyu_strdup("{\"ok\":false,\"error\":\"permission required\"}");
#ifdef _WIN32
  HWND h=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(h,&pid);HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);wchar_t path[32768];DWORD n=32768;char utf8[32768]="";if(p&&QueryFullProcessImageNameW(p,0,path,&n))WideCharToMultiByte(CP_UTF8,0,path,-1,utf8,sizeof(utf8),NULL,NULL);if(p)CloseHandle(p);cJSON* o=cJSON_CreateObject();cJSON_AddStringToObject(o,"process",utf8);char* out=cJSON_PrintUnformatted(o);cJSON_Delete(o);return out;
#else
  return moyu_strdup("{\"process\":\"\"}");
#endif
}

static char* memory_remember_tool(const char* input,void* user){
  moyu_app* app=(moyu_app*)user;char* text=arg_string(input,"text");char* section=arg_string(input,"section");
  bool ok=text&&memory_remember(app->memory,section?section:"## Important episodes",text);
  if(text)moyu_free(text);if(section)moyu_free(section);return ok?json_ok("remembered"):moyu_strdup("{\"ok\":false,\"error\":\"not stored\"}");
}

static char* memory_forget_tool(const char* input,void* user){
  moyu_app* app=(moyu_app*)user;char* q=arg_string(input,"query");int n=q?memory_forget(app->memory,q):0;if(q)moyu_free(q);
  char out[64];snprintf(out,sizeof(out),"{\"ok\":true,\"removed\":%d}",n);return moyu_strdup(out);
}

bool builtin_collection_add_note(moyu_app* app,
                                 const char* title,
                                 const char* body,
                                 const char* source) {
  if(!app||!title||!body)return false;
  char name[160];safe_name(title,name,sizeof(name));strncat(name,".md",sizeof(name)-strlen(name)-1);char* path=platform_join_path(app->workdir->collections_dir,name);
  char doc[4096];snprintf(doc,sizeof(doc),"# %s\n\n%s\n",title,body);bool ok=platform_write_file_atomic(path,doc,strlen(doc));
  if(ok){
    char meta[512];snprintf(meta,sizeof(meta),"{\"source\":\"%s\"}",source?source:"manual");
    state_add_episode(app->state,"collection",title,meta,0.65,0.65);
    state_meta_set(app->state,"last_collection_title",title);
    state_meta_set(app->state,"last_collection_body",body);
    moyu_app_note_collection(app,title,body);
  }
  moyu_free(path);
  return ok;
}

static char* collection_add(const char* input,void* user){
  moyu_app* app=(moyu_app*)user;char* title=arg_string(input,"title");char* body=arg_string(input,"body");
  if(!title||!body){if(title)moyu_free(title);if(body)moyu_free(body);return moyu_strdup("{\"ok\":false,\"error\":\"title and body required\"}");}
  bool ok=builtin_collection_add_note(app,title,body,"tool");
  moyu_free(title);moyu_free(body);return ok?json_ok("collected"):moyu_strdup("{\"ok\":false}");
}

static char* draft_create(const char* input,void* user){
  moyu_app* app=(moyu_app*)user;char* title=arg_string(input,"title");char* body=arg_string(input,"body");
  if(!title||!body){if(title)moyu_free(title);if(body)moyu_free(body);return moyu_strdup("{\"ok\":false,\"error\":\"title and body required\"}");}
  char name[160];safe_name(title,name,sizeof(name));strncat(name,".md",sizeof(name)-strlen(name)-1);char* path=platform_join_path(app->workdir->drafts_dir,name);
  char* doc=(char*)moyu_alloc(strlen(title)+strlen(body)+8);sprintf(doc,"# %s\n\n%s\n",title,body);bool ok=platform_write_file_atomic(path,doc,strlen(doc));
  if(ok)moyu_app_emit_info(app,"Draft tucked away",title,3600);
  moyu_free(doc);moyu_free(path);moyu_free(title);moyu_free(body);return ok?json_ok("drafted"):moyu_strdup("{\"ok\":false}");
}

#ifdef _WIN32
static char* list_markdown_dir(const char* dir){char pattern[32768];snprintf(pattern,sizeof(pattern),"%s\\*.md",dir);wchar_t wp[32768];MultiByteToWideChar(CP_UTF8,0,pattern,-1,wp,32768);WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileW(wp,&fd);cJSON* root=cJSON_CreateObject();cJSON* a=cJSON_AddArrayToObject(root,"items");if(h!=INVALID_HANDLE_VALUE){do{char name[1024];WideCharToMultiByte(CP_UTF8,0,fd.cFileName,-1,name,sizeof(name),NULL,NULL);cJSON_AddItemToArray(a,cJSON_CreateString(name));}while(FindNextFileW(h,&fd));FindClose(h);}char* out=cJSON_PrintUnformatted(root);cJSON_Delete(root);return out;}
static char* remove_own_file(const char* input,const char* dir){char* name=arg_string(input,"name");if(!name)return moyu_strdup("{\"ok\":false}");char safe[160];safe_name(name,safe,sizeof(safe));if(!strstr(safe,".md"))strncat(safe,".md",sizeof(safe)-strlen(safe)-1);char* path=platform_join_path(dir,safe);bool ok=platform_remove_file(path);moyu_free(path);moyu_free(name);return ok?json_ok("removed"):moyu_strdup("{\"ok\":false}");}
#else
static char* list_markdown_dir(const char* dir){(void)dir;return moyu_strdup("{\"items\":[]}");}
static char* remove_own_file(const char* input,const char* dir){(void)input;(void)dir;return moyu_strdup("{\"ok\":false}");}
#endif
static char* collection_list(const char* i,void* u){(void)i;return list_markdown_dir(((moyu_app*)u)->workdir->collections_dir);}
static char* collection_remove(const char* i,void* u){return remove_own_file(i,((moyu_app*)u)->workdir->collections_dir);}
static char* draft_list(const char* i,void* u){(void)i;return list_markdown_dir(((moyu_app*)u)->workdir->drafts_dir);}
static char* draft_remove(const char* i,void* u){return remove_own_file(i,((moyu_app*)u)->workdir->drafts_dir);}
static char* runtime_explain(const char* i,void* u){(void)i;return agent_explain(((moyu_app*)u)->agent);}
static char* runtime_cancel(const char* i,void* u){(void)i;agent_cancel(((moyu_app*)u)->agent,"cancelled through tool");return json_ok("cancelled");}

#ifdef _WIN32
static bool path_is_within(const char* root,const char* candidate){
  wchar_t wr[32768],wc[32768],fr[32768],fc[32768];
  MultiByteToWideChar(CP_UTF8,0,root,-1,wr,32768);MultiByteToWideChar(CP_UTF8,0,candidate,-1,wc,32768);
  HANDLE hr=CreateFileW(wr,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);HANDLE hc=CreateFileW(wc,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
  if(hr!=INVALID_HANDLE_VALUE&&hc!=INVALID_HANDLE_VALUE){DWORD rn=GetFinalPathNameByHandleW(hr,fr,32768,FILE_NAME_NORMALIZED);DWORD cn=GetFinalPathNameByHandleW(hc,fc,32768,FILE_NAME_NORMALIZED);CloseHandle(hr);CloseHandle(hc);if(!rn||rn>=32768||!cn||cn>=32768)return false;}
  else{if(hr!=INVALID_HANDLE_VALUE)CloseHandle(hr);if(hc!=INVALID_HANDLE_VALUE)CloseHandle(hc);if(!GetFullPathNameW(wr,32768,fr,NULL)||!GetFullPathNameW(wc,32768,fc,NULL))return false;}
  size_t n=wcslen(fr);return _wcsnicmp(fr,fc,n)==0&&(fc[n]==0||fc[n]=='\\'||fc[n]=='/');
}

static char* filesystem_observe(const char* input,void* user){
  moyu_app* app=(moyu_app*)user;char* root=arg_string(input,"root");
  if(!root&&app->observe_root)root=moyu_strdup(app->observe_root);
  if(!root||!app->observe_root||!path_is_within(app->observe_root,root)||
     !state_permission_allowed(app->state,"filesystem.observe",app->observe_root,"observe")){
    if(root)moyu_free(root);return moyu_strdup("{\"ok\":false,\"error\":\"scope not authorized\"}");}
  char pattern[32768];snprintf(pattern,sizeof(pattern),"%s\\*",root);wchar_t wp[32768];MultiByteToWideChar(CP_UTF8,0,pattern,-1,wp,32768);
  WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileW(wp,&fd);cJSON* o=cJSON_CreateObject();cJSON* items=cJSON_AddArrayToObject(o,"items");int count=0;
  if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(fd.cFileName,L".")||!wcscmp(fd.cFileName,L".."))continue;char name[1024];WideCharToMultiByte(CP_UTF8,0,fd.cFileName,-1,name,sizeof(name),NULL,NULL);
      cJSON* it=cJSON_CreateObject();cJSON_AddStringToObject(it,"name",name);cJSON_AddBoolToObject(it,"directory",(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0);
      ULARGE_INTEGER sz;sz.HighPart=fd.nFileSizeHigh;sz.LowPart=fd.nFileSizeLow;cJSON_AddNumberToObject(it,"size",(double)sz.QuadPart);cJSON_AddItemToArray(items,it);
    }while(++count<200&&FindNextFileW(h,&fd));FindClose(h);}cJSON_AddStringToObject(o,"root",root);cJSON_AddNumberToObject(o,"count",count);
  char* out=cJSON_PrintUnformatted(o);cJSON_Delete(o);moyu_free(root);return out;
}
#else
static char* filesystem_observe(const char* input,void* user){(void)input;(void)user;return moyu_strdup("{\"ok\":false,\"error\":\"Windows only\"}");}
#endif

static void add(moyu_app* app,const char* name,const char* desc,const char* schema,tool_risk risk,const char* aff,tool_fn fn){
  tool_def d={.name=name,.description=desc,.input_schema_json=schema,.source="builtin",.affordance=aff,.risk=risk,.invoke=fn,.user=app};tool_registry_add(app->tools,d);
}

void builtin_register_persistent(moyu_app* app){
  add(app,"system.now","Read local date and time.","{\"type\":\"object\"}",TOOL_OBSERVE,"{\"domain\":\"system\",\"senses\":[\"time\"]}",system_now);
  add(app,"system.idle_time","Read user idle time.","{\"type\":\"object\"}",TOOL_OBSERVE,"{\"domain\":\"system\",\"senses\":[\"idle\"]}",system_idle);
  add(app,"system.foreground_app","Read the foreground process name when authorized.","{\"type\":\"object\"}",TOOL_OBSERVE,"{\"domain\":\"system\",\"senses\":[\"activity\"]}",foreground_app);
  add(app,"memory.remember","Store a human-readable long-term memory.","{\"type\":\"object\",\"required\":[\"text\"]}",TOOL_DRAFT,"{\"domain\":\"memory\",\"actions\":[\"remember\"]}",memory_remember_tool);
  add(app,"memory.forget","Forget matching long-term memories.","{\"type\":\"object\",\"required\":[\"query\"]}",TOOL_MUTATE,"{\"domain\":\"memory\",\"actions\":[\"forget\"]}",memory_forget_tool);
  add(app,"collection.add","Add a discovery to MOYU's collection.","{\"type\":\"object\",\"required\":[\"title\",\"body\"]}",TOOL_DRAFT,"{\"domain\":\"collection\",\"actions\":[\"collect\"]}",collection_add);
  add(app,"collection.list","List MOYU's collection.","{\"type\":\"object\"}",TOOL_OBSERVE,"{\"domain\":\"collection\"}",collection_list);
  add(app,"collection.remove","Remove one collection item.","{\"type\":\"object\",\"required\":[\"name\"]}",TOOL_MUTATE,"{\"domain\":\"collection\"}",collection_remove);
  add(app,"draft.create","Create a private draft in MOYU's home.","{\"type\":\"object\",\"required\":[\"title\",\"body\"]}",TOOL_DRAFT,"{\"domain\":\"notes\",\"actions\":[\"draft\"]}",draft_create);
  add(app,"draft.list","List private drafts.","{\"type\":\"object\"}",TOOL_OBSERVE,"{\"domain\":\"notes\"}",draft_list);
  add(app,"draft.remove","Remove a private draft.","{\"type\":\"object\",\"required\":[\"name\"]}",TOOL_MUTATE,"{\"domain\":\"notes\"}",draft_remove);
  add(app,"runtime.explain","Explain the active intention.","{\"type\":\"object\"}",TOOL_OBSERVE,"{\"domain\":\"runtime\"}",runtime_explain);
  add(app,"runtime.cancel_intention","Cancel the active intention.","{\"type\":\"object\"}",TOOL_MUTATE,"{\"domain\":\"runtime\"}",runtime_cancel);
  add(app,"filesystem.observe","Observe metadata in an authorized root.","{\"type\":\"object\",\"properties\":{\"root\":{\"type\":\"string\"}}}",TOOL_OBSERVE,"{\"domain\":\"files\",\"senses\":[\"recency\",\"anomaly\"]}",filesystem_observe);
}
