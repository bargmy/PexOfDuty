/* BLOCK / BREACH native PexCraft single-player adaptation.
 * No additional HUD is drawn. Only a Blockbench MAC-10 appears in the
 * first-person renderer. Hands use PexCraft's current skin GL texture.
 * libbbench supplies runtime model+animation data from the .bbmodel file. */
#include "../libbbench/libbbench.h"

#define BREACH_MAG_SIZE 32
static BBModel *g_breach_model = NULL;
static BBPose *g_breach_pose = NULL;
static BBPose *g_breach_other = NULL;
static BBPose *g_breach_base = NULL;
static Texture g_breach_texture[BB_MAX_TEXTURES];
static int g_breach_loaded = 0, g_breach_ammo = BREACH_MAG_SIZE;
static double g_breach_reload_start = -1, g_breach_reload_end = -1;
static double g_breach_last_shot = -100, g_breach_inspect_start = -1;
static double g_breach_auto_fire_start = -1;
static int g_breach_inspecting = 0, g_breach_slide_key_was_down = 0;
static int g_breach_fire_was_down = 0;
static float g_breach_sprint_amount = 0.0f;
static int g_breach_fire_click_pending = 0;
static double g_breach_walk_phase = 0.0;
static float g_breach_motion_amount = 0.0f;
static float g_breach_last_yaw = 0.0f, g_breach_last_pitch = 0.0f;
static float g_breach_sway_yaw = 0.0f, g_breach_sway_pitch = 0.0f;
static float g_breach_jump_offset = 0.0f;
static float g_breach_ground_bob = 1.0f;
static double g_breach_slide_end = -1;
static float g_breach_aim = 0, g_breach_recoil = 0;
static double g_breach_last_draw_time = -1;

static int breach_active(void) {
    return g_screen == SCREEN_INGAME && !g_mp_connected && !g_player_dead;
}

static void breach_queue_fire_click(void) {
    g_breach_fire_click_pending = 1;
}

static void breach_play_shot_sound(int rapid) {
    const char *paths[] = {
        rapid ? "src/assets/bbench/mac10_rapidfire.ogg" : "src/assets/bbench/mac10_oneshot.ogg",
        rapid ? "assets/bbench/mac10_rapidfire.ogg" : "assets/bbench/mac10_oneshot.ogg",
        rapid ? "mac10_rapidfire.ogg" : "mac10_oneshot.ogg"
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        if (file_exists(paths[i])) {
            pex_sound_play_file_direct(paths[i], 0.9f, 1.0f);
            return;
        }
    }
}

static void breach_start_reload(double now) {
    if (g_breach_reload_end >= 0 || g_breach_ammo >= BREACH_MAG_SIZE) return;
    g_breach_reload_start = now;
    g_breach_reload_end = now + (g_breach_ammo ? 1.125 : 1.5);
    g_breach_inspecting = 0;
}

/* Loads the model once when first opening a local world. Source PNGs are
 * decoded through PexCraft's existing cross-platform texture loader. */
static void breach_load(void) {
    if(g_breach_loaded) return;
    g_breach_loaded = 1;
    g_breach_model = (BBModel*)calloc(1,sizeof(BBModel));
    g_breach_pose=(BBPose*)malloc(sizeof(BBPose));
    g_breach_other=(BBPose*)malloc(sizeof(BBPose));
    g_breach_base=(BBPose*)malloc(sizeof(BBPose));
    if(!g_breach_model || !g_breach_pose || !g_breach_other || !g_breach_base)return;
    const char *paths[]={
        "src/assets/bbench/mac-10.bbmodel",
        "assets/bbench/mac-10.bbmodel",
        "bbench/mac-10.bbmodel",
        "mac-10.bbmodel"
    };
    int ok=0;
    for(size_t i=0;i<sizeof(paths)/sizeof(paths[0]);i++){
        if(bb_load_file(g_breach_model,paths[i])){ok=1;break;}
    }
    if(!ok){pex_logf("BLOCK/BREACH: .bbmodel not found; place it in assets/bbench");return;}
    for(int i=0;i<g_breach_model->texture_count;i++){
        BBTexture *t=&g_breach_model->textures[i];
        if(!t->png || t->png_size==0 || !strcmp(t->name,"steve.png"))continue;
        /* load_png_texture() already implements PNG decoding for target
         * desktop backends. The temporary file is removed immediately. */
        char tmp[64],fallback[128];
        snprintf(tmp,sizeof(tmp),"pex_bbench_tex_%d.png",i);
        FILE *f=fopen(tmp,"wb");
        if(f){
            size_t written=fwrite(t->png,1,t->png_size,f);fclose(f);
            if(written==t->png_size)load_png_texture(&g_breach_texture[i],tmp,0);
            remove(tmp);
        }
        if(!g_breach_texture[i].id){
            snprintf(fallback,sizeof(fallback),"src/assets/bbench/tex%d.png",i);
            load_png_texture(&g_breach_texture[i],fallback,0);
        }
    }
    pex_logf("BLOCK/BREACH: loaded %d cubes, %d bones, %d animations",
             g_breach_model->cube_count,g_breach_model->node_count,g_breach_model->clip_count);
}

