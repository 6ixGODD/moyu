#include "cJSON.h"
#include "llm.h"
#include "mcp.h"
#include "mem.h"
#include "memory.h"
#include "platform.h"
#include "secrets.h"
#include "state.h"
#include "tool.h"
#include "workdir.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define C_RESET "\x1b[0m"
#define C_DIM "\x1b[2m"
#define C_GREEN "\x1b[38;2;126;156;120m"
#define C_CREAM "\x1b[38;2;245;224;184m"
#define C_RED "\x1b[38;2;220;110;110m"

typedef struct { moyu_workdir wd; state_store state; memory_system memory; llm_config llm; tool_registry tools; mcp_client** mcps; size_t mcp_count; } chat_app;

static char* read_file(const char* path){size_t n=0;return platform_read_file(path,&n);}
static void replace(char** dst,const char* src){if(*dst)moyu_free(*dst);*dst=moyu_strdup(src);}
static void json_set_string(cJSON* obj,const char* key,const char* value){cJSON* item=cJSON_GetObjectItem(obj,key);if(item)cJSON_ReplaceItemInObject(obj,key,cJSON_CreateString(value?value:""));else cJSON_AddStringToObject(obj,key,value?value:"");}
static void json_set_number(cJSON* obj,const char* key,double value){cJSON* item=cJSON_GetObjectItem(obj,key);if(item)cJSON_ReplaceItemInObject(obj,key,cJSON_CreateNumber(value));else cJSON_AddNumberToObject(obj,key,value);}
static void json_set_bool(cJSON* obj,const char* key,int value){cJSON* item=cJSON_GetObjectItem(obj,key);if(item)cJSON_ReplaceItemInObject(obj,key,cJSON_CreateBool(value));else cJSON_AddBoolToObject(obj,key,value);}

static bool save_config(chat_app* a){
  cJSON* root=NULL;char* text=read_file(a->wd.config_path);
  if(text){root=cJSON_Parse(text);moyu_free(text);}
  if(!root)root=cJSON_CreateObject();
  cJSON* llm=cJSON_GetObjectItem(root,"llm");
  if(!cJSON_IsObject(llm)){if(llm)cJSON_DeleteItemFromObject(root,"llm");llm=cJSON_AddObjectToObject(root,"llm");}
  json_set_string(llm,"base_url",a->llm.base_url?a->llm.base_url:"");
  json_set_string(llm,"api_key_source","windows_dpapi");
  json_set_string(llm,"model",a->llm.model?a->llm.model:"");
  json_set_number(llm,"max_tokens",a->llm.max_tokens);
  json_set_number(llm,"temperature",a->llm.temperature);
  json_set_bool(llm,"json_mode",a->llm.json_mode);
  cJSON* daily=cJSON_GetObjectItem(llm,"daily_limit");if(!cJSON_IsNumber(daily))cJSON_AddNumberToObject(llm,"daily_limit",50);
  char* out=cJSON_Print(root);cJSON_Delete(root);
  bool ok=out&&platform_write_file_atomic(a->wd.config_path,out,strlen(out));
  if(out)moyu_free(out);
  return ok;
}

static bool store_api_key(chat_app* a,const char* key){
  if(!key||!*key)return false;
  if(!secrets_store(a->wd.secrets_path,key))return false;
  replace(&a->llm.api_key,key);
  return save_config(a);
}

static bool read_line(char* out,size_t cap){
#ifdef _WIN32
  HANDLE in=GetStdHandle(STD_INPUT_HANDLE);if(GetFileType(in)!=FILE_TYPE_CHAR){if(!fgets(out,(int)cap,stdin))return false;out[strcspn(out,"\r\n")]=0;return true;}
  wchar_t w[4096];DWORD n=0;if(!ReadConsoleW(in,w,4095,&n,NULL))return false;
  while(n&&(w[n-1]==L'\n'||w[n-1]==L'\r'))n--;w[n]=0;
  WideCharToMultiByte(CP_UTF8,0,w,-1,out,(int)cap,NULL,NULL);return true;
#else
  if(!fgets(out,(int)cap,stdin))return false;out[strcspn(out,"\r\n")]=0;return true;
#endif
}

