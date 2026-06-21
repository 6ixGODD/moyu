#include "mcp.h"

#include "cJSON.h"
#include "log.h"
#include "mem.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define MCP_VERSION "2025-11-25"

typedef struct {
  mcp_client* client;
  char* tool_name;
} mcp_adapter;

static void init_common(mcp_client* c,const char* name){memset(c,0,sizeof(*c));c->name=moyu_strdup(name&&*name?name:"mcp");c->next_id=1;strcpy(c->protocol_version,MCP_VERSION);}

void mcp_client_init_http(mcp_client* c,const char* name,const char* url,const char* key){init_common(c,name);c->transport=MCP_TRANSPORT_HTTP;c->server_url=moyu_strdup(url?url:"");c->api_key=key&&*key?moyu_strdup(key):NULL;}
void mcp_client_init_stdio(mcp_client* c,const char* name,const char* cmd,const char* cwd){init_common(c,name);c->transport=MCP_TRANSPORT_STDIO;c->command_line=moyu_strdup(cmd?cmd:"");c->working_dir=cwd&&*cwd?moyu_strdup(cwd):NULL;}
void mcp_client_init(mcp_client* c,const char* url,const char* key){mcp_client_init_http(c,"legacy-http",url,key);}

static void tools_free(mcp_client* c){for(size_t i=0;i<c->tool_count;i++){moyu_free(c->tools[i].name);moyu_free(c->tools[i].description);moyu_free(c->tools[i].input_schema);}if(c->tools)moyu_free(c->tools);c->tools=NULL;c->tool_count=0;}

#ifdef _WIN32
static wchar_t* wide(const char* s){int n=MultiByteToWideChar(CP_UTF8,0,s?s:"",-1,NULL,0);wchar_t* w=(wchar_t*)moyu_alloc((size_t)n*sizeof(wchar_t));MultiByteToWideChar(CP_UTF8,0,s?s:"",-1,w,n);return w;}

static bool stdio_start(mcp_client* c){
  SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};HANDLE out_r=NULL,out_w=NULL,in_r=NULL,in_w=NULL;
  if(!CreatePipe(&out_r,&out_w,&sa,0)||!SetHandleInformation(out_r,HANDLE_FLAG_INHERIT,0)||!CreatePipe(&in_r,&in_w,&sa,0)||!SetHandleInformation(in_w,HANDLE_FLAG_INHERIT,0))return false;
  STARTUPINFOW si={0};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdInput=in_r;si.hStdOutput=out_w;si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION pi={0};wchar_t* cmd=wide(c->command_line);wchar_t* cwd=c->working_dir?wide(c->working_dir):NULL;
  BOOL ok=CreateProcessW(NULL,cmd,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,cwd,&si,&pi);moyu_free(cmd);if(cwd)moyu_free(cwd);CloseHandle(in_r);CloseHandle(out_w);
  if(!ok){CloseHandle(out_r);CloseHandle(in_w);LOGW("MCP stdio CreateProcess failed: %lu",GetLastError());return false;}
  c->process=pi.hProcess;c->thread=pi.hThread;c->stdin_write=in_w;c->stdout_read=out_r;return true;
}

static char* stdio_exchange(mcp_client* c,const char* json,int timeout_ms,bool expect_response){
  HANDLE in=(HANDLE)c->stdin_write,out=(HANDLE)c->stdout_read;DWORD wrote=0;size_t n=strlen(json);
  if(!WriteFile(in,json,(DWORD)n,&wrote,NULL)||wrote!=n||!WriteFile(in,"\n",1,&wrote,NULL))return NULL;
  if(!expect_response)return moyu_strdup("{}");uint64_t end=platform_now_ms()+(uint64_t)timeout_ms;size_t cap=4096,len=0;char* buf=(char*)moyu_alloc(cap);
  while(platform_now_ms()<end){DWORD avail=0;if(!PeekNamedPipe(out,NULL,0,NULL,&avail,NULL)){moyu_free(buf);return NULL;}if(!avail){if(WaitForSingleObject((HANDLE)c->process,0)==WAIT_OBJECT_0){moyu_free(buf);return NULL;}platform_sleep_ms(10);continue;}
    if(len+avail+1>cap){while(len+avail+1>cap)cap*=2;if(cap>4*1024*1024){moyu_free(buf);return NULL;}buf=(char*)moyu_realloc(buf,cap);}DWORD rd=0;if(!ReadFile(out,buf+len,avail,&rd,NULL)||!rd){moyu_free(buf);return NULL;}len+=rd;buf[len]=0;
    char* nl=strchr(buf,'\n');if(nl){*nl=0;return buf;}}
  moyu_free(buf);return NULL;
}
#else
static bool stdio_start(mcp_client* c){(void)c;return false;}
static char* stdio_exchange(mcp_client* c,const char* j,int t,bool e){(void)c;(void)j;(void)t;(void)e;return NULL;}
#endif