/* Hitscan only damages zombies. Hip-fire is inaccurate; ADS tightens spread.
 * This path calls existing native PexCraft mob hit/hurt/death handlers. */
static void breach_fire(double now) {
    if(g_breach_ammo<=0 || g_breach_reload_end>=0 || g_breach_inspecting)return;
    if(now-g_breach_last_shot<0.09)return;
    g_breach_last_shot=now;
    --g_breach_ammo;
    g_breach_recoil=fminf(1.6f,g_breach_recoil+0.65f);
    /* Exactly one playback request per accepted bullet, never a timed loop. */
    breach_play_shot_sound(g_breach_fire_was_down && key_down_vk(VK_LBUTTON));
    float dx=0,dy=0,dz=-1;
    pex_touch_aware_look_vector(&dx,&dy,&dz);
    float spread=(0.065f*(1.0f-g_breach_aim)+0.0065f*g_breach_aim);
    float move=sqrtf(g_player_motion_x*g_player_motion_x+g_player_motion_z*g_player_motion_z);
    spread*=1.0f+fminf(move*2.0f,1.5f)+(g_player_on_ground?0.0f:1.0f);
    dx+=(pex_rand_float01()-pex_rand_float01())*spread;
    dy+=(pex_rand_float01()-pex_rand_float01())*spread;
    dz+=(pex_rand_float01()-pex_rand_float01())*spread;
    float len=sqrtf(dx*dx+dy*dy+dz*dz);
    if(len>0.0001f){dx/=len;dy/=len;dz/=len;}
    PassiveMob *target=NULL;
    float best_t=48.0f;
    for(int i=0;i<MAX_PASSIVE_MOBS;i++){
        PassiveMob *m=&g_passive_mobs[i];
        if(!m->active || m->death_time>0 || m->type!=PASSIVE_MOB_ZOMBIE)continue;
        FlatAABB box;passive_mob_aabb(m,&box);
        float t=0;
        if(!passive_ray_intersect_aabb(&box,g_player_x,g_player_y,g_player_z,dx,dy,dz,best_t,&t))continue;
        if(!passive_ray_clear_blocks_125(g_player_x,g_player_y,g_player_z,
                g_player_x+dx*t,g_player_y+dy*t,g_player_z+dz*t))continue;
        if(t>=0 && t<best_t){best_t=t;target=m;}
    }
    if(target){
        PexDamageSource s=pex_damage_source_simple(PEX_DAMAGE_ARROW);
        s.true_kind=PEX_DAMAGE_ENTITY_PLAYER;
        s.true_x=g_player_x;s.true_y=g_player_y;s.true_z=g_player_z;
        pex_mob_attack_entity_from(target,s,7);
    }
}

