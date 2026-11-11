/* Capture at most two startup access units for offline decode diagnosis.
   Only compressed AVC/parameter sets are written; no URLs or tokens. */
#include "ptwitch_avc_snapshot.h"
#include "../app/version.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
void komi_sibling_path(char *out,size_t size,const char *name);
void komi_result(const char *format,...);
static unsigned captures;

void ptwitch_avc_snapshot(const void *sps,size_t ns,const void *pps,size_t np,
                         const void *sample,size_t length,unsigned prefix,
                         unsigned width,unsigned height,int mode,int boot)
{
    if(captures>=2)return;
    unsigned index=captures++;
    const unsigned char *data=sample;
    char types[192]={0};size_t used=0,at=0;unsigned count=0;
    if(!sps||!pps||!sample||!ns||!np||ns>4096||np>4096
        ||prefix<1||prefix>4||length>512u*1024u)return;
    while(at<length&&count++<64){
        if(prefix>length-at)return;
        uint32_t n=0;for(unsigned i=0;i<prefix;++i)n=(n<<8)|data[at++];
        if(!n||n>length-at)return;
        int written=snprintf(types+used,sizeof(types)-used,"%s%u",used?",":"",data[at]&31u);
        if(written<0||(size_t)written>=sizeof(types)-used)return;
        used+=(size_t)written;at+=n;
    }
    if(at!=length)return;
    komi_result("AVC startup capture=%u size=%ux%u mode=%d boot=%d bytes=%u nal-prefix=%u nal-types=%s",
                index,width,height,mode,boot,(unsigned)length,prefix,types);
    char name[64],path[256];
    snprintf(name,sizeof(name),"ptwitch-avc-%u-%u.txt",PTWITCH_BUILD,index);komi_sibling_path(path,sizeof(path),name);
    FILE *meta=fopen(path,"w");
    if(meta){
        fprintf(meta,"size=%ux%u mode=%d boot=%d bytes=%u prefix=%u nal-types=%s\nsps=",
                width,height,mode,boot,(unsigned)length,prefix,types);
        for(size_t i=0;i<ns;++i)fprintf(meta,"%02x",((const unsigned char*)sps)[i]);
        fputs("\npps=",meta);
        for(size_t i=0;i<np;++i)fprintf(meta,"%02x",((const unsigned char*)pps)[i]);
        fputs("\n",meta);fclose(meta);
    }
    snprintf(name,sizeof(name),"ptwitch-avc-%u-%u.h264",PTWITCH_BUILD,index);komi_sibling_path(path,sizeof(path),name);
    FILE *out=fopen(path,"wb");if(!out){komi_result("AVC capture file could not open");return;}
    static const unsigned char start[]={0,0,0,1};
    at=0;int ok=1;
    ok=ok&&fwrite(start,1,4,out)==4&&fwrite(sps,1,ns,out)==ns;
    ok=ok&&fwrite(start,1,4,out)==4&&fwrite(pps,1,np,out)==np;
    for(unsigned i=0;i<count&&at<length;++i){
        uint32_t n=0;for(unsigned j=0;j<prefix;++j)n=(n<<8)|data[at++];
        ok=ok&&fwrite(start,1,4,out)==4&&fwrite(data+at,1,n,out)==n;at+=n;
    }
    if(fclose(out)!=0)ok=0;
    komi_result("AVC capture %u saved=%d",index,ok);
}
