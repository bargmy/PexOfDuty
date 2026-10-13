#include "libbbench.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* Small JSON tokenizer. Each token knows the index of its next sibling;
 * it never modifies the source JSON (including embedded base64 textures). */
typedef struct { int start, end, next, first, last, count, type; } JT;
typedef struct { const char *s; size_t len, pos; JT *t; int n, cap, depth, failed; } JP;
enum { J_STRING=1,J_PRIMITIVE,J_ARRAY,J_OBJECT };
static void jw(JP *p) { while(p->pos<p->len && (unsigned char)p->s[p->pos]<=32) p->pos++; }
static int jt_new(JP *p, int type) {
    if(p->n >= p->cap) {
        int next=p->cap ? p->cap*2:2048;
        JT *t=(JT*)realloc(p->t,(size_t)next*sizeof(JT));
        if(!t){p->failed=1;return -1;} p->t=t;p->cap=next;
    }
    int i=p->n++; JT *t=&p->t[i];memset(t,0,sizeof(*t));
    t->type=type;t->start=(int)p->pos;t->first=-1;t->last=-1;t->next=i+1;
    return i;
}
static void jt_child(JP *p,int parent,int child) {
    if(p->t[parent].last>=0)p->t[p->t[parent].last].next=child;
    else p->t[parent].first=child;
    p->t[parent].last=child;p->t[parent].count++;
}
static int jp_value(JP *p) {
    jw(p);if(p->pos>=p->len || p->depth++>100){p->failed=1;return -1;}
    int i=-1;char c=p->s[p->pos];
    if(c=='"') {
        i=jt_new(p,J_STRING);if(i<0)goto done;
        p->pos++;
        while(p->pos<p->len){
            char d=p->s[p->pos++];if(d=='"')break;
            if(d=='\\'){if(p->pos>=p->len){p->failed=1;break;}p->pos++;}
            else if((unsigned char)d<32){p->failed=1;break;}
        }
        if(p->s[p->pos-1]!='"')p->failed=1;
    } else if(c=='[' || c=='{') {
        int object=c=='{', closer=object?'}':']';
        i=jt_new(p,object?J_OBJECT:J_ARRAY);if(i<0)goto done;p->pos++;
        jw(p);
        while(!p->failed && p->pos<p->len && p->s[p->pos]!=closer){
            int k=jp_value(p);if(k<0){p->failed=1;break;}jt_child(p,i,k);
            jw(p);
            if(object){if(p->pos>=p->len || p->s[p->pos++]!=':'){p->failed=1;break;}
                int v=jp_value(p);if(v<0){p->failed=1;break;}jt_child(p,i,v);jw(p);}
            if(p->pos<p->len && p->s[p->pos]==',') {p->pos++;jw(p);}
            else if(p->pos<p->len && p->s[p->pos]!=closer){p->failed=1;break;}
        }
        if(p->pos>=p->len || p->s[p->pos++]!=closer)p->failed=1;
    } else {
        i=jt_new(p,J_PRIMITIVE);if(i<0)goto done;
        while(p->pos<p->len && p->s[p->pos]!=',' && p->s[p->pos]!=']' && p->s[p->pos]!='}' && !isspace((unsigned char)p->s[p->pos]))p->pos++;
        if(p->pos==(size_t)p->t[i].start)p->failed=1;
    }
    if(i>=0){p->t[i].end=(int)p->pos;p->t[i].next=p->n;}
done:p->depth--;return p->failed?-1:i;
}
/* Object children are key,value,key,value, ... and next links skip subtrees. */
static int j_eq(const JP *p,int i,const char *s){
    if(i<0 || p->t[i].type!=J_STRING)return 0;
    int n=p->t[i].end-p->t[i].start-2;
    return n==(int)strlen(s) && !memcmp(p->s+p->t[i].start+1,s,(size_t)n);
}
static int j_get(const JP *p,int i,const char *key){
    if(i<0 || p->t[i].type!=J_OBJECT)return -1;
    for(int k=p->t[i].first;k>=0 && k<p->t[i].next;){
        int v=k+1;
        if(j_eq(p,k,key))return v;
        k=p->t[v].next;
    }return -1;
}
static void j_str(const JP *p,int i,char *dst,size_t cap){
    if(!cap)return;
    dst[0]=0;
    if(i<0 || p->t[i].type!=J_STRING)return;
    size_t n=(size_t)(p->t[i].end-p->t[i].start-2);if(n>=cap)n=cap-1;
    memcpy(dst,p->s+p->t[i].start+1,n);dst[n]=0;
}
static float j_num(const JP *p,int i,float def){
    if(i<0)return def;
    if(p->t[i].type!=J_PRIMITIVE && p->t[i].type!=J_STRING)return def;
    const char *a=p->s+p->t[i].start+(p->t[i].type==J_STRING);
    return (float)strtod(a,NULL);
}
static int j_bool(const JP *p,int i,int def){
    if(i<0)return def;
    if(!strncmp(p->s+p->t[i].start,"true",4))return 1;
    if(!strncmp(p->s+p->t[i].start,"false",5))return 0;
    return def;
}
static void j_vec(const JP *p,int i,float *v,float def){
    for(int k=0;k<3;k++)v[k]=def;
    if(i<0)return;
    if(p->t[i].type==J_ARRAY){int n=p->t[i].first;for(int k=0;k<3 && n>=0 && n<p->t[i].next;k++){v[k]=j_num(p,n,def);n=p->t[n].next;}}
    else if(p->t[i].type==J_OBJECT){v[0]=j_num(p,j_get(p,i,"x"),def);v[1]=j_num(p,j_get(p,i,"y"),def);v[2]=j_num(p,j_get(p,i,"z"),def);}
}
static void j_quad(const JP *p,int i,float *v){
    for(int k=0;k<4;k++)v[k]=0;
    if(i<0 || p->t[i].type!=J_ARRAY)return;
    int n=p->t[i].first;for(int k=0;k<4 && n>=0 && n<p->t[i].next;k++){v[k]=j_num(p,n,0);n=p->t[n].next;}
}
static int j_arr(const JP *p,int i,int nth){
    if(i<0 || p->t[i].type!=J_ARRAY)return -1;
    int k=p->t[i].first;
    while(nth-->0 && k>=0 && k<p->t[i].next)k=p->t[k].next;
    return k>=0 && k<p->t[i].next?k:-1;
}
static int bb_find_uuid(const BBModel *m,const char *uuid){
    for(int i=0;i<m->node_count;i++)if(!strcmp(m->nodes[i].uuid,uuid))return i;
    return -1;
}
/* The base64 decoder is used only on data:image/png;base64,... source strings.
   Texture bytes are not trusted as pixels until the host PNG decoder accepts them. */