static void breach_spawn_zombies(void) {
    int zombies=0;
    for(int i=0;i<MAX_PASSIVE_MOBS;i++){
        PassiveMob *m=&g_passive_mobs[i];
        if(m->active && !m->breach_bot && m->death_time==0 && m->type==PASSIVE_MOB_ZOMBIE)zombies++;
        /* Keep new worlds populated by zombies instead of vanilla animals. */
        if(m->active && m->type!=PASSIVE_MOB_ZOMBIE){
            passive_path_release_storage(m);
            memset(m,0,sizeof(*m));
        }
    }
    if(zombies>=10 || (g_ingame_ticks%40)!=0)return;
    for(int tries=0;tries<12;tries++){
        float a=pex_rand_float01()*6.2831853f;
        float r=10.0f+pex_rand_float01()*14.0f;
        int x=(int)floorf(g_player_x+cosf(a)*r);
        int z=(int)floorf(g_player_z+sinf(a)*r);
        if(!flat_chunk_generated_at_block(x,z))continue;
        for(int y=95;y>=2;y--){
            if(!flat_block_is_solid(flat_get_block(x,y-1,z)))continue;
            if(flat_get_block(x,y,z) || flat_get_block(x,y+1,z))continue;
            PassiveMob *m=passive_mob_alloc();if(!m)return;
            passive_mob_init(m,PASSIVE_MOB_ZOMBIE,x+0.5f,(float)y,z+0.5f);
            m->wander_cooldown=0;
            return;
        }
    }
}

static void breach_update_game(void) {
    if(!breach_active()){
        g_breach_reload_end=-1;g_breach_inspecting=0;
        g_breach_aim=0;g_breach_slide_end=-1;
        g_breach_fire_was_down=0;g_breach_auto_fire_start=-1;
        g_breach_sprint_amount=0.0f;
        g_breach_fire_click_pending=0;
        pex_sound_stop_weapon_rapid();
        return;
    }
    double now=now_seconds();
    g_world_time=(g_world_time/24000)*24000+6000; /* keep the Breach world at noon */
    if(!g_player_dead && !g_mp_connected)breach_spawn_zombies();
    breach_bots_update();
    if(g_breach_reload_end>=0 && now>=g_breach_reload_end){
        g_breach_ammo=BREACH_MAG_SIZE;
        g_breach_reload_end=-1;
    }
    int inspecting=key_down_vk('I');
    if(inspecting && !g_breach_inspecting && g_breach_reload_end<0)g_breach_inspect_start=now;
    if(!inspecting)g_breach_inspect_start=-1;
    g_breach_inspecting=inspecting && g_breach_reload_end<0;
    if(g_breach_ammo<=0 && g_breach_reload_end<0)breach_start_reload(now);
    if(key_down_vk('R'))breach_start_reload(now);
    int fire_click=g_breach_fire_click_pending;
    g_breach_fire_click_pending=0;
    int firing=(key_down_vk(VK_LBUTTON) || fire_click) &&
               !g_breach_inspecting && g_breach_reload_end<0;
    if(firing && !g_breach_fire_was_down)g_breach_auto_fire_start=now;
    if(!firing){
        g_breach_auto_fire_start=-1;
        if(g_breach_fire_was_down)pex_sound_stop_weapon_rapid();
    }
    if(firing)breach_fire(now);
    g_breach_fire_was_down=firing;
    /* Momentum slide is initiated by sprint + Ctrl, then eases out. */
    int slide_key=key_down_vk(VK_CONTROL);
    if(slide_key && !g_breach_slide_key_was_down && g_player_sprinting && g_player_on_ground){
        g_breach_slide_end=now+0.68;
        float fwdx=0,fwdy=0,fwdz=0;
        pex_touch_aware_look_vector(&fwdx,&fwdy,&fwdz);
        float h=sqrtf(fwdx*fwdx+fwdz*fwdz);
        if(h>0.1f){g_player_motion_x+=fwdx/h*0.22f;g_player_motion_z+=fwdz/h*0.22f;}
    }
    g_breach_slide_key_was_down=slide_key;
    if(g_breach_slide_end>now && g_player_on_ground){
        g_player_motion_x*=1.09f;
        g_player_motion_z*=1.09f;
    }
}

