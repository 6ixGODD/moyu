#include "agent.h"

#include "cJSON.h"
#include "loop.h"
#include "memory.h"
#include "mem.h"
#include "platform.h"
#include "lua_rt.h"
#include "state.h"
#include "tool.h"

#include <stdio.h>
#include <string.h>

bool agent_init(agent_runtime* a,moyu_app* app){
  if(!a||!app)return false;memset(a,0,sizeof(*a));a->app=app;a->autonomous_enabled=true;a->rng=platform_unix_ms()^0x9e3779b97f4a7c15ULL;
  state_intention in;if(state_load_active_intention(app->state,&in)){
    a->active_id=in.id;snprintf(a->goal,sizeof(a->goal),"%s",in.goal);snprintf(a->status,sizeof(a->status),"%s",in.status);a->step_index=in.step_index;
    if(in.deadline_ms<platform_unix_ms()){state_update_intention(app->state,in.id,"failed",in.step_index,"expired during sleep");a->active_id=0;}
  }
  a->next_autonomous_ms=platform_now_ms()+30000;return true;
}

void agent_on_human_event(agent_runtime* a,const char* kind,const char* detail){
  if(!a||!a->app)return;char summary[512];snprintf(summary,sizeof(summary),"The human %s%s%s.",kind?kind:"interacted",detail?": ":"",detail?detail:"");
  state_add_episode(a->app->state,"interaction",summary,"{}",0.45,0.25);
  if(kind&&!strcmp(kind,"corrected"))state_record_feedback(a->app->state,"general","correct");
}

static void complete_observation(agent_runtime* a,const char* result){
  moyu_app* app=a->app;size_t n=result?strlen(result):0;char summary[512];snprintf(summary,sizeof(summary),"I quietly inspected an authorized place and found %zu bytes of clues.",n);
  int64_t ep=state_add_episode(app->state,"discovery",summary,"{\"tool\":\"filesystem.observe\"}",0.58,n>80?0.65:0.25);
  state_upsert_belief(app->state,"authorized workspace","contains","small digital traces",0.55,"filesystem.observe");
  lua_runtime_call_memory_candidate(app,summary);
  memory_consider_episode(app->memory,ep,summary,0.58,n>80?0.65:0.25,app->personality.curiosity);
  state_update_intention(app->state,a->active_id,"completed",1,summary);a->active_id=0;a->goal[0]=0;strcpy(a->status,"idle");
  emotion_react(&app->emotion, 0.14f, 0.18f);
  moyu_app_emit_anim(app,ANIM_FOUND);moyu_app_emit_say(app,"捡到一点线索。",2600);
}

void agent_tick(agent_runtime* a,uint64_t now_ms,float idle){
  if(!a||!a->app||!a->autonomous_enabled||a->active_id||now_ms<a->next_autonomous_ms)return;
  a->next_autonomous_ms=now_ms+30ULL*60ULL*1000ULL;if(idle<120.0f||!a->app->observe_root)return;
  if(!state_budget_take(a->app->state,"autonomous_observe",6,1))return;
  char* policy_state=state_relevant_context(a->app->state,8);char proposed_goal[256],proposed_tool[128],proposed_args[1024];
  bool proposed=lua_runtime_propose_desire(a->app,policy_state,proposed_goal,sizeof(proposed_goal),proposed_tool,sizeof(proposed_tool),proposed_args,sizeof(proposed_args));moyu_free(policy_state);
  if(proposed){const tool_def* pt=tool_registry_find(a->app->tools,proposed_tool);bool safe=pt&&pt->risk!=TOOL_MUTATE&&(strcmp(pt->source,"builtin")==0||state_permission_allowed(a->app->state,pt->name,"*",tool_risk_name(pt->risk)));if(safe){a->active_id=state_create_intention(a->app->state,proposed_goal,"lua_policy",3,platform_unix_ms()+30000);snprintf(a->goal,sizeof(a->goal),"%s",proposed_goal);strcpy(a->status,"running");char* r=tool_invoke(pt,proposed_args);if(r){lua_runtime_call_tool_result(a->app,r);complete_observation(a,r);moyu_free(r);return;}state_update_intention(a->app->state,a->active_id,"failed",0,"policy tool failed");a->active_id=0;return;}}
  a->active_id=state_create_intention(a->app->state,"Look for one small clue in an authorized place.","curiosity",3,platform_unix_ms()+30000);
  if(!a->active_id)return;snprintf(a->goal,sizeof(a->goal),"Look for one small clue in an authorized place.");strcpy(a->status,"running");
  state_update_intention(a->app->state,a->active_id,"running",0,NULL);moyu_app_emit_anim(a->app,ANIM_WORK);
  const tool_def* t=tool_registry_find(a->app->tools,"filesystem.observe");char args[1024];snprintf(args,sizeof(args),"{\"root\":\"%s\"}",a->app->observe_root);
  char* out=t?tool_invoke(t,args):NULL;if(out){complete_observation(a,out);moyu_free(out);}else{state_update_intention(a->app->state,a->active_id,"failed",0,"tool unavailable");a->active_id=0;}
}

void agent_cancel(agent_runtime* a,const char* reason){if(!a||!a->active_id)return;state_update_intention(a->app->state,a->active_id,"cancelled",a->step_index,reason?reason:"cancelled by human");a->active_id=0;a->goal[0]=0;strcpy(a->status,"idle");}

char* agent_explain(agent_runtime* a){
  cJSON* o=cJSON_CreateObject();cJSON_AddStringToObject(o,"status",a&&a->active_id?a->status:"idle");cJSON_AddStringToObject(o,"goal",a&&a->active_id?a->goal:"No active project.");
  cJSON_AddStringToObject(o,"policy","One intention, at most three steps, permission checked before external effects.");char* s=cJSON_PrintUnformatted(o);cJSON_Delete(o);return s;
}
