/* PexOfDuty LAN v5. Windows TCP carries framed, host-authoritative game state
 * and native map chunks; UDP broadcasts discover hosts (no subnet/port scan).
 * All sockets are nonblocking and serviced on the main thread in LAN mode. */
#if PEX_LAN_SUPPORTED
#define LAN_MAGIC 0x31444f50u
#define LAN_FRAME_MAX 196608u
#define LAN_IO_CAP (2u*1024u*1024u)
enum { LAN_HELLO=1, LAN_WELCOME, LAN_CHUNK, LAN_READY, LAN_INPUT, LAN_SNAPSHOT, LAN_ERROR, LAN_CHUNK_UPDATE, LAN_TEAM, LAN_SKIN, LAN_BOTS };
#pragma pack(push,1)
typedef struct LanHeader { uint32_t magic; uint16_t version, type; uint32_t bytes; } LanHeader;
typedef struct LanHuman {
    uint32_t active, epoch;
    uint32_t acknowledged_shots;
    int32_t health, ammo, flash;
    float x,y,z,yaw,pitch;
    char name[32];
} LanHuman;
typedef struct LanWelcome {
    uint32_t id, chunks;
    int64_t seed;
    LanHuman self;
    float marker[4], spawns[16][4];
    uint32_t spawn_count;
} LanWelcome;
typedef struct LanInput {
    float x,y,z,yaw,pitch,aim;
    uint32_t shots, reloads, respawns, weapon, selected_weapon;
} LanInput;
typedef struct LanMob {
    uint32_t slot;
    int32_t type, bot, entity, health, death, flash;
    int32_t team;
    float x,y,z,yaw,head,pitch,limb,move;
} LanMob;
typedef struct LanSnapshot {
    LanHuman humans[6];
    int32_t phase, countdown, teams[6];
    int32_t bots_enabled, killer_id[6], killer_bot[6], weapons[6];
    float aims[6];
    int32_t atomic_hurt[6];
    char killer_name[6][32];
    uint32_t mob_count;
    LanMob mobs[MAX_PASSIVE_MOBS];
    int32_t mob_atomic_hurt[MAX_PASSIVE_MOBS];
    BreachGunDrop drops[BREACH_MAX_GUN_DROPS];
} LanSnapshot;
#pragma pack(pop)
typedef struct LanPeer {
    SOCKET socket;
    unsigned char *rx, *tx;
    size_t rx_len, tx_len;
    int joined, ready, chunks_sent;
    int catchup;
    FILE *manifest;
    double seen, last_shot, reload_end, last_move, hurt_until;
    uint32_t shots, reloads, respawns;
    int wants_reload;
    float aim;
    int weapon, killer_id, killer_bot, skip_allowed;
    int selected_weapon,pickup_locked,atomic_hurt;
    double died_at;
    char killer_name[32];
    LanHuman human;
} LanPeer;
static LanPeer g_lan_peers[6];
static SOCKET g_lan_listener=INVALID_SOCKET, g_lan_discovery=INVALID_SOCKET, g_lan_advertiser=INVALID_SOCKET;
static int g_lan_socket_initialized, g_lan_port, g_lan_self_id=-1;
static LanPeer g_lan_connection;
static LanWelcome g_lan_welcome;
static int g_lan_chunks_received, g_lan_client_entered, g_lan_loading;
static char g_lan_cache[MAX_PATHBUF];
static uint32_t g_lan_shots, g_lan_reloads, g_lan_respawns, g_lan_epoch;
static double g_lan_query_time=-100, g_lan_snapshot_time;
static char g_lan_failure[160];
static double g_lan_start_at;
static int lan_queue(LanPeer *p,int type,const void *data,size_t bytes);
typedef struct LanSkin {uint32_t id,width,height,bytes;unsigned char rgba[64*64*4];} LanSkin;
static LanSkin g_lan_skins[6];
static Texture g_lan_skin_textures[6];
static uint32_t g_lan_local_skin_hash;
static int g_lan_last_human_attacker=-1;
typedef struct LanReplayPose { float x,y,z,yaw,pitch,aim; int valid,weapon,flash,health; } LanReplayPose;
static LanReplayPose g_lan_replay_history[100][MAX_PASSIVE_MOBS+6];
static LanReplayPose g_lan_replay_clip[100];
static LanReplayPose g_lan_replay_scene[100][MAX_PASSIVE_MOBS+6];
static int g_lan_replay_head,g_lan_replay_count,g_lan_replay_length;
static float g_lan_replay_aims[6];
static int g_lan_replay_weapons[6];
static void lan_replay_record(void) {
    if(g_lan_phase!=2)return;
    LanReplayPose *row=g_lan_replay_history[g_lan_replay_head];memset(row,0,sizeof(g_lan_replay_history[0]));
    if(pex_lan_host())row[0]=(LanReplayPose){g_player_x,g_player_y,g_player_z,g_player_yaw,g_player_pitch,g_breach_aim,!g_player_dead,breach_weapon_current(),now_seconds()-g_breach_last_shot<.1};
    else if(g_lan_self_id>=0)row[g_lan_self_id]=(LanReplayPose){g_player_x,g_player_y,g_player_z,g_player_yaw,g_player_pitch,g_breach_aim,!g_player_dead,breach_weapon_current(),now_seconds()-g_breach_last_shot<.1};
    if(g_lan_self_id>=0){row[g_lan_self_id].valid=1;row[g_lan_self_id].health=g_player_health;}
    for(int i=0;i<MAX_PASSIVE_MOBS;++i){PassiveMob *m=&g_passive_mobs[i];if(!m->active)continue;
        int slot=m->breach_remote_player?m->breach_remote_player-1:6+i;
        row[slot]=(LanReplayPose){m->x,m->y+1.62f,m->z,m->head_yaw,m->pitch,0,1,0,m->breach_bot_flash>0};
        row[slot].health=m->health;
        if(pex_lan_host()&&m->breach_remote_player){LanPeer *p=&g_lan_peers[slot];row[slot].aim=p->aim;row[slot].weapon=p->weapon;}
        else if(m->breach_remote_player){row[slot].aim=g_lan_replay_aims[slot];row[slot].weapon=g_lan_replay_weapons[slot];}
    }
    g_lan_replay_head=(g_lan_replay_head+1)%100;if(g_lan_replay_count<100)++g_lan_replay_count;
}
static void lan_replay_capture(int bot,int id) {
    g_lan_replay_length=0;int slot=bot?6+id:id;if(slot<0||slot>=MAX_PASSIVE_MOBS+6)return;
    for(int i=0;i<g_lan_replay_count;++i){int index=(g_lan_replay_head-g_lan_replay_count+i+100)%100;
        LanReplayPose p=g_lan_replay_history[index][slot];if(p.valid){
            memcpy(g_lan_replay_scene[g_lan_replay_length],g_lan_replay_history[index],sizeof(g_lan_replay_history[0]));
            g_lan_replay_clip[g_lan_replay_length++]=p;
        }
    }
}
static int lan_replay_index(void) {
    int ticks=g_lan_killcam_ticks<90?g_lan_killcam_ticks:90;
    int i=ticks*(g_lan_replay_length-1)/90;
    return i<g_lan_replay_length?i:g_lan_replay_length-1;
}
static void lan_replay_draw_weapon(void) {
    if(!g_lan_killcam_active||g_lan_replay_length<=0)return;
    LanReplayPose *p=&g_lan_replay_clip[lan_replay_index()];
    int weapon=g_breach_equipped_weapon,inspecting=g_breach_inspecting,replay=g_breach_replay_render;
    float aim=g_breach_aim,recoil=g_breach_recoil;
    double shot=g_breach_last_shot,reload=g_breach_reload_end,autofire=g_breach_auto_fire_start;
    g_breach_equipped_weapon=p->weapon>=0&&p->weapon<BREACH_WEAPON_COUNT?p->weapon:0;g_breach_aim=p->aim;g_breach_recoil=p->flash?.15f:0;
    g_breach_reload_end=-1;g_breach_inspecting=0;g_breach_auto_fire_start=-1;
    g_breach_last_shot=p->flash?now_seconds():-100;g_breach_replay_render=1;
    breach_draw_first_person();
    g_breach_equipped_weapon=weapon;g_breach_aim=aim;g_breach_recoil=recoil;
    g_breach_reload_end=reload;g_breach_inspecting=inspecting;g_breach_auto_fire_start=autofire;
    g_breach_last_shot=shot;g_breach_replay_render=replay;g_breach_last_draw_time=-1;
}
static PassiveMob g_lan_replay_saved_mobs[MAX_PASSIVE_MOBS];
static void lan_replay_scene_begin(void) {
    memcpy(g_lan_replay_saved_mobs,g_passive_mobs,sizeof(g_lan_replay_saved_mobs));
    LanReplayPose *row=g_lan_replay_scene[lan_replay_index()];
    for(int i=0;i<MAX_PASSIVE_MOBS;++i){PassiveMob *m=&g_passive_mobs[i];
        int id=i>=MAX_PASSIVE_MOBS-6?i-(MAX_PASSIVE_MOBS-6):6+i;
        LanReplayPose *p=&row[id];int killer=id==(g_lan_killcam_killer_bot?6+g_lan_killcam_killer_id:g_lan_killcam_killer_id);
        if(!p->valid||killer){m->active=0;continue;}
        m->active=1;m->type=PASSIVE_MOB_ZOMBIE;m->breach_bot=1;m->health=p->health;m->death_time=p->health<=0?10:0;
        m->breach_remote_player=id<6?id+1:0;
        m->x=m->prev_x=p->x;m->y=m->prev_y=p->y-1.62f;m->z=m->prev_z=p->z;
        m->yaw=m->prev_yaw=m->head_yaw=m->prev_head_yaw=m->render_yaw=m->prev_render_yaw=p->yaw;
        m->pitch=m->prev_pitch=p->pitch;m->breach_bot_flash=p->flash;
    }
}
static void lan_replay_scene_end(void) { memcpy(g_passive_mobs,g_lan_replay_saved_mobs,sizeof(g_lan_replay_saved_mobs)); }