/* Raw cube vertices (TL, TR, BR, BL as seen from outside). */
static void breach_cube_face(const BBCube *c,int face,float vertices[4][3]){
    float x0=c->from[0]-c->inflate,x1=c->to[0]+c->inflate;
    float y0=c->from[1]-c->inflate,y1=c->to[1]+c->inflate;
    float z0=c->from[2]-c->inflate,z1=c->to[2]+c->inflate;
    static const unsigned char index[6][4]={
      {1,0,4,5},{3,1,5,7},{2,3,7,6},{0,2,6,4},{0,1,3,2},{6,7,5,4}
    };
    float p[8][3]={{x0,y1,z0},{x1,y1,z0},{x0,y1,z1},{x1,y1,z1},
                   {x0,y0,z0},{x1,y0,z0},{x0,y0,z1},{x1,y0,z1}};
    for(int j=0;j<4;j++)for(int k=0;k<3;k++)vertices[j][k]=p[index[face][j]][k]-c->origin[k];
}
static void breach_rotate3(const float *a){
    glRotatef(a[2],0,0,1);glRotatef(a[1],0,1,0);glRotatef(a[0],1,0,0);
}

/* Overlay an action clip as a delta from the draw pose. This keeps the ADS
 * transform in the base pose instead of replacing it with the hip-fire pose. */
