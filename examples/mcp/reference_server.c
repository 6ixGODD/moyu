// MOYU reference MCP servers. Compile with SERVER_KIND:
// 1 = git over stdio, 2 = notes over stdio, 3 = weather over Streamable HTTP.
#include "cJSON.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SERVER_KIND
#define SERVER_KIND 1
#endif

static cJSON* content(const char* text) {
  cJSON* result=cJSON_CreateObject();cJSON* list=cJSON_AddArrayToObject(result,"content");cJSON* item=cJSON_CreateObject();cJSON_AddStringToObject(item,"type","text");cJSON_AddStringToObject(item,"text",text?text:"");cJSON_AddItemToArray(list,item);return result;
}

static cJSON* response(cJSON* request,cJSON* result) {
  cJSON* out=cJSON_CreateObject();cJSON_AddStringToObject(out,"jsonrpc","2.0");cJSON* id=cJSON_GetObjectItem(request,"id");if(id)cJSON_AddItemToObject(out,"id",cJSON_Duplicate(id,1));if(result)cJSON_AddItemToObject(out,"result",result);return out;
}

static cJSON* tool(const char* name,const char* desc,const char* props,const char* required){
  cJSON* t=cJSON_CreateObject();cJSON_AddStringToObject(t,"name",name);cJSON_AddStringToObject(t,"description",desc);cJSON* schema=cJSON_CreateObject();cJSON_AddStringToObject(schema,"type","object");cJSON* p=cJSON_Parse(props);cJSON_AddItemToObject(schema,"properties",p?p:cJSON_CreateObject());if(required){cJSON* r=cJSON_CreateArray();cJSON_AddItemToArray(r,cJSON_CreateString(required));cJSON_AddItemToObject(schema,"required",r);}cJSON_AddItemToObject(t,"inputSchema",schema);return t;
}

static bool safe_arg(const char* s){if(!s||!*s)return false;for(;*s;s++)if(*s=='"'||*s=='&'||*s=='|'||*s=='<'||*s=='>')return false;return true;}

static char* run_command(const char* command){
  FILE* p=_popen(command,"r");if(!p)return _strdup("command failed");size_t cap=4096,len=0;char* out=(char*)malloc(cap);char buf[512];while(fgets(buf,sizeof(buf),p)){size_t n=strlen(buf);if(len+n+1>cap){cap*=2;out=(char*)realloc(out,cap);}if(len+n>60000)break;memcpy(out+len,buf,n);len+=n;}out[len]=0;_pclose(p);return out;
}

static cJSON* git_call(const char* name,cJSON* args){
  cJSON* root=cJSON_GetObjectItem(args,"root");const char* path=root&&cJSON_IsString(root)?root->valuestring:".";if(!safe_arg(path))return content("invalid root");char cmd[4096];
  if(!strcmp(name,"git.recent_commits"))snprintf(cmd,sizeof(cmd),"git -C \"%s\" log -n 8 --pretty=format:\"%%h %%ad %%s\" --date=short 2>&1",path);
  else if(!strcmp(name,"git.stale_branches"))snprintf(cmd,sizeof(cmd),"git -C \"%s\" for-each-ref --sort=committerdate --format=\"%%%%(committerdate:short) %%%%(refname:short)\" refs/heads 2>&1",path);
  else snprintf(cmd,sizeof(cmd),"git -C \"%s\" grep -n -I -E \"TODO|FIXME\" -- \"*.c\" \"*.h\" \"*.md\" 2>&1",path);
  char* text=run_command(cmd);cJSON* r=content(text);free(text);return r;
}

static void clean_name(const char* s,char* out,size_t cap){size_t j=0;for(;s&&*s&&j+1<cap;s++){unsigned char c=(unsigned char)*s;if(isalnum(c)||c=='-'||c=='_')out[j++]=(char)c;else if(c==' ')out[j++]='_';}if(!j)strcpy(out,"note");else out[j]=0;}

static const char* notes_root(void){static char root[MAX_PATH];if(root[0])return root;DWORD n=GetEnvironmentVariableA("MOYU_NOTES_ROOT",root,MAX_PATH);if(!n||n>=MAX_PATH)strcpy(root,"notes");CreateDirectoryA(root,NULL);char drafts[MAX_PATH];snprintf(drafts,sizeof(drafts),"%s\\drafts",root);CreateDirectoryA(drafts,NULL);return root;}