static void load_config(chat_app* a){
  char* text=read_file(a->wd.config_path);if(!text)return;cJSON* root=cJSON_Parse(text);moyu_free(text);if(!root)return;
  cJSON* llm=cJSON_GetObjectItem(root,"llm");if(llm){cJSON* v=cJSON_GetObjectItem(llm,"base_url");if(cJSON_IsString(v))replace(&a->llm.base_url,v->valuestring);v=cJSON_GetObjectItem(llm,"model");if(cJSON_IsString(v))replace(&a->llm.model,v->valuestring);v=cJSON_GetObjectItem(llm,"max_tokens");if(cJSON_IsNumber(v))a->llm.max_tokens=v->valueint;v=cJSON_GetObjectItem(llm,"temperature");if(cJSON_IsNumber(v))a->llm.temperature=(float)v->valuedouble;}
  char* secret=secrets_load(a->wd.secrets_path);if(secret){replace(&a->llm.api_key,secret);moyu_free(secret);}
  cJSON* servers=cJSON_GetObjectItem(root,"mcp_servers");if(cJSON_IsArray(servers)){int count=cJSON_GetArraySize(servers);a->mcps=(mcp_client**)moyu_alloc((size_t)count*sizeof(*a->mcps));memset(a->mcps,0,(size_t)count*sizeof(*a->mcps));for(int i=0;i<count;i++){cJSON* s=cJSON_GetArrayItem(servers,i);cJSON* name=cJSON_GetObjectItem(s,"name");cJSON* trans=cJSON_GetObjectItem(s,"transport");cJSON* url=cJSON_GetObjectItem(s,"url");cJSON* cmd=cJSON_GetObjectItem(s,"command");mcp_client* m=(mcp_client*)moyu_alloc(sizeof(*m));memset(m,0,sizeof(*m));bool configured=false;if(cJSON_IsString(trans)&&!strcmp(trans->valuestring,"stdio")&&cJSON_IsString(cmd)){char line[4096];snprintf(line,sizeof(line),"\"%s\"",cmd->valuestring);cJSON* args=cJSON_GetObjectItem(s,"args");if(cJSON_IsArray(args))for(int j=0;j<cJSON_GetArraySize(args);j++){cJSON* x=cJSON_GetArrayItem(args,j);if(cJSON_IsString(x)){strncat(line," \"",sizeof(line)-strlen(line)-1);strncat(line,x->valuestring,sizeof(line)-strlen(line)-1);strncat(line,"\"",sizeof(line)-strlen(line)-1);}}mcp_client_init_stdio(m,cJSON_IsString(name)?name->valuestring:"mcp",line,NULL);configured=true;}else if(cJSON_IsString(url)){mcp_client_init_http(m,cJSON_IsString(name)?name->valuestring:"mcp",url->valuestring,NULL);configured=true;}if(configured&&mcp_register_tools(m,&a->tools))a->mcps[a->mcp_count++]=m;else{if(configured)mcp_client_free(m);moyu_free(m);}}}
  cJSON_Delete(root);
}

static void banner(chat_app* a){
  printf("\x1b[2J\x1b[H" C_CREAM "  /\\_/\\   MOYU CHAT" C_RESET "\n" C_CREAM " ( o.o )  " C_RESET C_DIM "persistent companion runtime" C_RESET "\n" C_CREAM "  > ^ <   " C_RESET C_DIM "%s | %zu MCP tool(s)" C_RESET "\n\n",a->llm.model,a->tools.count);
  printf(C_DIM "Type naturally, or /help for commands. Ctrl+C or /exit closes chat.\n" C_RESET);
}

static void help(void){printf("\n" C_GREEN "Commands" C_RESET "\n  /status                 runtime, home and LLM status\n  /config                 show editable config and secret paths\n  /model <name>           switch model now and persist to config\n  /baseurl <url>          switch OpenAI-compatible endpoint\n  /provider deepseek      set DeepSeek base_url and flash model\n  /apikey <token>         store key in Windows DPAPI secret storage\n  /pause | /resume        control autonomous behavior\n  /cancel                 cancel the active intention\n  /memory                 show long-term MEMORY.md\n  /remember <text>        add a durable fact about you\n  /forget <text>          remove matching memory (asks confirmation)\n  /collections            list collected discoveries\n  /collect <title> | <body>\n  /drafts                 list private drafts\n  /draft <title> | <body>\n  /tools                  list connected MCP tools and risk\n  /tool <name> <json>     explicitly call a tool\n  /allow <name> <scope>   persist external-tool permission\n  /clear                  redraw this screen\n  /exit                   close terminal chat\n\n");}

static void list_md(const char* dir){
#ifdef _WIN32
  char p[32768];snprintf(p,sizeof(p),"%s\\*.md",dir);wchar_t wp[32768];MultiByteToWideChar(CP_UTF8,0,p,-1,wp,32768);WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileW(wp,&fd);if(h==INVALID_HANDLE_VALUE){puts(C_DIM "  (empty)" C_RESET);return;}do{char n[1024];WideCharToMultiByte(CP_UTF8,0,fd.cFileName,-1,n,sizeof(n),NULL,NULL);printf("  %s\n",n);}while(FindNextFileW(h,&fd));FindClose(h);
#else
  (void)dir;puts("  unavailable");
#endif
}