static void pex_lan_killcam_begin(int killer_bot,int killer_id,const char *name) {
    if(!pex_lan_active())return;
    g_lan_killcam_active=1;g_lan_killcam_ticks=0;g_lan_killcam_respawn_sent=0;
    g_lan_killcam_killer_bot=killer_bot!=0;g_lan_killcam_killer_id=killer_id;
    g_lan_killcam_skip_allowed=!killer_bot;
    lan_replay_record();
    lan_replay_capture(killer_bot,killer_id);
    snprintf(g_lan_killcam_killer_name,sizeof(g_lan_killcam_killer_name),"%.47s",name&&name[0]?name:(killer_bot?"Bot":"Player"));
}
static void pex_lan_killcam_from_source(const PexDamageSource *source) {
    if(!pex_lan_host()||!source)return;
    if(g_lan_last_human_attacker>=0){
        int id=g_lan_last_human_attacker;g_lan_last_human_attacker=-1;
        pex_lan_killcam_begin(0,id,id<6&&g_lan_names[id][0]?g_lan_names[id]:"Player");return;
    }
    if(source->true_kind==PEX_DAMAGE_ENTITY_MOB&&source->true_mob_index>=0&&source->true_mob_index<MAX_PASSIVE_MOBS){
        PassiveMob *m=&g_passive_mobs[source->true_mob_index];if(!m->active)return;
        if(m->breach_remote_player){int id=m->breach_remote_player-1;
            pex_lan_killcam_begin(0,id,id>=0&&id<6&&g_lan_names[id][0]?g_lan_names[id]:"Player");}
        else if(m->breach_bot)pex_lan_killcam_begin(1,source->true_mob_index,m->breach_team?"Blue Bot":"Red Bot");
    }
}
static void pex_lan_killcam_skip(void) {
    if(g_lan_killcam_active&&g_lan_killcam_skip_allowed)g_lan_killcam_ticks=100;
}
static void pex_lan_killcam_tick(void) {
    if(!g_lan_killcam_active)return;
    if(g_lan_killcam_ticks<100)++g_lan_killcam_ticks;
    if(g_lan_killcam_ticks<100||g_lan_killcam_respawn_sent)return;
    g_lan_killcam_respawn_sent=1;
    if(pex_lan_client())pex_lan_respawn();
    else player_respawn();
}
static void pex_lan_toggle_bots(void) {
    if(!pex_lan_host()||g_lan_phase!=0)return;
    g_lan_bots_enabled=!g_lan_bots_enabled;
    breach_bots_update();rebuild_screen();
}
static int pex_lan_killcam_camera(float *x,float *y,float *z,float *yaw,float *pitch) {
    if(!g_lan_killcam_active)return 0;
    if(g_lan_replay_length>0){
        int index=lan_replay_index();
        LanReplayPose *p=&g_lan_replay_clip[index];
        if(x)*x=p->x;if(y)*y=p->y;if(z)*z=p->z;if(yaw)*yaw=p->yaw;if(pitch)*pitch=p->pitch;return 1;
    }
    float px=0,py=0,pz=0,pyaw=0,ppitch=0;int found=0;
    if(!g_lan_killcam_killer_bot&&g_lan_killcam_killer_id==0&&pex_lan_host()){
        px=g_player_x;py=g_player_y;pz=g_player_z;pyaw=g_player_yaw;ppitch=g_player_pitch;found=1;
    }else if(!g_lan_killcam_killer_bot&&g_lan_killcam_killer_id>=0&&g_lan_killcam_killer_id<6){
        int id=g_lan_killcam_killer_id;
        if(pex_lan_host()&&id==0){px=g_player_x;py=g_player_y;pz=g_player_z;pyaw=g_player_yaw;ppitch=g_player_pitch;found=1;}
        else if(pex_lan_host()&&g_lan_peers[id].joined){LanPeer *p=&g_lan_peers[id];px=p->human.x;py=p->human.y;pz=p->human.z;pyaw=p->human.yaw;ppitch=p->human.pitch;found=1;}
        else {PassiveMob *m=&g_passive_mobs[MAX_PASSIVE_MOBS-6+id];
            if(m->active){px=m->x;py=m->y+1.62f;pz=m->z;pyaw=m->yaw;ppitch=m->pitch;found=1;}}
    }else if(g_lan_killcam_killer_bot&&g_lan_killcam_killer_id>=0&&g_lan_killcam_killer_id<MAX_PASSIVE_MOBS){
        PassiveMob *m=&g_passive_mobs[g_lan_killcam_killer_id];
        if(m->active){px=m->x;py=m->y+1.62f;pz=m->z;pyaw=m->yaw;ppitch=m->pitch;found=1;}
    }
    if(!found)return 0;
    if(x)*x=px;if(y)*y=py;if(z)*z=pz;if(yaw)*yaw=pyaw;if(pitch)*pitch=ppitch;return 1;
}

static int lan_skin_valid(const LanSkin *skin,size_t bytes) {
    return bytes>=16&&skin->id<6&&skin->width==64&&(skin->height==32||skin->height==64)&&
           skin->bytes==64*skin->height*4&&bytes==16+skin->bytes;
}
static void lan_store_skin(const LanSkin *skin) {
    g_lan_skins[skin->id]=*skin;
#ifndef PEX_LAN_HEADLESS_TEST
    unsigned char *copy=(unsigned char*)malloc(skin->bytes);if(!copy)return;
    memcpy(copy,skin->rgba,skin->bytes);
    (void)upload_rgba_texture(&g_lan_skin_textures[skin->id],64,(int)skin->height,copy,0);
#endif
}
static Texture *pex_lan_skin_texture(int id) {
    if(id>=0&&id<6&&g_lan_skin_textures[id].id)return &g_lan_skin_textures[id];
    return tex_steve_default.id?&tex_steve_default:&tex_steve;
}
static void pex_lan_send_skin(void) {
    if(!pex_lan_active()||g_lan_self_id<0||!tex_steve.rgba||tex_steve.w!=64||(tex_steve.h!=32&&tex_steve.h!=64))return;
    LanSkin skin={0};skin.id=(uint32_t)g_lan_self_id;skin.width=64;skin.height=tex_steve.h;skin.bytes=64*skin.height*4;
    memcpy(skin.rgba,tex_steve.rgba,skin.bytes);lan_store_skin(&skin);
    if(pex_lan_host()){
        for(int i=1;i<6;++i)if(g_lan_peers[i].joined)(void)lan_queue(&g_lan_peers[i],LAN_SKIN,&skin,16+skin.bytes);
    }else (void)lan_queue(&g_lan_connection,LAN_SKIN,&skin,16+skin.bytes);
    uint32_t hash=2166136261u;for(uint32_t i=0;i<skin.bytes;++i)hash=(hash^skin.rgba[i])*16777619u;
    g_lan_local_skin_hash=hash^(uint32_t)skin.height;
}
static int lan_receive_skin(LanPeer *p,int id,const unsigned char *data,size_t bytes) {
    if(bytes<16||bytes>sizeof(LanSkin))return 0;
    LanSkin skin={0};memcpy(&skin,data,bytes);
    if(pex_lan_host()){
        if(!p->joined)return 0;
        skin.id=(uint32_t)id; /* identity belongs to socket, not untrusted payload */
    }
    if(!lan_skin_valid(&skin,bytes))return 0;
    lan_store_skin(&skin);
    if(pex_lan_host())for(int i=1;i<6;++i)if(i!=id&&g_lan_peers[i].joined)
        (void)lan_queue(&g_lan_peers[i],LAN_SKIN,&skin,bytes);
    return 1;
}

static void lan_lobby_names(void) {
    g_lan_present[0]=1;snprintf(g_lan_names[0],32,"%.31s",g_multiplayer_username);
    for(int i=1;i<6;++i){g_lan_present[i]=g_lan_peers[i].joined;
        snprintf(g_lan_names[i],32,"%.31s",g_lan_peers[i].human.name);}
}
static int lan_set_team(int id,int team) {
    if(g_lan_phase!=0||id<0||id>=6||team<0||team>1)return 0;
    lan_lobby_names();
    if(g_lan_teams[id]!=team&&pex_lan_team_count(team)>=3)return 0;
    g_lan_teams[id]=team;return 1;
}
static void pex_lan_choose_team(int team) {
    if(pex_lan_host()){(void)lan_set_team(0,team);rebuild_screen();}
    else if(pex_lan_client()&&g_lan_phase==0){int32_t t=team;(void)lan_queue(&g_lan_connection,LAN_TEAM,&t,sizeof(t));}
}
static int pex_lan_friendly(const PassiveMob *m) {
    if(!pex_lan_active()||g_lan_self_id<0||!m->breach_bot)return 0;
    int team=m->breach_remote_player?g_lan_teams[m->breach_remote_player-1]:m->breach_team;
    return team==g_lan_teams[g_lan_self_id];
}
typedef struct LanDirtyChunk {int cx,cz,pending;} LanDirtyChunk;
static LanDirtyChunk g_lan_dirty_chunks[2048];
static int g_lan_dirty_count;

static void pex_lan_mark_chunk(int cx,int cz) {
    if(!pex_lan_host())return;
    for(int i=0;i<g_lan_dirty_count;++i)if(g_lan_dirty_chunks[i].cx==cx&&g_lan_dirty_chunks[i].cz==cz){g_lan_dirty_chunks[i].pending=1;return;}
    if(g_lan_dirty_count<2048)g_lan_dirty_chunks[g_lan_dirty_count++]=(LanDirtyChunk){cx,cz,1};
}

