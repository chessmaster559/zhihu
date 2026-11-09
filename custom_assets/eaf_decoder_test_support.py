"""Freestanding host stubs and heap hooks for the official EAF decoder check."""

STUBS = {'stdlib.h': '#include <stddef.h>\n#include <limits.h>\n',
 'string.h': '#include <stddef.h>\n'
             'void *memcpy(void*,const void*,size_t);\n'
             'void *memset(void*,int,size_t);\n'
             'int memcmp(const void*,const void*,size_t);\n'
             'int strncmp(const char*,const char*,size_t);\n',
 'esp_log.h': '#define ESP_LOGW(...) ((void)0)\n'
              '#define ESP_LOGE(...) ((void)0)\n'
              '#define ESP_LOGI(...) ((void)0)\n'
              '#define ESP_LOGD(...) ((void)0)\n',
 'esp_heap_caps.h': '#include <stddef.h>\n'
                    '#define MALLOC_CAP_SPIRAM 1\n'
                    '#define MALLOC_CAP_8BIT 2\n'
                    'void *heap_caps_malloc(size_t,unsigned);\n'
                    'void *heap_caps_realloc(void*,size_t,unsigned);\n'
                    'void heap_caps_free(void*);\n'}

SUPPORT = r'''
#include "esp_eaf_dec.h"
#include <stddef.h>
#define API __declspec(dllexport)
static void *(*allocate_cb)(size_t,unsigned);
static void *(*resize_cb)(void*,size_t,unsigned);
static void (*free_cb)(void*);
void *heap_caps_malloc(size_t n,unsigned c){return allocate_cb(n,c);}
void *heap_caps_realloc(void *p,size_t n,unsigned c){return resize_cb(p,n,c);}
void heap_caps_free(void *p){free_cb(p);}
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
void *memset(void *d,int v,size_t n){unsigned char *a=d;for(size_t i=0;i<n;i++)a[i]=(unsigned char)v;return d;}
int memcmp(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return (int)x[i]-(int)y[i];return 0;}
int strncmp(const char *a,const char *b,size_t n){for(size_t i=0;i<n;i++){if(a[i]!=b[i])return (unsigned char)a[i]-(unsigned char)b[i];if(!a[i])return 0;}return 0;}
API void set_callbacks(void *(*a)(size_t,unsigned),void *(*r)(void*,size_t,unsigned),void (*f)(void*)){allocate_cb=a;resize_cb=r;free_cb=f;}
'''
