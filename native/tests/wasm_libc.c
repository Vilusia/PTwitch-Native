#include <stddef.h>
void *memset(void *p,int c,size_t n){unsigned char *d=p;for(size_t i=0;i<n;++i)d[i]=(unsigned char)c;return p;}
void *memcpy(void *p,const void *q,size_t n){unsigned char *d=p;const unsigned char *s=q;for(size_t i=0;i<n;++i)d[i]=s[i];return p;}
void *memmove(void *p,const void *q,size_t n){unsigned char *d=p;const unsigned char *s=q;if(d<s)for(size_t i=0;i<n;++i)d[i]=s[i];else while(n){--n;d[n]=s[n];}return p;}