static char* sse_json(const char* body){
  if(!body)return NULL;const char* p=body;while((p=strstr(p,"data:"))!=NULL){p+=5;while(*p==' ')p++;const char* e=strchr(p,'\n');size_t n=e?(size_t)(e-p):strlen(p);while(n&&p[n-1]=='\r')n--;if(n&&(*p=='{'||*p=='[')){char* out=(char*)moyu_alloc(n+1);memcpy(out,p,n);out[n]=0;return out;}if(!e)break;p=e+1;}return moyu_strdup(body);
}

static char* http_exchange(mcp_client* c,const char* json,int timeout_ms,bool expect_response){
  char headers[1024];snprintf(headers,sizeof(headers),"Accept: application/json, text/event-stream\r\nMCP-Protocol-Version: %s%s%s",c->protocol_version,c->session_id?"\r\nMCP-Session-Id: ":"",c->session_id?c->session_id:"");
  platform_http_resp r=platform_http_request("POST",c->server_url,c->api_key?c->api_key:"",headers,json,"application/json",timeout_ms);
  LOGI("MCP HTTP response: status=%d type=%s session=%s bytes=%zu",
       r.status,
       r.content_type ? r.content_type : "",
       r.session_id ? r.session_id : "",
       r.body_len);
  if(r.session_id&&!c->session_id)c->session_id=moyu_strdup(r.session_id);char* out=NULL;
  if(!r.err&&(r.status==200||(!expect_response&&r.status==202)))out=expect_response?sse_json(r.body):moyu_strdup("{}");
  else LOGW("MCP HTTP %s: status=%d error=%s",c->name,r.status,r.err?r.err:"none");platform_http_resp_free(&r);return out;
}

static char* exchange(mcp_client* c,const char* json,int timeout,bool expect){return c->transport==MCP_TRANSPORT_STDIO?stdio_exchange(c,json,timeout,expect):http_exchange(c,json,timeout,expect);}

static char* rpc(mcp_client* c,const char* method,cJSON* params,bool notification){
  cJSON* root=cJSON_CreateObject();cJSON_AddStringToObject(root,"jsonrpc","2.0");if(!notification)cJSON_AddNumberToObject(root,"id",c->next_id++);cJSON_AddStringToObject(root,"method",method);if(params)cJSON_AddItemToObject(root,"params",params);char* req=cJSON_PrintUnformatted(root);cJSON_Delete(root);
  char* raw=exchange(c,req,30000,!notification);moyu_free(req);if(notification)return raw;if(!raw)return NULL;
  cJSON* response=cJSON_Parse(raw);if(!response){moyu_free(raw);return NULL;}cJSON* err=cJSON_GetObjectItem(response,"error");if(err){char* shown=cJSON_PrintUnformatted(err);LOGW("MCP %s error: %s",method,shown?shown:"unknown");if(shown)moyu_free(shown);cJSON_Delete(response);moyu_free(raw);return NULL;}cJSON_Delete(response);return raw;
}

bool mcp_connect(mcp_client* c){
  if(!c)return false;if(c->connected)return true;if(c->transport==MCP_TRANSPORT_STDIO&&!stdio_start(c))return false;
  cJSON* p=cJSON_CreateObject();cJSON_AddStringToObject(p,"protocolVersion",MCP_VERSION);cJSON_AddItemToObject(p,"capabilities",cJSON_CreateObject());cJSON* ci=cJSON_CreateObject();cJSON_AddStringToObject(ci,"name","moyu");cJSON_AddStringToObject(ci,"version","0.2.0");cJSON_AddItemToObject(p,"clientInfo",ci);
  char* raw=rpc(c,"initialize",p,false);if(!raw)return false;cJSON* root=cJSON_Parse(raw);cJSON* result=root?cJSON_GetObjectItem(root,"result"):NULL;cJSON* ver=result?cJSON_GetObjectItem(result,"protocolVersion"):NULL;if(ver&&cJSON_IsString(ver))snprintf(c->protocol_version,sizeof(c->protocol_version),"%s",ver->valuestring);if(root)cJSON_Delete(root);moyu_free(raw);
  raw=rpc(c,"notifications/initialized",cJSON_CreateObject(),true);if(raw)moyu_free(raw);c->connected=true;return true;
}