static bool write_note(const char* dir,const char* spec){const char* bar=strchr(spec,'|');if(!bar)return false;char title[128];size_t j=0;for(const char* p=spec;p<bar&&j+1<sizeof(title);p++){unsigned char c=(unsigned char)*p;if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_')title[j++]=(char)c;else if(c==' ')title[j++]='_';}if(!j)memcpy(title,"untitled",8),j=8;title[j]=0;char file[180];snprintf(file,sizeof(file),"%s.md",title);char* path=platform_join_path(dir,file);char body[4096];snprintf(body,sizeof(body),"# %.*s\n\n%s\n",(int)(bar-spec),spec,bar+1);bool ok=platform_write_file_atomic(path,body,strlen(body));moyu_free(path);return ok;}

static bool command(chat_app* a,const char* line,bool* quit){
  if(!strcmp(line,"/exit")||!strcmp(line,"/quit")){*quit=true;return true;}if(!strcmp(line,"/help")){help();return true;}if(!strcmp(line,"/clear")){banner(a);return true;}
  if(!strcmp(line,"/status")){char* ctx=state_relevant_context(&a->state,5);printf("\nHome: %s\nLLM: %s (%s)\nMCP tools: %zu\nRecent state: %s\n\n",a->wd.root,a->llm.api_key&&*a->llm.api_key?"online":"offline",a->llm.model,a->tools.count,ctx);moyu_free(ctx);return true;}
  if(!strcmp(line,"/config")){printf("\nconfig: %s\nsecret: %s\nbase_url: %s\nmodel: %s\napi_key: %s\n\n",a->wd.config_path,a->wd.secrets_path,a->llm.base_url,a->llm.model,a->llm.api_key&&*a->llm.api_key?"stored in DPAPI":"missing");return true;}
  if(!strncmp(line,"/model ",7)){replace(&a->llm.model,line+7);puts(save_config(a)?C_GREEN "model saved" C_RESET:C_RED "failed to save config" C_RESET);return true;}
  if(!strncmp(line,"/baseurl ",9)){replace(&a->llm.base_url,line+9);puts(save_config(a)?C_GREEN "base_url saved" C_RESET:C_RED "failed to save config" C_RESET);return true;}
  if(!strcmp(line,"/provider deepseek")){replace(&a->llm.base_url,"https://api.deepseek.com/v1");if(!a->llm.model||!strncmp(a->llm.model,"glm-",4))replace(&a->llm.model,"deepseek-v4-flash");puts(save_config(a)?C_GREEN "DeepSeek profile saved" C_RESET:C_RED "failed to save config" C_RESET);return true;}
  if(!strncmp(line,"/apikey ",8)){puts(store_api_key(a,line+8)?C_GREEN "API key stored in DPAPI" C_RESET:C_RED "failed to store API key" C_RESET);return true;}
  if(!strcmp(line,"/pause")||!strcmp(line,"/resume")){bool on=!strcmp(line,"/resume");state_meta_set(&a->state,"autonomy_enabled",on?"1":"0");puts(on?C_GREEN "autonomy resumed" C_RESET:C_GREEN "autonomy paused" C_RESET);return true;}
  if(!strcmp(line,"/cancel")){state_intention in;if(state_load_active_intention(&a->state,&in)){state_update_intention(&a->state,in.id,"cancelled",in.step_index,"cancelled in terminal chat");puts(C_GREEN "active intention cancelled" C_RESET);}else puts(C_DIM "no active intention" C_RESET);return true;}
  if(!strcmp(line,"/memory")){memory_reload(&a->memory);printf("\n%s\n",a->memory.memory);return true;}
  if(!strncmp(line,"/remember ",10)){printf(memory_remember(&a->memory,"## About the human",line+10)?C_GREEN "remembered\n" C_RESET:C_RED "not stored (duplicate or secret-like)\n" C_RESET);return true;}
  if(!strncmp(line,"/forget ",8)){printf(C_RED "Remove matching memory? Type YES: " C_RESET);char yes[32];if(read_line(yes,sizeof(yes))&&!strcmp(yes,"YES"))printf("removed %d item(s)\n",memory_forget(&a->memory,line+8));else puts("cancelled");return true;}
  if(!strcmp(line,"/collections")){puts(C_GREEN "Collections" C_RESET);list_md(a->wd.collections_dir);return true;}if(!strncmp(line,"/collect ",9)){puts(write_note(a->wd.collections_dir,line+9)?C_GREEN "collected" C_RESET:C_RED "usage: /collect title | body" C_RESET);return true;}
  if(!strcmp(line,"/drafts")){puts(C_GREEN "Drafts" C_RESET);list_md(a->wd.drafts_dir);return true;}if(!strncmp(line,"/draft ",7)){puts(write_note(a->wd.drafts_dir,line+7)?C_GREEN "draft created" C_RESET:C_RED "usage: /draft title | body" C_RESET);return true;}
  if(!strcmp(line,"/tools")){if(!a->tools.count)puts(C_DIM "No MCP tools connected. Configure ~/.moyu/config.json." C_RESET);for(size_t i=0;i<a->tools.count;i++)printf("  %-28s [%s/%s]\n      %s\n",a->tools.defs[i].name,a->tools.defs[i].source,tool_risk_name(a->tools.defs[i].risk),a->tools.defs[i].description);return true;}
  if(!strncmp(line,"/allow ",7)){const char* p=line+7;const char* sp=strchr(p,' ');char name[256];snprintf(name,sizeof(name),"%.*s",sp?(int)(sp-p):(int)strlen(p),p);state_permission_set(&a->state,name,sp?sp+1:"*","allow",true);puts(C_GREEN "permission saved" C_RESET);return true;}
  if(!strncmp(line,"/tool ",6)){const char* p=line+6;const char* sp=strchr(p,' ');char name[256];snprintf(name,sizeof(name),"%.*s",sp?(int)(sp-p):(int)strlen(p),p);const tool_def* t=tool_registry_find(&a->tools,name);if(!t){puts(C_RED "unknown tool; use /tools" C_RESET);return true;}if(t->risk!=TOOL_MUTATE&&strcmp(t->source,"builtin")&& !state_permission_allowed(&a->state,name,"*",tool_risk_name(t->risk))){puts(C_RED "permission required; use /allow <name> *" C_RESET);return true;}if(t->risk==TOOL_MUTATE){printf(C_RED "Mutation tool. Type RUN to confirm: " C_RESET);char yes[32];if(!read_line(yes,sizeof(yes))||strcmp(yes,"RUN")){puts("cancelled");return true;}}char* result=tool_invoke(t,sp?sp+1:"{}");printf(C_GREEN "tool> " C_RESET "%s\n",result?result:"failed");if(result)moyu_free(result);return true;}
  return line[0]=='/';
}

int main(void){
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);SetConsoleCP(CP_UTF8);DWORD mode=0;HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE);GetConsoleMode(out,&mode);SetConsoleMode(out,mode|ENABLE_VIRTUAL_TERMINAL_PROCESSING);SetConsoleTitleW(L"MOYU Chat");
#endif
  chat_app a;memset(&a,0,sizeof(a));llm_config_init(&a.llm);tool_registry_init(&a.tools,16);if(!workdir_init(&a.wd)||!state_open(&a.state,a.wd.db_path)||!memory_init(&a.memory,&a.wd,&a.state)){fputs("MOYU runtime initialization failed.\n",stderr);return 1;}load_config(&a);banner(&a);bool quit=false;char line[4096];char* last_user=NULL;char* last_assistant=NULL;while(!quit){printf("\n" C_GREEN "you" C_RESET " > ");fflush(stdout);if(!read_line(line,sizeof(line)))break;if(!*line)continue;state_add_message(&a.state,"user",line);if(command(&a,line,&quit))continue;if(!a.llm.api_key||!*a.llm.api_key){puts(C_RED "moyu> LLM is offline. Memory and local commands still work." C_RESET);continue;}char* recent=state_relevant_context(&a.state,8);char* system=memory_compose(&a.memory,NULL,recent,line,12000);moyu_free(recent);const char* msgs[4]={system,line,NULL,NULL};size_t msg_count=2;if(last_user&&last_assistant){msgs[1]=last_user;msgs[2]=last_assistant;msgs[3]=line;msg_count=4;}printf(C_DIM "moyu is thinking...\r" C_RESET);fflush(stdout);llm_result r=llm_complete(&a.llm,NULL,msgs,msg_count,60000);printf("\x1b[2K\r" C_CREAM "moyu" C_RESET " > %s\n",r.text?r.text:(r.error?r.error:"no response"));if(r.text){state_add_message(&a.state,"assistant",r.text);if(last_user)moyu_free(last_user);if(last_assistant)moyu_free(last_assistant);last_user=moyu_strdup(line);last_assistant=moyu_strdup(r.text);}moyu_free(system);llm_result_free(&r);}if(last_user)moyu_free(last_user);if(last_assistant)moyu_free(last_assistant);
  for(size_t i=0;i<a.mcp_count;i++){mcp_client_free(a.mcps[i]);moyu_free(a.mcps[i]);}if(a.mcps)moyu_free(a.mcps);tool_registry_free(&a.tools);memory_free(&a.memory);state_close(&a.state);workdir_free(&a.wd);llm_config_free(&a.llm);return 0;
}
