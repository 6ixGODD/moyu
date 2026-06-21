#include "memory.h"

#include "hash.h"
#include "log.h"
#include "mem.h"
#include "platform.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MEMORY_MAX (64u * 1024u)

static bool contains_secret(const char* s) {
  if (!s) return false;
  static const char* bad[] = {"api_key", "apikey", "password", "private key", "authorization:", "sk-", NULL};
  char lower[1024]; size_t n=strlen(s); if(n>=sizeof(lower))n=sizeof(lower)-1;
  for(size_t i=0;i<n;i++)lower[i]=(char)tolower((unsigned char)s[i]); lower[n]=0;
  for(int i=0;bad[i];i++)if(strstr(lower,bad[i]))return true; return false;
}

static char* clean_line(const char* s) {
  size_t n=strlen(s); if(n>512)n=512; char* out=(char*)moyu_alloc(n+1); size_t j=0;
  for(size_t i=0;i<n;i++){ unsigned char c=(unsigned char)s[i]; if(c=='\r'||c=='\n'||c=='\t')c=' '; if(c<32)continue; out[j++]=(char)c; }
  while(j&&out[j-1]==' ')j--; out[j]=0; return out;
}

bool memory_reload(memory_system* m) {
  if(!m||!m->wd)return false; size_t n=0;
  char* soul=platform_read_file(m->wd->soul_path,&n); if(!soul)return false;
  char* mem=platform_read_file(m->wd->memory_path,&n); if(!mem){moyu_free(soul);return false;}
  if(m->soul)moyu_free(m->soul);if(m->memory)moyu_free(m->memory);
  m->soul=soul;m->memory=mem;m->loaded_at_ms=platform_now_ms();return true;
}

bool memory_init(memory_system* m,moyu_workdir* wd,state_store* state){
  if(!m||!wd)return false;memset(m,0,sizeof(*m));m->wd=wd;m->state=state;return memory_reload(m);
}

void memory_free(memory_system* m){if(!m)return;if(m->soul)moyu_free(m->soul);if(m->memory)moyu_free(m->memory);memset(m,0,sizeof(*m));}

bool memory_remember(memory_system* m,const char* section,const char* text){
  if(!m||!m->memory||!text||!*text||contains_secret(text))return false;
  char* line=clean_line(text);if(!*line||strstr(m->memory,line)){moyu_free(line);return false;}
  const char* heading=section&&*section?section:"## Important episodes";
  const char* pos=strstr(m->memory,heading);if(!pos){moyu_free(line);return false;}
  const char* insert=strchr(pos,'\n');if(!insert){moyu_free(line);return false;}insert++;
  size_t before=(size_t)(insert-m->memory), old=strlen(m->memory), add=strlen(line)+3;
  if(old+add>MEMORY_MAX){moyu_free(line);return false;}
  char* next=(char*)moyu_alloc(old+add+1);memcpy(next,m->memory,before);
  int wrote=snprintf(next+before,old+add+1-before,"- %s\n",line);
  memcpy(next+before+(size_t)wrote,insert,old-before+1);
  bool ok=platform_write_file_atomic(m->wd->memory_path,next,old+add);
  if(ok){moyu_free(m->memory);m->memory=next;}else moyu_free(next);moyu_free(line);return ok;
}

int memory_forget(memory_system* m,const char* needle){
  if(!m||!m->memory||!needle||!*needle)return 0;size_t cap=strlen(m->memory)+1;char* out=(char*)moyu_alloc(cap);out[0]=0;int removed=0;
  const char* p=m->memory;size_t used=0;size_t qn=strlen(needle);while(*p){const char* e=strchr(p,'\n');size_t n=e?(size_t)(e-p)+1:strlen(p);bool found=false;if(qn<=n){for(size_t i=0;i+qn<=n;i++)if(memcmp(p+i,needle,qn)==0){found=true;break;}}bool drop=n>2&&p[0]=='-'&&found;
    if(drop)removed++;else{memcpy(out+used,p,n);used+=n;out[used]=0;}p+=n;}
  if(removed&&platform_write_file_atomic(m->wd->memory_path,out,used)){moyu_free(m->memory);m->memory=out;return removed;}moyu_free(out);return 0;
}

bool memory_consider_episode(memory_system* m,int64_t id,const char* summary,double importance,double novelty,double bias){
  if(!m||!summary)return false;double score=importance*0.55+novelty*0.35+bias*0.10;if(score<0.62)return false;
  uint64_t h=hash_fnv1a64(summary,strlen(summary))^(uint64_t)id;double roll=(double)(h%10000)/10000.0;double chance=0.15+(score-0.62)*1.5;if(chance>0.80)chance=0.80;
  if(roll>chance)return false;bool ok=memory_remember(m,"## Important episodes",summary);if(ok&&m->state)state_mark_episode_promoted(m->state,id);return ok;
}

char* memory_compose(memory_system* m,const char* active,const char* recent,const char* input,size_t max_chars){
  if(!m||!m->soul||!m->memory)return moyu_strdup(input?input:"");if(max_chars<1024)max_chars=1024;
  const char* fmt="%s\n\n--- LONG-TERM MEMORY ---\n%s\n\n--- ACTIVE INTENTION ---\n%s\n\n--- RELEVANT CONTEXT ---\n%s\n\n--- CURRENT INPUT ---\n%s";
  size_t need=strlen(fmt)+strlen(m->soul)+strlen(m->memory)+strlen(active?active:"none")+strlen(recent?recent:"[]")+strlen(input?input:"")+1;
  char* out=(char*)moyu_alloc(need);snprintf(out,need,fmt,m->soul,m->memory,active?active:"none",recent?recent:"[]",input?input:"");
  if(strlen(out)>max_chars)out[max_chars]=0;return out;
}