/* Capture current, loaded terrain rather than a possibly stale disk save. */
static unsigned char *lan_capture_chunk(int cx,int cz,size_t *bytes) {
    if(!flat_chunk_generated_at_block(cx*16,cz*16))return NULL;
    unsigned char *blocks=(unsigned char*)malloc(FLAT_CHUNK_BLOCK_COUNT),*meta=(unsigned char*)malloc(FLAT_CHUNK_BLOCK_COUNT);
    uLongf capacity=compressBound(FLAT_CHUNK_BLOCK_COUNT),bl=capacity,ml=capacity;
    unsigned char *wire=(unsigned char*)malloc(28+capacity*2);
    if(!blocks||!meta||!wire){free(blocks);free(meta);free(wire);return NULL;}
    flat_world_map_enter();
    for(int y=0;y<FLAT_WORLD_HEIGHT;++y)for(int z=0;z<16;++z){
        int sx=flat_storage_x_world(cx*16),sz=flat_storage_z_world(cz*16+z);
        memcpy(blocks+y*256+z*16,&g_flat_blocks[y][sz][sx],16);
        memcpy(meta+y*256+z*16,&g_flat_meta[y][sz][sx],16);
    }
    flat_world_map_leave();
    int ok=compress2(wire+28,&bl,blocks,FLAT_CHUNK_BLOCK_COUNT,Z_BEST_SPEED)==Z_OK;
    if(ok)ok=compress2(wire+28+bl,&ml,meta,FLAT_CHUNK_BLOCK_COUNT,Z_BEST_SPEED)==Z_OK;
    free(blocks);free(meta);
    if(!ok){free(wire);return NULL;}
    memcpy(wire,"PXCDLTZ1",8);int32_t count=FLAT_CHUNK_BLOCK_COUNT;uint32_t b=(uint32_t)bl,m=(uint32_t)ml;
    memcpy(wire+8,&cx,4);memcpy(wire+12,&cz,4);memcpy(wire+16,&count,4);memcpy(wire+20,&b,4);memcpy(wire+24,&m,4);
    *bytes=28+(size_t)bl+(size_t)ml;return wire;
}