static void breach_overlay_action(const BBModel *m, BBPose *pose,
                                  const BBPose *base, const BBPose *action,
                                  int clip_index) {
    if (!m || !pose || !base || !action || clip_index < 0 || clip_index >= m->clip_count) return;
    const BBClip *clip = &m->clips[clip_index];
    for (int i = clip->first_track; i < clip->first_track + clip->track_count; ++i) {
        const BBTrack *track = &m->tracks[i];
        int node = track->node;
        if (node < 0 || node >= m->node_count) continue;
        float (*dst)[3] = track->channel == 0 ? pose->translation :
                          track->channel == 1 ? pose->rotation : pose->scale;
        const float (*ref)[3] = track->channel == 0 ? base->translation :
                                track->channel == 1 ? base->rotation : base->scale;
        const float (*src)[3] = track->channel == 0 ? action->translation :
                                track->channel == 1 ? action->rotation : action->scale;
        for (int k = 0; k < 3; ++k) {
            if (track->channel == 2) {
                float ratio = ref[node][k] != 0.0f ? src[node][k] / ref[node][k] : src[node][k];
                dst[node][k] *= ratio;
            } else {
                dst[node][k] += src[node][k] - ref[node][k];
            }
        }
    }
}
static void breach_render_bones(const BBModel *m,const BBPose *pose,const BBCube *cube){
    int stack[BB_MAX_NODES],count=0,n=cube->node;
    while(n>=0 && n<m->node_count && count<BB_MAX_NODES){
        stack[count++]=n;n=m->nodes[n].parent;
    }
    int parent=-1;
    while(count>0){
        n=stack[--count];const BBNode *b=&m->nodes[n];
        float r[3];
        for(int k=0;k<3;k++){
            float prev=parent>=0?m->nodes[parent].origin[k]:0;
            /* Keyed Blockbench positions are offsets in native model pixels. */
            if(k==0)glTranslatef((b->origin[k]-prev+pose->translation[n][k])/16.0f,0,0);
            else if(k==1)glTranslatef(0,(b->origin[k]-prev+pose->translation[n][k])/16.0f,0);
            else glTranslatef(0,0,(b->origin[k]-prev+pose->translation[n][k])/16.0f);
            r[k]=b->rotation[k]+pose->rotation[n][k];
        }
        breach_rotate3(r);
        glScalef(pose->scale[n][0],pose->scale[n][1],pose->scale[n][2]);
        parent=n;
    }
    float parent_origin[3]={0,0,0};
    if(parent>=0)for(int k=0;k<3;k++)parent_origin[k]=m->nodes[parent].origin[k];
    glTranslatef((cube->origin[0]-parent_origin[0])/16.0f,
                 (cube->origin[1]-parent_origin[1])/16.0f,
                 (cube->origin[2]-parent_origin[2])/16.0f);
    breach_rotate3(cube->rotation);
}
static void breach_draw_first_person(void){
    if(!breach_active())return;
    breach_load();
    if(!g_breach_model || !g_breach_model->cube_count || !g_breach_pose || !g_breach_other || !g_breach_base)return;
    double now=now_seconds();
    int first_frame=g_breach_last_draw_time<0 || now-g_breach_last_draw_time>0.25;
    float dt=g_breach_last_draw_time<0?0.016f:(float)(now-g_breach_last_draw_time);
    g_breach_last_draw_time=now;
    if(dt<0)dt=0;if(dt>0.08f)dt=0.08f;
    float yaw_delta=first_frame?0.0f:g_player_yaw-g_breach_last_yaw;
    while(yaw_delta>180.0f)yaw_delta-=360.0f;
    while(yaw_delta<-180.0f)yaw_delta+=360.0f;
    float pitch_delta=first_frame?0.0f:g_player_pitch-g_breach_last_pitch;
    g_breach_last_yaw=g_player_yaw;g_breach_last_pitch=g_player_pitch;
    float sway_dt=fmaxf(dt,0.001f);
    /* Degrees per second: quick flicks produce more swing than slow turns.
     * A soft limit preserves speed differences instead of abruptly clipping. */
    float yaw_speed=yaw_delta/sway_dt*0.018f;
    float pitch_speed=pitch_delta/sway_dt*0.014f;
    float yaw_target=-yaw_speed/(1.0f+fabsf(yaw_speed)/6.0f);
    float pitch_target=-pitch_speed/(1.0f+fabsf(pitch_speed)/4.0f);
    float sway_blend=1.0f-expf(-30.0f*dt);
    g_breach_sway_yaw+=(yaw_target-g_breach_sway_yaw)*sway_blend;
    g_breach_sway_pitch+=(pitch_target-g_breach_sway_pitch)*sway_blend;
    float speed=sqrtf(g_player_motion_x*g_player_motion_x+g_player_motion_z*g_player_motion_z);
    float sprint_target=(g_player_sprinting && speed>0.015f)?1.0f:0.0f;
    g_breach_sprint_amount+=(sprint_target-g_breach_sprint_amount)*fminf(1.0f,dt*8.0f);
    g_breach_motion_amount+=(speed-g_breach_motion_amount)*fminf(1.0f,dt*10.0f);
    /* Footstep bob is a grounded motion. Fade it instead of abruptly toggling
     * at takeoff/landing or continuing to bounce throughout a running jump. */
    float ground_target=g_player_on_ground?1.0f:0.0f;
    if(first_frame){
        g_breach_ground_bob=ground_target;
        g_breach_jump_offset=0.0f;
    }
    g_breach_ground_bob+=(ground_target-g_breach_ground_bob)*(1.0f-expf(-14.0f*dt));
    float ads_target=key_down_vk(VK_RBUTTON) &&
                     !g_breach_inspecting && g_breach_reload_end<0?1:0;
    float amt=fminf(1.0f,dt*10.0f);
    g_breach_aim+=(ads_target-g_breach_aim)*amt;
    g_breach_recoil=fmaxf(0,g_breach_recoil-dt*5.0f);

    BBModel *m=g_breach_model;
    int idle=bb_find_clip(m,"draw");
    float ilen=idle>=0?m->clips[idle].length:0;
    bb_sample_pose(m,idle,ilen,g_breach_base);
    memcpy(g_breach_pose,g_breach_base,sizeof(BBPose));
    if(g_breach_aim>0.001f && g_breach_reload_end<0 && !g_breach_inspecting){
        int ads=bb_find_clip(m,"ads");
        if(ads>=0){
            bb_sample_pose(m,ads,m->clips[ads].length,g_breach_other);
            bb_blend_pose(m,g_breach_pose,g_breach_other,g_breach_aim);
        }
    }
    int which=-1;float t=0;
    if(g_breach_reload_end>=0){
        which=bb_find_clip(m,g_breach_ammo?"reload":"reload_empty");
        t=(float)(now-g_breach_reload_start);
    }else if(g_breach_inspecting){
        which=bb_find_clip(m,"inspect");
        t=(float)(now-g_breach_inspect_start);
    }else if(g_breach_auto_fire_start>=0 && key_down_vk(VK_LBUTTON)){
        which=bb_find_clip(m,"auto_fire");
        t=(float)(now-g_breach_auto_fire_start);
    }else if(now-g_breach_last_shot<0.5){
        which=bb_find_clip(m,"fire");
        t=(float)(now-g_breach_last_shot);
    }
    if(which>=0){
        bb_sample_pose(m,which,t,g_breach_other);
        if (which == bb_find_clip(m,"fire") || which == bb_find_clip(m,"auto_fire"))
            breach_overlay_action(m,g_breach_pose,g_breach_base,g_breach_other,which);
        else
            bb_blend_pose(m,g_breach_pose,g_breach_other,1.0f);
    }
    /* This is an actual weapon camera, independent of the world camera. */
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    float aspect=(float)g_render_w/(float)(g_render_h>0?g_render_h:1);
    gluPerspective(50.0-3.0*(double)g_breach_aim,aspect,0.025,60.0);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glClear(GL_DEPTH_BUFFER_BIT);
    glRotatef(4.91f,1,0,0);
    glRotatef(1.23f,0,1,0);
    glTranslatef(0.112f,-0.533f,-1.035f);
    float hipfire=1.0f-g_breach_aim;
    float sprint=g_breach_sprint_amount;
    float bob=fminf(g_breach_motion_amount*7.0f,1.0f)*hipfire*g_breach_ground_bob;
    /* Player motion is blocks per 20 Hz tick, not blocks per second. */
    g_breach_walk_phase += (double)fminf(g_breach_motion_amount,0.25f) * (double)dt * 120.0 * g_breach_ground_bob;
    float walk=(float)g_breach_walk_phase;
    float slide=(g_breach_slide_end>now)?(float)((g_breach_slide_end-now)/0.68):0;
    float airborne=g_player_on_ground?0.0f:1.0f;
    /* A single small, eased lift/drop rather than velocity-dependent offsets
     * layered on sprint bob. ADS suppresses this at application time. */
    float jump_lag=airborne*0.009f;
    g_breach_jump_offset+=(jump_lag-g_breach_jump_offset)*(1.0f-expf(-10.0f*dt));
    float run_bob=bob*(1.0f+sprint*0.35f);
    float sway_weight=hipfire*0.85f+0.15f;
    glTranslatef(sinf(walk)*0.012f*run_bob+sprint*0.006f*hipfire-g_breach_sway_yaw*0.003f*sway_weight,
                 sinf(walk*2.0f)*0.009f*run_bob-g_breach_jump_offset*hipfire-
                     sprint*0.035f*hipfire-slide*0.035f*hipfire-g_breach_recoil*0.012f,0);
    glRotatef(sinf(walk)*1.2f*run_bob+slide*4.0f*hipfire+sprint*(1.5f*bob+2.0f)*hipfire,0,0,1);
    glRotatef(-g_breach_sway_yaw*sway_weight,0,1,0);
    glRotatef(-g_breach_recoil*4.0f-g_breach_sway_pitch*sway_weight-sprint*1.5f*hipfire,1,0,0);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1,1,1,1);
    Texture *skin=pex_java47_local_skin_texture();
    if(!skin || !skin->id)skin=&tex_steve;
    for(int i=0;i<m->cube_count;i++){
        const BBCube *c=&m->cubes[i];if(!c->visible)continue;
        /* Hide model's bullet helper elements, but keep the muzzle flash. */
        glPushMatrix();
        breach_render_bones(m,g_breach_pose,c);
        for(int f=0;f<6;f++){
            const BBFace *face=&c->faces[f];int ti=face->texture;
            if(ti<0 || ti>=m->texture_count)continue;
            Texture *tex=&g_breach_texture[ti];
            if(!strcmp(m->textures[ti].name,"steve.png") && skin && skin->id)tex=skin;
            if(!tex || !tex->id)continue;
            glBindTexture(GL_TEXTURE_2D,tex->id);
            float quad[4][3];breach_cube_face(c,f,quad);
            int tw=m->textures[ti].uv_width,th=m->textures[ti].uv_height;
            if(tw<=0)tw=16;if(th<=0)th=16;
            glBegin(GL_QUADS);
            for(int j=0;j<4;j++){
                int k=(j-((face->uv_rotation/90)%4)+4)%4;
                float u=k==0||k==3?face->uv[0]:face->uv[2];
                float v=k<2?face->uv[1]:face->uv[3];
                glTexCoord2f(u/tw,v/th);
                glVertex3f(quad[j][0]/16.0f,quad[j][1]/16.0f,quad[j][2]/16.0f);
            }
            glEnd();
        }
        glPopMatrix();
    }
    glDisable(GL_DEPTH_TEST);
    glColor4f(1,1,1,1);
}
