#include "secrets.h"

#include "mem.h"
#include "platform.h"

#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

bool secrets_store(const char* path,const char* secret){
  if(!path||!secret||!*secret)return false;DATA_BLOB in={(DWORD)strlen(secret),(BYTE*)secret},out={0};
  if(!CryptProtectData(&in,L"MOYU API key",NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&out))return false;
  bool ok=platform_write_file_atomic(path,out.pbData,out.cbData);LocalFree(out.pbData);return ok;
}
char* secrets_load(const char* path){
  size_t n=0;char* enc=platform_read_file(path,&n);if(!enc)return NULL;DATA_BLOB in={(DWORD)n,(BYTE*)enc},out={0};char* secret=NULL;
  if(CryptUnprotectData(&in,NULL,NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&out)){secret=(char*)moyu_alloc((size_t)out.cbData+1);memcpy(secret,out.pbData,out.cbData);secret[out.cbData]=0;LocalFree(out.pbData);}moyu_free(enc);return secret;
}
#else
bool secrets_store(const char* path,const char* secret){return platform_write_file_atomic(path,secret,strlen(secret));}
char* secrets_load(const char* path){return platform_read_file(path,NULL);}
#endif