static int lan_nonblocking(SOCKET s) { u_long one=1; return ioctlsocket(s,FIONBIO,&one)==0; }
static int lan_would_block(void) { return WSAGetLastError()==WSAEWOULDBLOCK; }
static int lan_init(void) {
    if(g_lan_socket_initialized)return 1;
    WSADATA data;
    if(WSAStartup(MAKEWORD(2,2),&data))return 0;
    for(int i=0;i<6;++i)g_lan_peers[i].socket=INVALID_SOCKET;
    g_lan_connection.socket=INVALID_SOCKET;
    g_lan_socket_initialized=1;
    return 1;
}
static void lan_close_peer(LanPeer *p) {
    if(p->socket!=INVALID_SOCKET)closesocket(p->socket);
    if(p->manifest)fclose(p->manifest);
    free(p->rx);free(p->tx);
    memset(p,0,sizeof(*p));p->socket=INVALID_SOCKET;
}
static int lan_attach(LanPeer *p,SOCKET socket) {
    memset(p,0,sizeof(*p));p->socket=socket;
    p->rx=(unsigned char*)malloc(LAN_IO_CAP);p->tx=(unsigned char*)malloc(LAN_IO_CAP);
    p->seen=now_seconds();p->last_shot=-100;p->reload_end=-1;
    if(!p->rx||!p->tx||!lan_nonblocking(socket)){lan_close_peer(p);return 0;}
    int one=1;setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,(const char*)&one,sizeof(one));
    return 1;
}
static int lan_queue(LanPeer *p,int type,const void *data,size_t bytes) {
    if(bytes>LAN_FRAME_MAX||p->tx_len+sizeof(LanHeader)+bytes>LAN_IO_CAP)return 0;
    LanHeader h={LAN_MAGIC,PEX_LAN_PROTOCOL_VERSION,(uint16_t)type,(uint32_t)bytes};
    memcpy(p->tx+p->tx_len,&h,sizeof(h));p->tx_len+=sizeof(h);
    if(bytes){memcpy(p->tx+p->tx_len,data,bytes);p->tx_len+=bytes;}
    return 1;
}
static int lan_flush(LanPeer *p) {
    if(!p->tx_len)return 1;
    int n=send(p->socket,(const char*)p->tx,(int)p->tx_len,0);
    if(n<0)return lan_would_block();
    if(n==0)return 0;
    memmove(p->tx,p->tx+n,p->tx_len-(size_t)n);p->tx_len-=(size_t)n;return 1;
}
static int pex_lan_humans(void) {
    if(!pex_lan_host())return pex_lan_client()?2:1;
    int n=1;for(int i=1;i<6;++i)if(g_lan_peers[i].joined)++n;return n;
}
static void lan_shot_sound(float x,float y,float z) {
    float dx=x-g_player_x,dy=y-g_player_y,dz=z-g_player_z;
    float distance=sqrtf(dx*dx+dy*dy+dz*dz);
    const char *paths[]={"src/assets/bbench/mac10_oneshot.ogg","assets/bbench/mac10_oneshot.ogg","mac10_oneshot.ogg"};
    for(int i=0;i<3;++i)if(file_exists(paths[i])){pex_sound_play_file_direct(paths[i],PEX_SHOT_REMOTE_VOLUME/(1+distance*.10f),1);break;}
}
static void lan_proxy(int id,const LanHuman *h) {
    if(id==g_lan_self_id)return;
    int slot=MAX_PASSIVE_MOBS-6+id;
    PassiveMob *m=&g_passive_mobs[slot];
    if(!h->active){if(m->breach_remote_player==id+1)m->active=0;return;}
    if(!m->active||m->breach_remote_player!=id+1){
        passive_path_release_storage(m);
        passive_mob_init(m,PASSIVE_MOB_ZOMBIE,h->x,h->y-1.62f,h->z);
        m->breach_bot=1;m->breach_remote_player=id+1;
    }
    if(h->flash>0&&m->breach_bot_flash==0){
        lan_shot_sound(h->x,h->y,h->z);
        float yaw=h->yaw*(float)M_PI/180,pitch=h->pitch*(float)M_PI/180;
        breach_spawn_bullet_visual(h->x,h->y-.08f,h->z,-sinf(yaw)*cosf(pitch),-sinf(pitch),cosf(yaw)*cosf(pitch),48);
    }
    if(h->health<m->health)m->hurt_time=10;else if(m->hurt_time>0)--m->hurt_time;
    float dx=h->x-m->x,dz=h->z-m->z;
    m->prev_x=m->x;m->prev_y=m->y;m->prev_z=m->z;
    m->prev_yaw=m->yaw;m->prev_render_yaw=m->render_yaw;m->prev_head_yaw=m->head_yaw;m->prev_pitch=m->pitch;
    m->x=h->x;m->y=h->y-1.62f;m->z=h->z;
    m->yaw=m->render_yaw=m->head_yaw=h->yaw;m->pitch=h->pitch;
    m->health=h->health;m->death_time=h->health<=0?1:0;m->breach_bot_flash=h->flash;
    m->breach_team=g_lan_teams[id];
    m->prev_limb_amount=m->limb_amount;m->prev_limb_swing=m->limb_swing;
    m->limb_amount=fminf(1,sqrtf(dx*dx+dz*dz)*4);m->limb_swing+=m->limb_amount;
}
static void lan_remove_peer(int id) {
    lan_close_peer(&g_lan_peers[id]);
    memset(&g_lan_skins[id],0,sizeof(g_lan_skins[id]));free_texture(&g_lan_skin_textures[id]);
    LanHuman empty={0};lan_proxy(id,&empty);
    lan_lobby_names();if(pex_lan_waiting())rebuild_screen();
}
static int pex_lan_hit_human(int id,int damage,const PexDamageSource *source) {
    if(!pex_lan_host()||id<0||id>=6)return 0;
    if(pex_lan_waiting())return 1;
    if(source&&source->true_kind==PEX_DAMAGE_ENTITY_MOB&&source->true_mob_index>=0&&source->true_mob_index<MAX_PASSIVE_MOBS&&
       g_passive_mobs[source->true_mob_index].breach_team==g_lan_teams[id])return 1;
    if(id==0){if(source)(void)player_attack_entity_from(*source,damage);return 1;}
    LanPeer *p=&g_lan_peers[id];
    if(!p->joined||!p->ready||p->human.health<=0)return 1;
    if(now_seconds()<p->hurt_until)return 1;
    p->hurt_until=now_seconds()+0.04;
    p->human.health-=damage;if(p->human.health<0)p->human.health=0;
    p->atomic_hurt=source&&source->breach_atomic;
    g_passive_mobs[MAX_PASSIVE_MOBS-6+id].breach_atomic_hurt=p->atomic_hurt;
    if(p->human.health<=0){
        breach_drop_gun(p->human.x,p->human.y-1.62f,p->human.z,p->weapon);
        int bot=source&&source->true_kind==PEX_DAMAGE_ENTITY_MOB;
        int killer=bot?source->true_mob_index:(g_lan_last_human_attacker>=0?g_lan_last_human_attacker:g_lan_self_id);
        p->killer_id=killer;p->killer_bot=bot;p->skip_allowed=!bot;
        snprintf(p->killer_name,sizeof(p->killer_name),"%.31s",bot?"Bot":(killer>=0&&killer<6&&g_lan_names[killer][0]?g_lan_names[killer]:"Player"));
        p->died_at=now_seconds();
    }
    lan_proxy(id,&p->human);return 1;
}
static int pex_lan_bot_target(float x,float y,float z,float *tx,float *ty,float *tz) {
    *tx=g_player_x;*ty=g_player_y;*tz=g_player_z;
    int best=g_player_dead?-1:0;float score=1e12f;
    if(!g_player_dead){float dx=*tx-x,dy=*ty-y,dz=*tz-z;score=dx*dx+dy*dy+dz*dz;
        if(!passive_ray_clear_blocks_125(x,y,z,*tx,*ty,*tz))score+=100000;}
    if(pex_lan_host())for(int i=1;i<6;++i){
        LanPeer *p=&g_lan_peers[i];if(!p->joined||!p->ready||p->human.health<=0)continue;
        float dx=p->human.x-x,dy=p->human.y-y,dz=p->human.z-z;
        float s=dx*dx+dy*dy+dz*dz;
        if(!passive_ray_clear_blocks_125(x,y,z,p->human.x,p->human.y,p->human.z))s+=100000;
        if(s<score){score=s;best=i;*tx=p->human.x;*ty=p->human.y;*tz=p->human.z;}
    }
    return best;
}
static void lan_spawn_human(LanPeer *p) {
    p->weapon=p->selected_weapon;p->pickup_locked=0;p->atomic_hurt=0;
    breach_map_pick_spawn(g_loaded_world_dir,&p->human.x,&p->human.y,&p->human.z,&p->human.yaw);
    float points[16][4];int count=breach_map_spawn_points(g_loaded_world_dir,points);
    float best=-1;int index=-1;
    for(int i=0;i<count;++i){
        if(flat_player_aabb_collides(points[i][0],points[i][1]+1.62f,points[i][2]))continue;
        float dx=points[i][0]-g_player_x,dz=points[i][2]-g_player_z;
        float spacing=g_player_dead?1e12f:dx*dx+dz*dz;
        for(int j=0;j<MAX_PASSIVE_MOBS;++j){
            PassiveMob *m=&g_passive_mobs[j];if(!m->active||m->death_time>0)continue;
            dx=points[i][0]-m->x;dz=points[i][2]-m->z;spacing=fminf(spacing,dx*dx+dz*dz);
        }
        if(spacing>best){best=spacing;index=i;}
    }
    if(index>=0){p->human.x=points[index][0];p->human.y=points[index][1]+1.62f;p->human.z=points[index][2];p->human.yaw=points[index][3];}
    p->human.pitch=0;p->human.health=20;p->human.ammo=p->weapon==1?30:32;++p->human.epoch;
    p->human.acknowledged_shots=p->shots;p->wants_reload=0;
    p->reload_end=-1;p->hurt_until=now_seconds()+2.0;p->last_move=now_seconds();p->died_at=0;
    p->killer_id=-1;p->killer_bot=0;p->killer_name[0]=0;
    for(int id=1;id<6;++id)if(p==&g_lan_peers[id]){
        lan_team_spawn(g_lan_teams[id],&p->human.x,&p->human.y,&p->human.z,&p->human.yaw);break;
    }
}
static void lan_team_spawn(int team,float *x,float *y,float *z,float *yaw) {
    float points[16][4];int n=breach_map_spawn_points(g_loaded_world_dir,points);
    float best=-1;int pick=-1;
    for(int i=0;i<n;++i){
        if((points[i][0]<0?0:1)!=team||flat_player_aabb_collides(points[i][0],points[i][1]+1.62f,points[i][2]))continue;
        float spacing=1e12f;
        for(int k=0;k<MAX_PASSIVE_MOBS;++k){PassiveMob *m=&g_passive_mobs[k];if(!m->active||m->death_time)continue;
            float dx=m->x-points[i][0],dz=m->z-points[i][2];spacing=fminf(spacing,dx*dx+dz*dz);}
        if(spacing>best){pick=i;best=spacing;}
    }
    if(pick>=0){*x=points[pick][0];*y=points[pick][1]+1.62f;*z=points[pick][2];*yaw=points[pick][3];}
}
static void pex_lan_start_match(void) {
    if(!pex_lan_host()||g_lan_phase!=0)return;
    for(int i=1;i<6;++i)if(g_lan_peers[i].joined&&!g_lan_peers[i].ready){hud_add_chat("Wait for all players to finish loading.");return;}
    g_lan_phase=1;g_lan_countdown=5;g_lan_start_at=now_seconds()+5.0;
    memset(g_breach_gun_drops,0,sizeof(g_breach_gun_drops));
    for(int i=0;i<MAX_PASSIVE_MOBS;++i){PassiveMob *m=&g_passive_mobs[i];
        if(m->active&&!m->breach_remote_player){passive_path_release_storage(m);memset(m,0,sizeof(*m));}}
    for(int i=1;i<6;++i)if(g_lan_peers[i].joined){LanPeer *p=&g_lan_peers[i];lan_spawn_human(p);
        lan_team_spawn(g_lan_teams[i],&p->human.x,&p->human.y,&p->human.z,&p->human.yaw);lan_proxy(i,&p->human);}
    lan_team_spawn(g_lan_teams[0],&g_player_x,&g_player_y,&g_player_z,&g_player_yaw);
    g_player_prev_x=g_player_x;g_player_prev_y=g_player_y;g_player_prev_z=g_player_z;g_player_prev_yaw=g_player_yaw;
    g_player_health=20;g_player_dead=0;g_breach_ammo=32;g_player_motion_x=g_player_motion_y=g_player_motion_z=0;
    breach_weapon_on_spawn();
    reset_player_damage_visuals();rebuild_screen();
}
static int pex_lan_enemy_target(PassiveMob *m,float *tx,float *ty,float *tz) {
    if(!pex_lan_host())return pex_lan_bot_target(m->x,m->y+1.5f,m->z,tx,ty,tz);
    if(pex_lan_waiting())return -1;
    float best=1e12f;int target=-1;
    for(int id=0;id<6;++id){
        if(g_lan_teams[id]==m->breach_team)continue;
        if(id==0&&g_player_dead)continue;
        if(id>0&&(!g_lan_peers[id].ready||g_lan_peers[id].human.health<=0))continue;
        float x=id?g_lan_peers[id].human.x:g_player_x,y=id?g_lan_peers[id].human.y:g_player_y,z=id?g_lan_peers[id].human.z:g_player_z;
        float dx=x-m->x,dz=z-m->z,score=dx*dx+dz*dz;
        if(!passive_ray_clear_blocks_125(m->x,m->y+1.5f,m->z,x,y-.35f,z))score+=100000;
        if(score<best){best=score;target=id;*tx=x;*ty=y;*tz=z;}
    }
    for(int i=0;i<MAX_PASSIVE_MOBS;++i){PassiveMob *other=&g_passive_mobs[i];
        if(!other->active||!other->breach_bot||other->breach_remote_player||other->death_time||other->breach_team==m->breach_team)continue;
        float dx=other->x-m->x,dz=other->z-m->z,score=dx*dx+dz*dz;
        if(!passive_ray_clear_blocks_125(m->x,m->y+1.5f,m->z,other->x,other->y+1.27f,other->z))score+=100000;
        score*=0.65f; /* Encourage bots to engage the opposing bot line. */
        if(score<best){best=score;target=6+i;*tx=other->x;*ty=other->y+1.62f;*tz=other->z;}
    }
    return target;
}
static void lan_human_fire(LanPeer *p,int id,float aim) {
    double now=now_seconds();
    double interval=p->weapon==1?1.0/7.0:1.0/11.0;int damage=p->weapon==BREACH_WEAPON_ATOMIC?8:p->weapon==1?7:4;
    if(pex_lan_waiting()||!p->ready||p->human.health<=0||p->human.ammo<=0||p->reload_end>=0||now-p->last_shot<interval)return;
    --p->human.ammo;p->last_shot=now;p->human.flash=2;
    float yaw=p->human.yaw*(float)M_PI/180,pitch=p->human.pitch*(float)M_PI/180;
    float dx=-sinf(yaw)*cosf(pitch),dy=-sinf(pitch),dz=cosf(yaw)*cosf(pitch);
    float spread=p->weapon==1?0.040f*(1-aim)+0.002f*aim:0.065f*(1-aim)+0.0065f*aim;
    dx+=(pex_rand_float01()-pex_rand_float01())*spread;dy+=(pex_rand_float01()-pex_rand_float01())*spread;
    dz+=(pex_rand_float01()-pex_rand_float01())*spread;float len=sqrtf(dx*dx+dy*dy+dz*dz);dx/=len;dy/=len;dz/=len;
    breach_spawn_bullet_visual(p->human.x,p->human.y-.08f,p->human.z,dx,dy,dz,48);
    float best=48;PassiveMob *target=NULL;int human=-1;
    FlatAABB host={g_player_x-.3f,g_player_y-1.62f,g_player_z-.3f,g_player_x+.3f,g_player_y+.18f,g_player_z+.3f};
    float t=0;
    if(!g_player_dead&&g_lan_teams[id]!=g_lan_teams[0]&&passive_ray_intersect_aabb(&host,p->human.x,p->human.y,p->human.z,dx,dy,dz,best,&t)){
        best=t;human=0;
    }
    for(int i=0;i<MAX_PASSIVE_MOBS;++i){
        PassiveMob *m=&g_passive_mobs[i];if(!m->active||m->death_time>0||m->breach_remote_player==id+1)continue;
        if(m->breach_bot&&(m->breach_remote_player?g_lan_teams[m->breach_remote_player-1]:m->breach_team)==g_lan_teams[id])continue;
        FlatAABB box;passive_mob_aabb(m,&box);
        if(passive_ray_intersect_aabb(&box,p->human.x,p->human.y,p->human.z,dx,dy,dz,best,&t)&&t<best){
            best=t;target=m;human=m->breach_remote_player?m->breach_remote_player-1:-1;
        }
    }
    float pickup_t=best;int pickup=breach_gun_drop_hit(p->human.x,p->human.y,p->human.z,dx,dy,dz,best,&pickup_t);
    if(pickup>=0){
        BreachGunDrop *drop=&g_breach_gun_drops[pickup];p->weapon=drop->weapon;drop->active=0;p->pickup_locked=1;
        p->human.ammo=breach_weapon_magazine_size(p->weapon);p->reload_end=-1;p->wants_reload=0;return;
    }
    if(!passive_ray_clear_blocks_125(p->human.x,p->human.y,p->human.z,
            p->human.x+dx*best,p->human.y+dy*best,p->human.z+dz*best))return;
    PexDamageSource s=pex_damage_source_simple(PEX_DAMAGE_ARROW);s.damage_type="bullet";
    s.breach_atomic=p->weapon==BREACH_WEAPON_ATOMIC;
    s.true_kind=PEX_DAMAGE_ENTITY_PLAYER;s.true_x=p->human.x;s.true_y=p->human.y;s.true_z=p->human.z;
    if(human>=0){g_lan_last_human_attacker=id;pex_lan_hit_human(human,damage,&s);g_lan_last_human_attacker=-1;}
    else if(target)pex_mob_attack_entity_from(target,s,damage);
}
static int lan_input(LanPeer *p,int id,const LanInput *in) {
    if(!isfinite(in->x)||!isfinite(in->y)||!isfinite(in->z)||!isfinite(in->yaw)||!isfinite(in->pitch)||!isfinite(in->aim)||in->weapon>=BREACH_WEAPON_COUNT||in->selected_weapon>1)return 0;
    double now=now_seconds();
    float dx=in->x-p->human.x,dy=in->y-p->human.y,dz=in->z-p->human.z;
    float allowance=(float)fmin(0.25,fmax(0.02,now-p->last_move))*14.0f+0.6f;
    if(!pex_lan_waiting()&&p->human.health>0&&fabsf(in->x)<512&&fabsf(in->z)<512&&in->y>0&&in->y<260&&
       dx*dx+dy*dy+dz*dz<=allowance*allowance){
        p->human.x=in->x;p->human.y=in->y;p->human.z=in->z;
    }
    p->last_move=now;p->human.yaw=pex_wrap_degrees(in->yaw);p->human.pitch=pex_clamp_float(in->pitch,-89.9f,89.9f);
    p->aim=pex_clamp_float(in->aim,0,1);
    p->selected_weapon=(int)in->selected_weapon;
    /* Atomic Star is granted only by the host's pickup ray, never by input. */
    if(!p->pickup_locked&&in->weapon<BREACH_WEAPON_ATOMIC)p->weapon=(int)in->weapon;
    if(!p->ready)p->catchup=0;
    p->ready=1;
    if(in->respawns!=p->respawns){p->respawns=in->respawns;
        if(p->human.health<=0&&(p->skip_allowed||p->died_at<=0||now-p->died_at>=5.0))lan_spawn_human(p);
    }
    if(p->reload_end>=0&&now>=p->reload_end){p->human.ammo=p->weapon==1?30:32;p->reload_end=-1;}
    if(in->reloads!=p->reloads){p->reloads=in->reloads;p->wants_reload=1;}
    if(in->shots!=p->shots){
        uint32_t delta=in->shots-p->shots;if(delta>8)return 0;
        p->shots=in->shots;
        /* Packet arrivals can bunch together. Tick the accepted shot queue at
           the host's weapon rate instead of dropping legitimate held shots. */
    }
    return 1;
}
static int lan_begin_map(LanPeer *p,int id,const void *data,size_t bytes) {
    if(bytes!=32||p->joined)return 0;
    memcpy(p->human.name,data,31);p->human.name[31]=0;
    for(int i=0;p->human.name[i];++i)if((unsigned char)p->human.name[i]<32||p->human.name[i]=='%')p->human.name[i]='_';
    p->joined=1;p->human.active=1;
    lan_lobby_names();g_lan_present[id]=0;
    g_lan_teams[id]=pex_lan_team_count(0)<=pex_lan_team_count(1)?0:1;
    g_lan_present[id]=1;
    if(pex_lan_waiting())rebuild_screen();
    breach_bots_update();lan_spawn_human(p);lan_proxy(id,&p->human);
    LanWelcome w={0};w.id=(uint32_t)id;w.seed=g_world_seed;w.self=p->human;
    breach_map_read_spawn(g_loaded_world_dir,&w.marker[0],&w.marker[1],&w.marker[2],&w.marker[3]);
    w.spawn_count=(uint32_t)breach_map_spawn_points(g_loaded_world_dir,w.spawns);
    const char *roots[]={"src/assets/maps/nuketown","assets/maps/nuketown","maps/nuketown"};char path[MAX_PATHBUF];
    for(int i=0;i<3&&!p->manifest;++i){snprintf(path,sizeof(path),"%s/chunks.txt",roots[i]);p->manifest=fopen(path,"rb");}
    if(!p->manifest)return 0;
    char name[96];while(fgets(name,sizeof(name),p->manifest))if(name[0]=='c')++w.chunks;
    rewind(p->manifest);if(!w.chunks||w.chunks>2048)return 0;
    if(!lan_queue(p,LAN_WELCOME,&w,sizeof(w)))return 0;
    for(int i=0;i<6;++i)if(g_lan_skins[i].bytes)
        if(!lan_queue(p,LAN_SKIN,&g_lan_skins[i],16+g_lan_skins[i].bytes))return 0;
    return 1;
}
static int lan_stream_map(LanPeer *p) {
    if(!p->manifest||p->tx_len>262144)return 1;
    for(int i=0;i<8;++i){
        char name[96],path[MAX_PATHBUF];
        if(!fgets(name,sizeof(name),p->manifest)){
            if(ferror(p->manifest))return 0;
            fclose(p->manifest);p->manifest=NULL;
            return lan_queue(p,LAN_READY,NULL,0);
        }
        name[strcspn(name,"\r\n")]=0;
        if(strncmp(name,"c.",2)||strstr(name,"..")||strchr(name,'/')||strchr(name,'\\'))return 0;
        snprintf(path,sizeof(path),"%s/chunks/%s",g_loaded_world_dir,name);
        FILE *f=fopen(path,"rb");if(!f)return 0;
        fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
        if(size<20||size>LAN_FRAME_MAX){fclose(f);return 0;}
        unsigned char *buf=(unsigned char*)malloc((size_t)size);
        if(!buf){fclose(f);return 0;}
        int ok=fread(buf,1,(size_t)size,f)==(size_t)size;fclose(f);
        if(ok){
            int cx,cz;memcpy(&cx,buf+8,4);memcpy(&cz,buf+12,4);size_t live_size=0;
            unsigned char *live=lan_capture_chunk(cx,cz,&live_size);
            ok=lan_queue(p,LAN_CHUNK,live?live:buf,live?live_size:(size_t)size);free(live);
        }
        free(buf);if(!ok)return 0;++p->chunks_sent;
    }
    return 1;
}
static int lan_client_welcome(const LanWelcome *w) {
    if(w->id<1||w->id>=6||!w->chunks||w->chunks>2048||w->spawn_count>16||g_lan_loading)return 0;
    if(!isfinite(w->self.x)||!isfinite(w->self.y)||!isfinite(w->self.z)||!isfinite(w->self.yaw))return 0;
    if(fabsf(w->self.x)>512||fabsf(w->self.z)>512||w->self.y<0||w->self.y>260||w->self.health<0||w->self.health>20)return 0;
    for(int i=0;i<4;++i)if(!isfinite(w->marker[i]))return 0;
    for(uint32_t i=0;i<w->spawn_count;++i)for(int k=0;k<4;++k)if(!isfinite(w->spawns[i][k]))return 0;
    if(fabsf(w->marker[0])>512||fabsf(w->marker[2])>512||w->marker[1]<1.62f||w->marker[1]>255)return 0;
    for(uint32_t i=0;i<w->spawn_count;++i)if(fabsf(w->spawns[i][0])>512||fabsf(w->spawns[i][2])>512||w->spawns[i][1]<1||w->spawns[i][1]>253)return 0;
    g_lan_welcome=*w;g_lan_self_id=(int)w->id;g_lan_loading=1;
    snprintf(g_lan_cache,sizeof(g_lan_cache),"%s/lan-cache/session_%lu_%lu",g_mc_dir,
             (unsigned long)GetCurrentProcessId(),(unsigned long)GetTickCount());
    if(dir_exists(g_lan_cache))return 0;
    make_dir_recursive(g_lan_cache);char path[MAX_PATHBUF];
    snprintf(path,sizeof(path),"%s/chunks",g_lan_cache);make_dir_recursive(path);
    snprintf(path,sizeof(path),"%s/breach_map.txt",g_lan_cache);
    FILE *f=fopen(path,"wb");if(!f)return 0;
    fprintf(f,"%.6f %.6f %.6f %.6f\n",w->marker[0],w->marker[1],w->marker[2],w->marker[3]);fclose(f);
    snprintf(path,sizeof(path),"%s/breach_spawns.txt",g_lan_cache);f=fopen(path,"wb");if(!f)return 0;
    for(uint32_t i=0;i<w->spawn_count;++i)fprintf(f,"%.6f %.6f %.6f %.6f\n",w->spawns[i][0],w->spawns[i][1],w->spawns[i][2],w->spawns[i][3]);
    fclose(f);snprintf(g_lan_status,sizeof(g_lan_status),"Downloading host map...");pex_lan_send_skin();return 1;
}
static int lan_client_chunk(const unsigned char *data,size_t bytes,int update) {
    if(!g_lan_loading||bytes<20||(!update&&g_lan_chunks_received>=(int)g_lan_welcome.chunks)||(update&&!g_lan_client_entered))return 0;
    int32_t cx,cz,count;memcpy(&cx,data+8,4);memcpy(&cz,data+12,4);memcpy(&count,data+16,4);
    if(cx < -2048 || cx > 2048 || cz < -2048 || cz > 2048 || count!=FLAT_CHUNK_BLOCK_COUNT)return 0;
    if(!chunk_delta_is_raw((const char*)data)&&memcmp(data,"PXCDLTZ1",8))return 0;
    char x[32],z[32],path[MAX_PATHBUF];base36_i32(cx,x,sizeof(x));base36_i32(cz,z,sizeof(z));
    snprintf(path,sizeof(path),"%s/chunks/c.%s.%s.dat",g_lan_cache,x,z);
    if(!update&&file_exists(path))return 0;
    FILE *f=fopen(path,"w+b");if(!f)return 0;
    int ok=fwrite(data,1,bytes,f)==bytes;rewind(f);fseek(f,20,SEEK_SET);
    unsigned char *b=(unsigned char*)malloc(FLAT_CHUNK_BLOCK_COUNT),*m=(unsigned char*)malloc(FLAT_CHUNK_BLOCK_COUNT);
    if(ok&&b&&m)ok=chunk_delta_read_payload(f,(const char*)data,cx,cz,count,cx,cz,b,m);else ok=0;
    if(ok)for(int i=0;i<FLAT_CHUNK_BLOCK_COUNT;++i)if(b[i]>124||m[i]>15){ok=0;break;}
    if(ok&&update&&flat_chunk_generated_at_block(cx*16,cz*16))flat_copy_chunk_buffers(cx,cz,b,m);
    free(b);free(m);fclose(f);if(!ok)return 0;
    if(!update)++g_lan_chunks_received;return 1;
}
static int lan_snapshot_apply(const LanSnapshot *s) {
    if(s->mob_count>MAX_PASSIVE_MOBS||g_lan_self_id<1)return 0;
    if(!g_lan_client_entered)return 1;
    if(s->phase<0||s->phase>2||s->countdown<0||s->countdown>5||s->bots_enabled<0||s->bots_enabled>1)return 0;
    int old_phase=g_lan_phase;
    int changed=g_lan_phase!=s->phase||g_lan_countdown!=s->countdown;
    g_lan_phase=s->phase;g_lan_countdown=s->countdown;g_lan_bots_enabled=s->bots_enabled!=0;
    for(int i=0;i<6;++i){if(s->teams[i]<0||s->teams[i]>1)return 0;
        if(s->weapons[i]<0||s->weapons[i]>=BREACH_WEAPON_COUNT||!isfinite(s->aims[i])||s->aims[i]<0||s->aims[i]>1)return 0;
        g_lan_replay_aims[i]=s->aims[i];g_lan_replay_weapons[i]=s->weapons[i];
        if(g_lan_teams[i]!=s->teams[i]||g_lan_present[i]!=(int)s->humans[i].active)changed=1;
        g_lan_teams[i]=s->teams[i];g_lan_present[i]=s->humans[i].active!=0;
        memcpy(g_lan_names[i],s->humans[i].name,31);g_lan_names[i][31]=0;}
    if(pex_lan_waiting()&&g_screen!=SCREEN_PAUSE)set_screen(SCREEN_PAUSE);
    else if(g_lan_phase==2&&old_phase!=2)set_screen(SCREEN_INGAME);
    else if(changed&&g_screen==SCREEN_PAUSE)rebuild_screen();
    unsigned char present[MAX_PASSIVE_MOBS]={0};
    for(uint32_t i=0;i<s->mob_count;++i){
        const LanMob *w=&s->mobs[i];
        if(w->slot>=MAX_PASSIVE_MOBS-6||!passive_mob_type_valid(w->type)||!isfinite(w->x)||!isfinite(w->y)||!isfinite(w->z)||
           !isfinite(w->yaw)||!isfinite(w->head)||!isfinite(w->pitch)||!isfinite(w->limb)||!isfinite(w->move))return 0;
        if(fabsf(w->x)>4096||fabsf(w->z)>4096||w->y < -128||w->y>512)return 0;
        present[w->slot]=1;PassiveMob *m=&g_passive_mobs[w->slot];
        if(!m->active||m->entity_id!=w->entity){passive_path_release_storage(m);passive_mob_init(m,w->type,w->x,w->y,w->z);}
        if(w->flash>0&&m->breach_bot_flash==0){
            lan_shot_sound(w->x,w->y,w->z);
            float yaw=w->head*(float)M_PI/180,pitch=w->pitch*(float)M_PI/180;
            breach_spawn_bullet_visual(w->x,w->y+1.5f,w->z,-sinf(yaw)*cosf(pitch),-sinf(pitch),cosf(yaw)*cosf(pitch),48);
        }
        if(w->health<m->health)m->hurt_time=10;else if(m->hurt_time>0)--m->hurt_time;
        m->prev_x=m->x;m->prev_y=m->y;m->prev_z=m->z;m->prev_render_yaw=m->render_yaw;m->prev_head_yaw=m->head_yaw;m->prev_pitch=m->pitch;
        m->prev_limb_amount=m->limb_amount;m->prev_limb_swing=m->limb_swing;
        m->x=w->x;m->y=w->y;m->z=w->z;m->yaw=m->render_yaw=w->yaw;m->head_yaw=w->head;m->pitch=w->pitch;
        m->health=w->health;m->death_time=w->death;m->breach_bot=w->bot;m->breach_bot_flash=w->flash;
        m->breach_atomic_hurt=s->mob_atomic_hurt[w->slot]!=0;
        m->breach_team=w->team;
        m->entity_id=w->entity;m->limb_swing=w->limb;m->limb_amount=w->move;
    }
    for(int i=0;i<MAX_PASSIVE_MOBS-6;++i)if(!present[i])g_passive_mobs[i].active=0;
    for(int i=0;i<6;++i){
        const LanHuman *h=&s->humans[i];if(!isfinite(h->x)||!isfinite(h->y)||!isfinite(h->z)||!isfinite(h->yaw)||!isfinite(h->pitch)||h->health<0||h->health>20||h->ammo<0||h->ammo>32)return 0;
        if(h->active&&(fabsf(h->x)>512||fabsf(h->z)>512||h->y<0||h->y>260))return 0;
        lan_proxy(i,h);
        if(i!=g_lan_self_id)g_passive_mobs[MAX_PASSIVE_MOBS-6+i].breach_atomic_hurt=s->atomic_hurt[i]!=0;
    }
    for(int i=0;i<BREACH_MAX_GUN_DROPS;++i){const BreachGunDrop *d=&s->drops[i];
        if(d->active&&(d->weapon<0||d->weapon>=BREACH_WEAPON_COUNT||!isfinite(d->x)||!isfinite(d->y)||!isfinite(d->z)||fabsf(d->x)>512||fabsf(d->z)>512||d->y < -128||d->y>512))return 0;
    }
    memcpy(g_breach_gun_drops,s->drops,sizeof(g_breach_gun_drops));
    const LanHuman *self=&s->humans[g_lan_self_id];
    if(!self->active)return 0;
    if(self->health>0&&breach_weapon_current()!=s->weapons[g_lan_self_id])breach_weapon_equip(s->weapons[g_lan_self_id]);
    if(self->epoch!=g_lan_epoch){
        g_lan_epoch=self->epoch;
        g_lan_killcam_active=0;g_lan_killcam_ticks=0;g_lan_killcam_respawn_sent=0;g_lan_replay_length=0;
        breach_weapon_on_spawn();
        breach_weapon_equip(s->weapons[g_lan_self_id]);
        g_player_x=g_player_prev_x=self->x;g_player_y=g_player_prev_y=self->y;g_player_z=g_player_prev_z=self->z;
        g_player_yaw=g_player_prev_yaw=self->yaw;g_player_pitch=g_player_prev_pitch=self->pitch;
        g_player_motion_x=g_player_motion_y=g_player_motion_z=0;g_player_dead=0;g_player_death_time=0;
        g_breach_ammo=self->ammo;g_breach_reload_start=g_breach_reload_end=-1;g_breach_inspecting=0;
        g_player_fall_distance=0;reset_player_damage_visuals();
        if(g_screen==SCREEN_DEATH)set_screen(pex_lan_waiting()?SCREEN_PAUSE:SCREEN_INGAME);
    }else{
        float dx=self->x-g_player_x,dy=self->y-g_player_y,dz=self->z-g_player_z;
        if(dx*dx+dy*dy+dz*dz>9){g_player_x=g_player_prev_x=self->x;g_player_y=g_player_prev_y=self->y;g_player_z=g_player_prev_z=self->z;}
    }
    if(self->health<g_player_health)g_player_hurt_time=10;
    g_player_health=self->health;
    g_player_atomic_hurt=s->atomic_hurt[g_lan_self_id]!=0;
    if(self->health>0){g_lan_killcam_active=0;g_lan_killcam_ticks=0;g_lan_replay_length=0;}
    if(self->health<=0){
        int id=g_lan_self_id;
        if(!g_lan_killcam_active){
            g_lan_killcam_active=1;g_lan_killcam_ticks=0;g_lan_killcam_respawn_sent=0;
            lan_replay_record();
            lan_replay_capture(s->killer_bot[id],s->killer_id[id]);
        }
        g_lan_killcam_killer_id=s->killer_id[id];g_lan_killcam_killer_bot=s->killer_bot[id]!=0;
        g_lan_killcam_skip_allowed=!g_lan_killcam_killer_bot;
        snprintf(g_lan_killcam_killer_name,sizeof(g_lan_killcam_killer_name),"%.47s",s->killer_name[id][0]?s->killer_name[id]:"Unknown");
    }
    uint32_t outstanding=g_lan_shots-self->acknowledged_shots;
    if(outstanding<=8&&g_breach_reload_end<0)g_breach_ammo=(int)fmaxf(0,(float)self->ammo-(float)outstanding);
    if(self->health<=0&&!g_player_dead)player_die("was shot in LAN");
    return 1;
}
static int lan_message(LanPeer *p,int id,int type,const unsigned char *data,size_t bytes) {
    if(type==LAN_SKIN)return lan_receive_skin(p,id,data,bytes);
    if(pex_lan_host()){
        if(type==LAN_TEAM&&bytes==4&&p->joined){int32_t team;memcpy(&team,data,4);(void)lan_set_team(id,team);rebuild_screen();return 1;}
        if(type==LAN_HELLO)return lan_begin_map(p,id,data,bytes);
        if(type==LAN_INPUT&&bytes==sizeof(LanInput)&&p->joined&&!p->manifest){LanInput in;memcpy(&in,data,sizeof(in));return lan_input(p,id,&in);}
        return 0;
    }
    if(type==LAN_WELCOME&&bytes==sizeof(LanWelcome)){LanWelcome w;memcpy(&w,data,sizeof(w));return lan_client_welcome(&w);}
    if(type==LAN_CHUNK)return lan_client_chunk(data,bytes,0);
    if(type==LAN_CHUNK_UPDATE)return lan_client_chunk(data,bytes,1);
    if(type==LAN_READY&&!bytes&&g_lan_loading==1&&g_lan_chunks_received==(int)g_lan_welcome.chunks){
        g_lan_loading=2;
        g_world_type=0;g_game_mode=0;g_pending_game_mode=0;
        if(!write_level_dat(g_lan_cache,"LAN Nuketown",g_lan_welcome.seed,0,65,0,0))return 0;
        start_world_generation_in_dir(g_lan_cache,"LAN Nuketown",0);return 1;
    }
    if(type==LAN_SNAPSHOT&&bytes==sizeof(LanSnapshot)){LanSnapshot s;memcpy(&s,data,sizeof(s));return lan_snapshot_apply(&s);}
    if(type==LAN_ERROR){snprintf(g_lan_failure,sizeof(g_lan_failure),"%.*s",(int)fmin(bytes,150),(const char*)data);return 0;}
    return 0;
}
static int lan_poll_peer(LanPeer *p,int id) {
    if(!lan_flush(p))return 0;
    for(int iteration=0;iteration<8;++iteration){
        if(p->rx_len>=LAN_IO_CAP)return 0;
        int n=recv(p->socket,(char*)p->rx+p->rx_len,(int)(LAN_IO_CAP-p->rx_len),0);
        if(n==0)return 0;
        if(n<0){if(lan_would_block())break;return 0;}
        p->seen=now_seconds();p->rx_len+=(size_t)n;
        size_t consumed=0;
        while(p->rx_len-consumed>=sizeof(LanHeader)){
            LanHeader h;memcpy(&h,p->rx+consumed,sizeof(h));
            if(h.magic!=LAN_MAGIC||h.version!=PEX_LAN_PROTOCOL_VERSION||h.bytes>LAN_FRAME_MAX)return 0;
            if(p->rx_len-consumed<sizeof(h)+h.bytes)break;
            if(!lan_message(p,id,h.type,p->rx+consumed+sizeof(h),h.bytes))return 0;
            consumed+=sizeof(h)+h.bytes;
        }
        if(consumed){memmove(p->rx,p->rx+consumed,p->rx_len-consumed);p->rx_len-=consumed;}
    }
    return now_seconds()-p->seen<30.0;
}
static void lan_discovery_tick(void) {
    if(g_lan_advertiser!=INVALID_SOCKET){
        for(int i=0;i<16;++i){char request[64];struct sockaddr_in from;int length=sizeof(from);
            int n=recvfrom(g_lan_advertiser,request,sizeof(request),0,(struct sockaddr*)&from,&length);
            if(n<0)break;
            if(n==12&&!memcmp(request,"POD_DISCOVER",12)){
                char response[160];int bytes=snprintf(response,sizeof(response),"POD_GAME %d %d %d %.63s",PEX_LAN_PROTOCOL_VERSION,g_lan_port,pex_lan_humans(),g_loaded_world_name);
                sendto(g_lan_advertiser,response,bytes,0,(struct sockaddr*)&from,length);
            }
        }
    }
    if(g_screen!=SCREEN_MULTIPLAYER)return;
    if(g_lan_discovery==INVALID_SOCKET){
        g_lan_discovery=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(g_lan_discovery==INVALID_SOCKET)return;
        int one=1;setsockopt(g_lan_discovery,SOL_SOCKET,SO_BROADCAST,(const char*)&one,sizeof(one));lan_nonblocking(g_lan_discovery);
        struct sockaddr_in local={0};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_ANY);
        if(bind(g_lan_discovery,(struct sockaddr*)&local,sizeof(local))){closesocket(g_lan_discovery);g_lan_discovery=INVALID_SOCKET;return;}
    }
    double now=now_seconds();
    if(now-g_lan_query_time>1.0){
        g_lan_query_time=now;struct sockaddr_in to={0};to.sin_family=AF_INET;to.sin_port=htons(PEX_LAN_DISCOVERY_PORT);
        to.sin_addr.s_addr=htonl(INADDR_BROADCAST);sendto(g_lan_discovery,"POD_DISCOVER",12,0,(struct sockaddr*)&to,sizeof(to));
        to.sin_addr.s_addr=htonl(INADDR_LOOPBACK);sendto(g_lan_discovery,"POD_DISCOVER",12,0,(struct sockaddr*)&to,sizeof(to));
    }
    int changed=0;
    for(int i=0;i<16;++i){
        char data[192];struct sockaddr_in from;int length=sizeof(from);
        int n=recvfrom(g_lan_discovery,data,sizeof(data)-1,0,(struct sockaddr*)&from,&length);if(n<0)break;data[n]=0;
        int version,port,humans;char name[64]={0};
        if(sscanf(data,"POD_GAME %d %d %d %63[^\r\n]",&version,&port,&humans,name)!=4||version!=PEX_LAN_PROTOCOL_VERSION||port<1||port>65535||humans<1||humans>6)continue;
        const char *ip=inet_ntoa(from.sin_addr);int index=-1;
        for(int j=0;j<g_lan_game_count;++j)if(g_lan_games[j].port==port&&!strcmp(g_lan_games[j].address,ip)){index=j;break;}
        if(index<0){if(g_lan_game_count>=8)continue;index=g_lan_game_count++;changed=1;}
        PexLanGame *game=&g_lan_games[index];snprintf(game->address,sizeof(game->address),"%s",ip);snprintf(game->name,sizeof(game->name),"%s",name);
        if(game->humans!=humans)changed=1;game->port=port;game->humans=humans;game->seen=now;
    }
    for(int i=g_lan_game_count-1;i>=0;--i)if(now-g_lan_games[i].seen>4){
        memmove(&g_lan_games[i],&g_lan_games[i+1],(size_t)(g_lan_game_count-i-1)*sizeof(PexLanGame));--g_lan_game_count;changed=1;
    }
    if(changed)rebuild_screen();
}
static int pex_lan_open(void) {
    if(pex_lan_active()||g_mp_connected)return 0;
    if(g_current_dimension!=PEX_DIM_OVERWORLD||fabsf(g_player_x)>512||fabsf(g_player_z)>512||
       !breach_map_read_spawn(g_loaded_world_dir,NULL,NULL,NULL,NULL)){
        snprintf(g_lan_status,sizeof(g_lan_status),"Open a local Nuketown world and stay within the map to host LAN.");return 0;
    }
    if(!lan_init()){snprintf(g_lan_status,sizeof(g_lan_status),"Windows network initialization failed.");return 0;}
    ingame_tick_async_shutdown();
    SOCKET tcp=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP),udp=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(tcp==INVALID_SOCKET||udp==INVALID_SOCKET){if(tcp!=INVALID_SOCKET)closesocket(tcp);if(udp!=INVALID_SOCKET)closesocket(udp);return 0;}
    struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_ANY);
    int one=1;setsockopt(udp,SOL_SOCKET,SO_REUSEADDR,(const char*)&one,sizeof(one));
    if(bind(tcp,(struct sockaddr*)&addr,sizeof(addr))||listen(tcp,5)||!lan_nonblocking(tcp))goto failed;
    int size=sizeof(addr);getsockname(tcp,(struct sockaddr*)&addr,&size);g_lan_port=ntohs(addr.sin_port);
    addr.sin_port=htons(PEX_LAN_DISCOVERY_PORT);
    if(bind(udp,(struct sockaddr*)&addr,sizeof(addr))||!lan_nonblocking(udp))goto failed;
    g_lan_listener=tcp;g_lan_advertiser=udp;g_lan_mode=1;g_lan_self_id=0;
    g_lan_bots_enabled=1;g_lan_killcam_active=0;g_lan_killcam_ticks=0;
    g_lan_phase=0;g_lan_countdown=5;g_lan_teams[0]=0;lan_lobby_names();
    set_screen(SCREEN_PAUSE);
    g_lan_dirty_count=0;
    pex_lan_send_skin();
    snprintf(g_lan_status,sizeof(g_lan_status),"LAN open: port %d, up to 6 humans",g_lan_port);
    hud_add_chat(g_lan_status);return 1;
