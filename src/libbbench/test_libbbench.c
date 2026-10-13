#include "libbbench.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
 BBModel *m=calloc(1,sizeof(*m));BBPose *p=malloc(sizeof(*p));
 if(argc<2 || !m || !p || !bb_load_file(m,argv[1])){fprintf(stderr,"load failed: %s\n",m?m->error:"out of memory");return 1;}
 printf("textures=%d cubes=%d nodes=%d clips=%d tracks=%d keys=%d\n",m->texture_count,m->cube_count,m->node_count,m->clip_count,m->track_count,m->key_count);
 for(int i=0;i<m->clip_count;i++){bb_sample_pose(m,i,m->clips[i].length,p);printf("  %s (%.2fs): %d tracks\n",m->clips[i].name,m->clips[i].length,m->clips[i].track_count);}
 for(int i=0;i<m->texture_count;i++)printf("  texture[%d] %s %zu PNG bytes\n",i,m->textures[i].name,m->textures[i].png_size);
 bb_free(m);free(m);free(p);return 0;
}