static int b64val(unsigned char c){if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;}
static void bb_texture_png(const JP *p,int i,BBTexture *t){
    if(i<0 || p->t[i].type!=J_STRING)return;
    const char *s=p->s+p->t[i].start+1, *end=p->s+p->t[i].end-1;
    const char *begin=s;
    while(begin<end && *begin!=',')begin++;
    if(begin==end || begin-s>128 || strncmp(s,"data:image/png;base64,",22))return;
    begin++;
    size_t max=(size_t)(end-begin)*3/4+4;
    if(max>32u*1024u*1024u)return;
    unsigned char *buf=(unsigned char*)malloc(max);
    if(!buf)return;
    size_t n=0;int bits=0,reg=0;
    while(begin<end && *begin!='='){
        int v=b64val((unsigned char)*begin++);
        if(v<0){free(buf);return;}
        reg=(reg<<6)|v;bits+=6;
        if(bits>=8){bits-=8;buf[n++]=(unsigned char)(reg>>bits);}
    }
    if(n<8 || memcmp(buf,"\x89PNG\r\n\x1a\n",8)){free(buf);return;}
    t->png=buf;t->png_size=n;
}
static void bb_read_textures(BBModel *m,const JP *p,int a){
    if(a<0 || p->t[a].type!=J_ARRAY)return;
    for(int i=p->t[a].first;i>=0 && i<p->t[a].next;i=p->t[i].next){
        if(m->texture_count>=BB_MAX_TEXTURES)break;
        BBTexture *t=&m->textures[m->texture_count++];
        j_str(p,j_get(p,i,"name"),t->name,sizeof(t->name));
        t->width=(int)j_num(p,j_get(p,i,"width"),16);
        t->height=(int)j_num(p,j_get(p,i,"height"),16);
        t->uv_width=(int)j_num(p,j_get(p,i,"uv_width"),(float)t->width);
        t->uv_height=(int)j_num(p,j_get(p,i,"uv_height"),(float)t->height);
        bb_texture_png(p,j_get(p,i,"source"),t);
        /* Some Blockbench versions save textures in layers[0].data_url. */
        if(!t->png){int lay=j_arr(p,j_get(p,i,"layers"),0);bb_texture_png(p,j_get(p,lay,"data_url"),t);}
    }
}
static void bb_read_nodes(BBModel *m,const JP *p,int a){
    if(a<0||p->t[a].type!=J_ARRAY)return;
    for(int i=p->t[a].first;i>=0 && i<p->t[a].next;i=p->t[i].next){
        if(m->node_count>=BB_MAX_NODES)break;
        BBNode *n=&m->nodes[m->node_count++];n->parent=-1;
        j_str(p,j_get(p,i,"uuid"),n->uuid,sizeof(n->uuid));
        j_str(p,j_get(p,i,"name"),n->name,sizeof(n->name));
        j_vec(p,j_get(p,i,"origin"),n->origin,0);
        j_vec(p,j_get(p,i,"rotation"),n->rotation,0);
        n->visible=j_bool(p,j_get(p,i,"visibility"),1);
    }
}
static const char *bb_face_names[6]={"north","east","south","west","up","down"};
static void bb_read_cubes(BBModel *m,const JP *p,int a){
    if(a<0||p->t[a].type!=J_ARRAY)return;
    for(int i=p->t[a].first;i>=0 && i<p->t[a].next;i=p->t[i].next){
        if(!j_eq(p,j_get(p,i,"type"),"cube"))continue;
        if(m->cube_count>=BB_MAX_CUBES)break;
        BBCube *c=&m->cubes[m->cube_count++];c->node=-1;
        j_str(p,j_get(p,i,"uuid"),c->name,sizeof(c->name)); /* swapped to display name after outliner */
        j_vec(p,j_get(p,i,"from"),c->from,0);
        j_vec(p,j_get(p,i,"to"),c->to,0);
        j_vec(p,j_get(p,i,"origin"),c->origin,0);
        j_vec(p,j_get(p,i,"rotation"),c->rotation,0);
        c->inflate=j_num(p,j_get(p,i,"inflate"),0);
        c->visible=j_bool(p,j_get(p,i,"visibility"),1);
        int faces=j_get(p,i,"faces");
        for(int f=0;f<6;f++){
            int fd=j_get(p,faces,bb_face_names[f]);BBFace *face=&c->faces[f];
            int tex=j_get(p,fd,"texture");
            face->texture=tex>=0 && p->s[p->t[tex].start]!='n'?(int)j_num(p,tex,-1):-1;
            if(face->texture>=m->texture_count)face->texture=-1;
            j_quad(p,j_get(p,fd,"uv"),face->uv);
            face->uv_rotation=(int)j_num(p,j_get(p,fd,"rotation"),0);
        }
    }
}
static void bb_outliner(BBModel *m,const JP *p,int a,int parent,int depth){
    if(depth>50 || a<0 || p->t[a].type!=J_ARRAY)return;
    for(int e=p->t[a].first;e>=0 && e<p->t[a].next;e=p->t[e].next){
        if(p->t[e].type==J_STRING){
            char uuid[BB_NAME];j_str(p,e,uuid,sizeof(uuid));
            for(int k=0;k<m->cube_count;k++)if(!strcmp(m->cubes[k].name,uuid)){m->cubes[k].node=parent;break;}
        }else if(p->t[e].type==J_OBJECT){
            char uuid[BB_UUID];j_str(p,j_get(p,e,"uuid"),uuid,sizeof(uuid));
            int k=bb_find_uuid(m,uuid);
            if(k>=0){m->nodes[k].parent=parent;bb_outliner(m,p,j_get(p,e,"children"),k,depth+1);}
        }
    }
}
static int bb_key_cmp(const void *a,const void *b){
    const BBKey *x=(const BBKey*)a,*y=(const BBKey*)b;
    return (x->time>y->time)-(x->time<y->time);
}
static void bb_read_animations(BBModel *m,const JP *p,int a){
    if(a<0||p->t[a].type!=J_ARRAY)return;
    for(int c=p->t[a].first;c>=0 && c<p->t[a].next;c=p->t[c].next){
        if(m->clip_count>=BB_MAX_CLIPS)break;
        BBClip *clip=&m->clips[m->clip_count++];
        j_str(p,j_get(p,c,"name"),clip->name,sizeof(clip->name));
        clip->length=j_num(p,j_get(p,c,"length"),0);
        clip->loop=j_eq(p,j_get(p,c,"loop"),"loop");
        clip->first_track=m->track_count;
        int animators=j_get(p,c,"animators");
        if(animators>=0 && p->t[animators].type==J_OBJECT){
            for(int k=p->t[animators].first;k>=0 && k<p->t[animators].next;){
                char id[BB_UUID];j_str(p,k,id,sizeof(id));int node=bb_find_uuid(m,id);
                int av=k+1;k=p->t[av].next;
                if(node<0)continue;
                int kfs=j_get(p,av,"keyframes");
                if(kfs<0 || p->t[kfs].type!=J_ARRAY)continue;
                for(int ch=0;ch<3;ch++){
                    if(m->track_count>=BB_MAX_TRACKS)break;
                    BBTrack tr={node,ch,m->key_count,0};
                    const char *chname=ch==0?"position":ch==1?"rotation":"scale";
                    for(int f=p->t[kfs].first;f>=0 && f<p->t[kfs].next;f=p->t[f].next){
                        if(!j_eq(p,j_get(p,f,"channel"),chname))continue;
                        if(m->key_count>=BB_MAX_KEYS)break;
                        BBKey *key=&m->keys[m->key_count++];
                        key->time=j_num(p,j_get(p,f,"time"),0);
                        int dp=j_arr(p,j_get(p,f,"data_points"),0);
                        j_vec(p,dp,key->value,ch==2?1:0);
                        key->interpolation=j_eq(p,j_get(p,f,"interpolation"),"step")?1:
                                           j_eq(p,j_get(p,f,"interpolation"),"catmullrom")?2:0;
                        tr.key_count++;
                    }
                    if(tr.key_count){
                        qsort(m->keys+tr.first_key,(size_t)tr.key_count,sizeof(BBKey),bb_key_cmp);
                        m->tracks[m->track_count++]=tr;
                    }
                }
            }
        }
        clip->track_count=m->track_count-clip->first_track;
    }
}
int bb_load_memory(BBModel *m,const char *data,size_t length){
    if(!m||!data||!length||length>64u*1024u*1024u)return 0;
    memset(m,0,sizeof(*m));
    JP p={0};p.s=data;p.len=length;
    int root=jp_value(&p);jw(&p);
    if(root<0 || p.failed || p.pos!=p.len || p.t[root].type!=J_OBJECT){
        snprintf(m->error,sizeof(m->error),"Invalid Blockbench JSON");free(p.t);return 0;
    }
    bb_read_textures(m,&p,j_get(&p,root,"textures"));
    bb_read_nodes(m,&p,j_get(&p,root,"groups"));
    bb_read_cubes(m,&p,j_get(&p,root,"elements"));
    bb_outliner(m,&p,j_get(&p,root,"outliner"),-1,0);
    bb_read_animations(m,&p,j_get(&p,root,"animations"));
    free(p.t);
    if(!m->cube_count || !m->node_count){snprintf(m->error,sizeof(m->error),"No cube model/bones found");return 0;}
    return 1;
}
int bb_load_file(BBModel *m,const char *path){
    if(!m||!path)return 0;
    FILE *f=fopen(path,"rb");if(!f){snprintf(m->error,sizeof(m->error),"Could not open %.140s",path);return 0;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return 0;}
    long n=ftell(f);if(n<=0 || n>64*1024*1024){fclose(f);return 0;}
    rewind(f);char *b=(char*)malloc((size_t)n+1);
    if(!b){fclose(f);return 0;}
    size_t got=fread(b,1,(size_t)n,f);fclose(f);b[got]=0;
    int ok=got==(size_t)n && bb_load_memory(m,b,got);free(b);return ok;
}
void bb_free(BBModel *m){if(!m)return;for(int i=0;i<m->texture_count;i++){free(m->textures[i].png);m->textures[i].png=NULL;m->textures[i].png_size=0;}}
int bb_find_node(const BBModel *m,const char *name){if(!m||!name)return -1;for(int i=0;i<m->node_count;i++)if(!strcmp(m->nodes[i].name,name))return i;return -1;}
int bb_find_clip(const BBModel *m,const char *name){
    if(!m||!name)return -1;
    for(int i=0;i<m->clip_count;i++)if(!strcmp(m->clips[i].name,name))return i;
    for(int i=0;i<m->clip_count;i++){
        const char *s=strrchr(m->clips[i].name,'.');if(s && !strcmp(s+1,name))return i;
    }return -1;
}
void bb_pose_identity(const BBModel *m,BBPose *out){(void)m;memset(out,0,sizeof(*out));for(int n=0;n<BB_MAX_NODES;n++)for(int k=0;k<3;k++)out->scale[n][k]=1;}
static void bb_track_sample(const BBModel *m,const BBTrack *tr,float t,float v[3]){
    const BBKey *a=m->keys+tr->first_key;
    const BBKey *b=a+tr->key_count-1;
    if(t<=a->time){memcpy(v,a->value,sizeof(float)*3);return;}
    if(t>=b->time){memcpy(v,b->value,sizeof(float)*3);return;}
    int j=0;while(j+1<tr->key_count && a[j+1].time<t)j++;
    const BBKey *k0=a+j,*k1=k0+1;
    float step=k1->time-k0->time;
    float u=step>0?(t-k0->time)/step:0;
    for(int k=0;k<3;k++){
        if(k0->interpolation==1){v[k]=k0->value[k];continue;}
        if(k0->interpolation==2 && j>0 && j+2<tr->key_count){
            /* Uniform Catmull-Rom approximation (time spacing may be uneven). */
            float a0=a[j-1].value[k],a1=k0->value[k],a2=k1->value[k],a3=a[j+2].value[k];
            v[k]=0.5f*((2*a1)+(-a0+a2)*u+(2*a0-5*a1+4*a2-a3)*u*u+(-a0+3*a1-3*a2+a3)*u*u*u);
        }else v[k]=k0->value[k]+(k1->value[k]-k0->value[k])*u;
    }
}
void bb_sample_pose(const BBModel *m,int clip_index,float seconds,BBPose *out){
    bb_pose_identity(m,out);
    if(!m||clip_index<0||clip_index>=m->clip_count)return;
    const BBClip *clip=&m->clips[clip_index];
    if(clip->length>0){
        if(clip->loop){seconds=fmodf(seconds,clip->length);if(seconds<0)seconds+=clip->length;}
        else {if(seconds<0)seconds=0;if(seconds>clip->length)seconds=clip->length;}
    }
    for(int i=clip->first_track;i<clip->first_track+clip->track_count;i++){
        const BBTrack *tr=&m->tracks[i];float v[3];bb_track_sample(m,tr,seconds,v);
        float (*target)[3]=tr->channel==0?out->translation:tr->channel==1?out->rotation:out->scale;
        for(int k=0;k<3;k++)target[tr->node][k]=v[k];
    }
}
void bb_blend_pose(const BBModel *m,BBPose *a,const BBPose *b,float w){
    if(w<0)w=0;
    if(w>1)w=1;
    if(!m)return;
    for(int i=0;i<m->node_count;i++)for(int k=0;k<3;k++){
        a->translation[i][k]+=(b->translation[i][k]-a->translation[i][k])*w;
        a->rotation[i][k]+=(b->rotation[i][k]-a->rotation[i][k])*w;
        a->scale[i][k]+=(b->scale[i][k]-a->scale[i][k])*w;
    }
}