failed:
    closesocket(tcp);closesocket(udp);snprintf(g_lan_status,sizeof(g_lan_status),"Could not open LAN sockets (error %d)",WSAGetLastError());return 0;
}
static int pex_lan_join(int index) {
    if(index<0||index>=g_lan_game_count||pex_lan_active()||!lan_init())return 0;
    SOCKET socket=WSASocketA(AF_INET,SOCK_STREAM,IPPROTO_TCP,NULL,0,0);
    if(socket==INVALID_SOCKET||!lan_attach(&g_lan_connection,socket))return 0;
    struct sockaddr_in to={0};to.sin_family=AF_INET;to.sin_port=htons((u_short)g_lan_games[index].port);to.sin_addr.s_addr=inet_addr(g_lan_games[index].address);
    if(connect(socket,(struct sockaddr*)&to,sizeof(to))&&WSAGetLastError()!=WSAEWOULDBLOCK){lan_close_peer(&g_lan_connection);return 0;}
    g_lan_mode=2;g_lan_self_id=-1;g_lan_chunks_received=0;g_lan_loading=0;g_lan_client_entered=0;
    g_lan_phase=0;g_lan_countdown=5;memset(g_lan_present,0,sizeof(g_lan_present));
    g_lan_shots=g_lan_reloads=g_lan_respawns=g_lan_epoch=0;g_lan_failure[0]=0;
    char name[32]={0};snprintf(name,sizeof(name),"%.31s",g_multiplayer_username);
    lan_queue(&g_lan_connection,LAN_HELLO,name,sizeof(name));
    snprintf(g_lan_status,sizeof(g_lan_status),"Connecting to %.48s...",g_lan_games[index].name);set_screen(SCREEN_CONNECTING);return 1;
}
static int pex_lan_join_address(const char *address) {
    char ip[48];int port;
    if(sscanf(address,"%47[^:]:%d",ip,&port)!=2||port<1||port>65535||inet_addr(ip)==INADDR_NONE)return 0;
    PexLanGame old=g_lan_games[0];int count=g_lan_game_count;
    snprintf(g_lan_games[0].address,48,"%s",ip);snprintf(g_lan_games[0].name,64,"Direct LAN game");
    g_lan_games[0].port=port;g_lan_game_count=count?count:1;
    int ok=pex_lan_join(0);g_lan_games[0]=old;g_lan_game_count=count;return ok;
}
static void pex_lan_stop(void) {
    memset(g_breach_gun_drops,0,sizeof(g_breach_gun_drops));
    for(int i=0;i<6;++i){free_texture(&g_lan_skin_textures[i]);memset(&g_lan_skins[i],0,sizeof(g_lan_skins[i]));}
    g_lan_local_skin_hash=0;
    if(g_lan_socket_initialized){for(int i=1;i<6;++i)lan_close_peer(&g_lan_peers[i]);lan_close_peer(&g_lan_connection);}
    if(g_lan_listener!=INVALID_SOCKET)closesocket(g_lan_listener);
    if(g_lan_advertiser!=INVALID_SOCKET)closesocket(g_lan_advertiser);
    g_lan_listener=g_lan_advertiser=INVALID_SOCKET;
    for(int i=0;i<MAX_PASSIVE_MOBS;++i)if(g_passive_mobs[i].breach_remote_player)g_passive_mobs[i].active=0;
    g_lan_mode=0;g_lan_self_id=-1;g_lan_client_entered=0;g_lan_loading=0;
    g_lan_killcam_active=0;g_lan_killcam_ticks=0;g_lan_bots_enabled=1;
    g_lan_replay_head=g_lan_replay_count=g_lan_replay_length=0;
}
static void pex_lan_shot(void) { if(pex_lan_client())++g_lan_shots; }
static void pex_lan_reload(void) { if(pex_lan_client())++g_lan_reloads; }
static void pex_lan_respawn(void) { if(pex_lan_client())++g_lan_respawns; }
static void pex_lan_frame(void) {
    if((g_screen!=SCREEN_MULTIPLAYER&&!pex_lan_active())||!lan_init())return;
    lan_discovery_tick();
    /* Also detect skin reset/texture-pack reloads that do not use the importer. */
    if(pex_lan_active()&&g_lan_self_id>=0&&tex_steve.rgba&&tex_steve.w==64&&(tex_steve.h==32||tex_steve.h==64)){
        uint32_t hash=2166136261u;int bytes=tex_steve.w*tex_steve.h*4;
        for(int i=0;i<bytes;++i)hash=(hash^tex_steve.rgba[i])*16777619u;
        hash^=(uint32_t)tex_steve.h;if(hash!=g_lan_local_skin_hash)pex_lan_send_skin();
    }
    if(pex_lan_host()){
        for(int iteration=0;iteration<8;++iteration){
            SOCKET s=accept(g_lan_listener,NULL,NULL);if(s==INVALID_SOCKET)break;
            int id=1;while(id<6&&g_lan_peers[id].socket!=INVALID_SOCKET)++id;
            if(id==6||g_lan_phase!=0){
                const char *message=g_lan_phase?"Match already starting or running":"LAN game is full (6 real players)";LanHeader h={LAN_MAGIC,PEX_LAN_PROTOCOL_VERSION,LAN_ERROR,(uint32_t)strlen(message)};
                send(s,(const char*)&h,sizeof(h),0);send(s,message,(int)strlen(message),0);closesocket(s);continue;
            }
            (void)lan_attach(&g_lan_peers[id],s);
        }
        for(int i=1;i<6;++i)if(g_lan_peers[i].socket!=INVALID_SOCKET){
            if(!lan_poll_peer(&g_lan_peers[i],i)||!lan_stream_map(&g_lan_peers[i]))lan_remove_peer(i);
        }
    }else if(pex_lan_client()){
        /* Wait for a nonblocking connect to finish before attempting a send. */
        fd_set write,error;FD_ZERO(&write);FD_ZERO(&error);FD_SET(g_lan_connection.socket,&write);FD_SET(g_lan_connection.socket,&error);
        struct timeval timeout={0,0};select(0,NULL,&write,&error,&timeout);
        int fail=FD_ISSET(g_lan_connection.socket,&error);
        if(!fail&&!g_lan_connection.joined&&FD_ISSET(g_lan_connection.socket,&write)){
            int result=0,length=sizeof(result);
            if(getsockopt(g_lan_connection.socket,SOL_SOCKET,SO_ERROR,(char*)&result,&length)||result)fail=1;
            else g_lan_connection.joined=1;
        }
        if(!fail&&g_lan_connection.joined)fail=!lan_poll_peer(&g_lan_connection,0);
        if(now_seconds()-g_lan_connection.seen>30)fail=1;
        if(fail){
            snprintf(g_lan_status,sizeof(g_lan_status),"%s",g_lan_failure[0]?g_lan_failure:"Host disconnected or LAN connection failed.");
            if(g_lan_client_entered||g_screen==SCREEN_GENERATING)leave_world_to_title();
            else {pex_lan_stop();set_screen(SCREEN_MULTIPLAYER);}return;
        }
        if(!g_lan_client_entered&&g_lan_loading&&g_screen==SCREEN_INGAME){
            ingame_tick_async_shutdown();g_lan_client_entered=1;
            const LanHuman *h=&g_lan_welcome.self;g_lan_epoch=h->epoch;
            g_player_x=g_player_prev_x=h->x;g_player_y=g_player_prev_y=h->y;g_player_z=g_player_prev_z=h->z;
            g_player_yaw=g_player_prev_yaw=h->yaw;g_player_health=20;g_breach_ammo=32;
            g_player_motion_x=g_player_motion_y=g_player_motion_z=0;
            set_screen(SCREEN_PAUSE);
        }
    }
}
static void pex_lan_tick_end(void) {
    if(pex_lan_active())lan_replay_record();
    if(pex_lan_client()){
        if(!g_lan_client_entered)return;
        LanInput in={g_player_x,g_player_y,g_player_z,g_player_yaw,g_player_pitch,g_breach_aim,g_lan_shots,g_lan_reloads,g_lan_respawns,(uint32_t)breach_weapon_current(),(uint32_t)breach_weapon_selected()};
        if(!lan_queue(&g_lan_connection,LAN_INPUT,&in,sizeof(in)))snprintf(g_lan_failure,sizeof(g_lan_failure),"LAN send queue overflow");
        return;
    }
    if(!pex_lan_host())return;
    lan_lobby_names();
    if(g_lan_phase==1){
        g_lan_countdown=(int)ceil(fmax(0,g_lan_start_at-now_seconds()));
        if(!g_lan_countdown){g_lan_phase=2;set_screen(SCREEN_INGAME);}
    }
    breach_bots_update();
    for(int id=1;id<6;++id){
        LanPeer *p=&g_lan_peers[id];if(!p->ready)continue;
        if(p->human.health<=0&&p->died_at>0&&now_seconds()-p->died_at>=5.0){lan_spawn_human(p);}
        if(p->reload_end>=0&&now_seconds()>=p->reload_end){p->human.ammo=p->weapon==1?30:32;p->reload_end=-1;}
        if(pex_lan_waiting()){p->human.acknowledged_shots=p->shots;continue;}
        if(p->human.acknowledged_shots!=p->shots){
            if(p->human.health<=0||p->human.ammo<=0||p->reload_end>=0)p->human.acknowledged_shots=p->shots;
            else if(now_seconds()-p->last_shot>=(p->weapon==1?1.0/7.0:1.0/11.0)){
                lan_human_fire(p,id,p->aim);++p->human.acknowledged_shots;
            }
        }
        if(p->wants_reload&&p->human.acknowledged_shots==p->shots&&p->reload_end<0){
            p->wants_reload=0;int mag=p->weapon==1?30:32;
            if(p->human.ammo<mag)p->reload_end=now_seconds()+(p->weapon==1?(p->human.ammo?2.1:2.8):(p->human.ammo?1.125:1.5));
        }
        lan_proxy(id,&p->human);
    }
    /* Peers catch up changes made during their map download, then receive live
       edits coalesced by chunk. Limit work and queue size on each tick. */
    for(int id=1;id<6;++id){
        LanPeer *p=&g_lan_peers[id];if(!p->ready||p->tx_len>262144)continue;
        for(int budget=0;budget<4&&p->catchup<g_lan_dirty_count;++budget){
            LanDirtyChunk *c=&g_lan_dirty_chunks[p->catchup++];size_t size=0;
            unsigned char *wire=lan_capture_chunk(c->cx,c->cz,&size);
            if(wire){(void)lan_queue(p,LAN_CHUNK_UPDATE,wire,size);free(wire);}
        }
    }
    int chunk_budget=4;
    for(int i=0;i<g_lan_dirty_count&&chunk_budget>0;++i){
        LanDirtyChunk *c=&g_lan_dirty_chunks[i];if(!c->pending)continue;
        size_t size=0;unsigned char *wire=lan_capture_chunk(c->cx,c->cz,&size);
        if(wire){
            for(int id=1;id<6;++id){LanPeer *p=&g_lan_peers[id];if(p->ready&&p->tx_len<262144)(void)lan_queue(p,LAN_CHUNK_UPDATE,wire,size);}
            free(wire);
        }
        c->pending=0;--chunk_budget;
    }
    if(now_seconds()-g_lan_snapshot_time<0.045)return;g_lan_snapshot_time=now_seconds();
    LanSnapshot s={0};
    s.phase=g_lan_phase;s.countdown=g_lan_countdown;s.bots_enabled=g_lan_bots_enabled;memcpy(s.teams,g_lan_teams,sizeof(s.teams));
    s.humans[0].active=1;s.humans[0].health=g_player_dead?0:g_player_health;s.humans[0].ammo=g_breach_ammo;
    s.humans[0].x=g_player_x;s.humans[0].y=g_player_y;s.humans[0].z=g_player_z;s.humans[0].yaw=g_player_yaw;s.humans[0].pitch=g_player_pitch;
    s.humans[0].flash=now_seconds()-g_breach_last_shot<0.1?2:0;snprintf(s.humans[0].name,32,"%.31s",g_multiplayer_username);
    if(g_player_dead){s.killer_id[0]=g_lan_killcam_killer_id;s.killer_bot[0]=g_lan_killcam_killer_bot;snprintf(s.killer_name[0],32,"%.31s",g_lan_killcam_killer_name);}
    s.weapons[0]=breach_weapon_current();s.aims[0]=g_breach_aim;
    s.atomic_hurt[0]=g_player_atomic_hurt;memcpy(s.drops,g_breach_gun_drops,sizeof(s.drops));
    for(int i=1;i<6;++i)if(g_lan_peers[i].joined){
        LanPeer *p=&g_lan_peers[i];s.humans[i]=p->human;s.weapons[i]=p->weapon;s.aims[i]=p->aim;
        s.atomic_hurt[i]=p->atomic_hurt;
        s.killer_id[i]=p->killer_id;s.killer_bot[i]=p->killer_bot;
        snprintf(s.killer_name[i],32,"%.31s",p->killer_name);
        if(p->human.flash>0)--p->human.flash;
    }
    for(int i=0;i<MAX_PASSIVE_MOBS-6;++i){
        PassiveMob *m=&g_passive_mobs[i];if(!m->active||m->breach_remote_player)continue;
        LanMob *w=&s.mobs[s.mob_count++];w->slot=i;w->type=m->type;w->bot=m->breach_bot;w->entity=m->entity_id;
        w->health=m->health;w->death=m->death_time;w->flash=m->breach_bot_flash;
        s.mob_atomic_hurt[i]=m->breach_atomic_hurt;
        w->team=m->breach_team;
        w->x=m->x;w->y=m->y;w->z=m->z;w->yaw=m->render_yaw;w->head=m->head_yaw;w->pitch=m->pitch;w->limb=m->limb_swing;w->move=m->limb_amount;
    }
    for(int i=1;i<6;++i){LanPeer *p=&g_lan_peers[i];if(p->ready&&p->tx_len<65536)(void)lan_queue(p,LAN_SNAPSHOT,&s,sizeof(s));}
}
#endif