static cJSON* notes_call(const char* name,cJSON* args){
  cJSON* title=cJSON_GetObjectItem(args,"title");cJSON* body=cJSON_GetObjectItem(args,"body");cJSON* id=cJSON_GetObjectItem(args,"draft_id");char safe[128],path[MAX_PATH],dest[MAX_PATH];
  if(!strcmp(name,"notes.create_draft")){if(!title||!body||!cJSON_IsString(title)||!cJSON_IsString(body))return content("title and body required");clean_name(title->valuestring,safe,sizeof(safe));snprintf(path,sizeof(path),"%s\\drafts\\%s.md",notes_root(),safe);FILE* f=fopen(path,"wb");if(!f)return content("cannot create draft");fprintf(f,"# %s\n\n%s\n",title->valuestring,body->valuestring);fclose(f);return content(safe);}
  if(!id||!cJSON_IsString(id)||!safe_arg(id->valuestring))return content("draft_id required");clean_name(id->valuestring,safe,sizeof(safe));snprintf(path,sizeof(path),"%s\\drafts\\%s.md",notes_root(),safe);snprintf(dest,sizeof(dest),"%s\\%s.md",notes_root(),safe);return content(MoveFileExA(path,dest,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)?"published":"publish failed");
}

static char* http_get(const char* url){
  wchar_t host[256],path[2048];const char* p=strstr(url,"://");p=p?p+3:url;const char* slash=strchr(p,'/');MultiByteToWideChar(CP_UTF8,0,p,(int)(slash-p),host,256);host[slash-p]=0;MultiByteToWideChar(CP_UTF8,0,slash,-1,path,2048);
  HINTERNET s=WinHttpOpen(L"moyu-weather/0.2",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,NULL,NULL,0);HINTERNET c=WinHttpConnect(s,host,INTERNET_DEFAULT_HTTPS_PORT,0);HINTERNET r=WinHttpOpenRequest(c,L"GET",path,NULL,NULL,NULL,WINHTTP_FLAG_SECURE);char* out=NULL;if(WinHttpSendRequest(r,NULL,0,NULL,0,0,0)&&WinHttpReceiveResponse(r,NULL)){size_t cap=8192,len=0;out=(char*)malloc(cap);DWORD avail=0;while(WinHttpQueryDataAvailable(r,&avail)&&avail){if(len+avail+1>cap){cap=(len+avail+1)*2;out=(char*)realloc(out,cap);}DWORD rd=0;if(!WinHttpReadData(r,out+len,avail,&rd)||!rd)break;len+=rd;}out[len]=0;}WinHttpCloseHandle(r);WinHttpCloseHandle(c);WinHttpCloseHandle(s);return out?out:_strdup("weather unavailable");
}

static cJSON* weather_call(const char* name,cJSON* args){(void)name;cJSON* lat=cJSON_GetObjectItem(args,"latitude");cJSON* lon=cJSON_GetObjectItem(args,"longitude");double a=lat&&cJSON_IsNumber(lat)?lat->valuedouble:39.9042,b=lon&&cJSON_IsNumber(lon)?lon->valuedouble:116.4074;char url[1024];snprintf(url,sizeof(url),"https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f&current=temperature_2m,weather_code,wind_speed_10m&daily=temperature_2m_max,temperature_2m_min&forecast_days=3&timezone=auto",a,b);char* text=http_get(url);cJSON* r=content(text);free(text);return r;}