bool mcp_discover(mcp_client* c){
  if(!mcp_connect(c))return false;char* raw=rpc(c,"tools/list",cJSON_CreateObject(),false);if(!raw)return false;cJSON* root=cJSON_Parse(raw);cJSON* result=root?cJSON_GetObjectItem(root,"result"):NULL;cJSON* tools=result?cJSON_GetObjectItem(result,"tools"):NULL;if(!tools||!cJSON_IsArray(tools)){if(root)cJSON_Delete(root);moyu_free(raw);return false;}
  tools_free(c);c->tool_count=(size_t)cJSON_GetArraySize(tools);c->tools=(mcp_tool_info*)moyu_alloc(c->tool_count*sizeof(*c->tools));memset(c->tools,0,c->tool_count*sizeof(*c->tools));
  for(size_t i=0;i<c->tool_count;i++){cJSON* t=cJSON_GetArrayItem(tools,(int)i);cJSON* n=cJSON_GetObjectItem(t,"name");cJSON* d=cJSON_GetObjectItem(t,"description");cJSON* s=cJSON_GetObjectItem(t,"inputSchema");c->tools[i].name=moyu_strdup(n&&cJSON_IsString(n)?n->valuestring:"unnamed");c->tools[i].description=moyu_strdup(d&&cJSON_IsString(d)?d->valuestring:"");c->tools[i].input_schema=s?cJSON_PrintUnformatted(s):moyu_strdup("{}");}
  cJSON_Delete(root);moyu_free(raw);return true;
}

char* mcp_call(mcp_client* c,const char* name,const char* args_json){
  if(!mcp_connect(c))return NULL;cJSON* p=cJSON_CreateObject();cJSON_AddStringToObject(p,"name",name);cJSON* args=cJSON_Parse(args_json?args_json:"{}");if(!args)args=cJSON_CreateObject();cJSON_AddItemToObject(p,"arguments",args);char* raw=rpc(c,"tools/call",p,false);if(!raw)return NULL;
  cJSON* root=cJSON_Parse(raw);cJSON* result=root?cJSON_GetObjectItem(root,"result"):NULL;char* out=result?cJSON_PrintUnformatted(result):moyu_strdup(raw);if(root)cJSON_Delete(root);moyu_free(raw);return out;
}

static char* adapter_call(const char* input,void* user){mcp_adapter* a=(mcp_adapter*)user;return a?mcp_call(a->client,a->tool_name,input):NULL;}

bool mcp_register_tools(mcp_client* c,tool_registry* reg){
  if(!mcp_discover(c))return false;for(size_t i=0;i<c->tool_count;i++){mcp_adapter* a=(mcp_adapter*)moyu_alloc(sizeof(*a));a->client=c;a->tool_name=moyu_strdup(c->tools[i].name);const char* n=c->tools[i].name;tool_risk risk=(strstr(n,"publish")||strstr(n,"write")||strstr(n,"delete")||strstr(n,"remove"))?TOOL_MUTATE:(strstr(n,"draft")||strstr(n,"create"))?TOOL_DRAFT:TOOL_OBSERVE;const char* domain=strchr(n,'.')?"{\"domain\":\"external\",\"senses\":[\"tool\"]}":"{}";tool_def d={.name=n,.description=c->tools[i].description,.input_schema_json=c->tools[i].input_schema,.source=c->name,.affordance=domain,.risk=risk,.invoke=adapter_call,.user=a};tool_registry_add(reg,d);}return true;
}

void mcp_client_free(mcp_client* c){
  if(!c)return;tools_free(c);
#ifdef _WIN32
  if(c->stdin_write)CloseHandle((HANDLE)c->stdin_write);if(c->stdout_read)CloseHandle((HANDLE)c->stdout_read);if(c->process){if(WaitForSingleObject((HANDLE)c->process,1000)!=WAIT_OBJECT_0)TerminateProcess((HANDLE)c->process,0);CloseHandle((HANDLE)c->process);}if(c->thread)CloseHandle((HANDLE)c->thread);
#endif
  if(c->name)moyu_free(c->name);if(c->server_url)moyu_free(c->server_url);if(c->api_key)moyu_free(c->api_key);if(c->command_line)moyu_free(c->command_line);if(c->working_dir)moyu_free(c->working_dir);if(c->session_id)moyu_free(c->session_id);memset(c,0,sizeof(*c));
}