static cJSON* dispatch(cJSON* req){
  cJSON* method=cJSON_GetObjectItem(req,"method");if(!method||!cJSON_IsString(method))return response(req,content("bad request"));const char* m=method->valuestring;
  if(!strcmp(m,"initialize")){cJSON* r=cJSON_CreateObject();cJSON_AddStringToObject(r,"protocolVersion","2025-11-25");cJSON* caps=cJSON_CreateObject();cJSON_AddItemToObject(caps,"tools",cJSON_CreateObject());cJSON_AddItemToObject(r,"capabilities",caps);cJSON* info=cJSON_CreateObject();cJSON_AddStringToObject(info,"name",SERVER_KIND==1?"moyu-git":SERVER_KIND==2?"moyu-notes":"moyu-weather");cJSON_AddStringToObject(info,"version","0.2.0");cJSON_AddItemToObject(r,"serverInfo",info);return response(req,r);}
  if(!strcmp(m,"notifications/initialized"))return NULL;
  if(!strcmp(m,"ping"))return response(req,cJSON_CreateObject());
  if(!strcmp(m,"tools/list")){cJSON* r=cJSON_CreateObject();cJSON* a=cJSON_AddArrayToObject(r,"tools");
#if SERVER_KIND==1
    cJSON_AddItemToArray(a,tool("git.recent_commits","Recent local commits.","{\"root\":{\"type\":\"string\"}}","root"));cJSON_AddItemToArray(a,tool("git.stale_branches","Local branches ordered by age.","{\"root\":{\"type\":\"string\"}}","root"));cJSON_AddItemToArray(a,tool("git.todo_candidates","Read-only TODO candidates.","{\"root\":{\"type\":\"string\"}}","root"));
#elif SERVER_KIND==2
    cJSON_AddItemToArray(a,tool("notes.create_draft","Create a private Markdown draft.","{\"title\":{\"type\":\"string\"},\"body\":{\"type\":\"string\"}}","title"));cJSON_AddItemToArray(a,tool("notes.publish","Publish a draft into the configured notes root.","{\"draft_id\":{\"type\":\"string\"}}","draft_id"));
#else
    cJSON_AddItemToArray(a,tool("weather.current","Current weather and a short forecast from Open-Meteo.","{\"latitude\":{\"type\":\"number\"},\"longitude\":{\"type\":\"number\"}}",NULL));
#endif
    return response(req,r);}
  if(!strcmp(m,"tools/call")){cJSON* p=cJSON_GetObjectItem(req,"params");cJSON* n=p?cJSON_GetObjectItem(p,"name"):NULL;cJSON* args=p?cJSON_GetObjectItem(p,"arguments"):NULL;if(!args)args=cJSON_CreateObject();cJSON* result=
#if SERVER_KIND==1
      git_call(n&&cJSON_IsString(n)?n->valuestring:"",args);
#elif SERVER_KIND==2
      notes_call(n&&cJSON_IsString(n)?n->valuestring:"",args);
#else
      weather_call(n&&cJSON_IsString(n)?n->valuestring:"",args);
#endif
    return response(req,result);}
  return response(req,content("unsupported method"));
}

static char* handle_json(const char* line){cJSON* req=cJSON_Parse(line);if(!req)return _strdup("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"parse error\"},\"id\":null}");cJSON* out=dispatch(req);char* s=out?cJSON_PrintUnformatted(out):NULL;if(out)cJSON_Delete(out);cJSON_Delete(req);return s;}

static int stdio_main(void){size_t cap=1024*1024;char* line=(char*)malloc(cap);if(!line)return 1;while(fgets(line,(int)cap,stdin)){char* out=handle_json(line);if(out){puts(out);fflush(stdout);free(out);}}free(line);return 0;}

static int http_main(void){WSADATA wd;if(WSAStartup(MAKEWORD(2,2),&wd))return 1;SOCKET srv=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(7788);if(bind(srv,(struct sockaddr*)&a,sizeof(a))||listen(srv,8))return 2;fprintf(stderr,"moyu-weather MCP listening at http://127.0.0.1:7788/mcp\n");char* req=(char*)malloc(1024*1024);if(!req)return 3;for(;;){SOCKET c=accept(srv,NULL,NULL);if(c==INVALID_SOCKET)continue;int len=0;char* body=NULL;size_t content_len=0;while(len<1024*1024-1){int got=recv(c,req+len,1024*1024-1-len,0);if(got<=0)break;len+=got;req[len]=0;if(!body){char* sep=strstr(req,"\r\n\r\n");if(sep){body=sep+4;char* cl=strstr(req,"Content-Length:");if(!cl)cl=strstr(req,"content-length:");if(cl)content_len=(size_t)strtoul(cl+15,NULL,10);}}if(body&&(size_t)(req+len-body)>=content_len)break;}if(!body){closesocket(c);continue;}char* out=handle_json(body);if(out){char head[512];int hn=snprintf(head,sizeof(head),"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nMCP-Session-Id: moyu-weather-local\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",strlen(out));send(c,head,hn,0);send(c,out,(int)strlen(out),0);free(out);}else{const char* accepted="HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send(c,accepted,(int)strlen(accepted),0);}closesocket(c);}free(req);closesocket(srv);WSACleanup();return 0;}

int main(void){
#if SERVER_KIND==3
  return http_main();
#else
  return stdio_main();
#endif
}
