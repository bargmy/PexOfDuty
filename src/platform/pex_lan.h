#ifndef PEX_LAN_H
#define PEX_LAN_H
#if defined(_WIN32) && !defined(PEX_PLATFORM_XBOX_UWP)
#define PEX_LAN_SUPPORTED 1
#else
#define PEX_LAN_SUPPORTED 0
#endif
#define PEX_LAN_MAX_HUMANS 6
#define PEX_LAN_PROTOCOL_VERSION 5
#ifndef PEX_LAN_DISCOVERY_PORT
#define PEX_LAN_DISCOVERY_PORT 27566
#endif
typedef struct PexLanGame {
    char address[48], name[64];
    int port, humans;
    double seen;
} PexLanGame;
static PexLanGame g_lan_games[8];
static int g_lan_game_count = 0;
static char g_lan_status[160] = "Searching for PexOfDuty LAN games...";
static int g_lan_mode = 0; /* 0 offline, 1 host, 2 joining/client */
static int g_lan_phase=0; /* 0 lobby, 1 countdown, 2 match */
static int g_lan_countdown=5;
static int g_lan_bots_enabled=1;
static int g_lan_teams[6]={0,1,0,1,0,1};
static int g_lan_present[6];
static char g_lan_names[6][32];
static int g_lan_killcam_active,g_lan_killcam_ticks,g_lan_killcam_skip_allowed,g_lan_killcam_respawn_sent;
static int g_lan_killcam_killer_bot=0,g_lan_killcam_killer_id=-1;
static char g_lan_killcam_killer_name[48]="Unknown";
static int pex_lan_waiting(void) { return g_lan_mode && g_lan_phase!=2; }
static int pex_lan_team_count(int team) {
    int n=0;for(int i=0;i<6;++i)if(g_lan_present[i]&&g_lan_teams[i]==team)++n;return n;
}
static int pex_lan_active(void) { return g_lan_mode != 0; }
static int pex_lan_client(void) { return g_lan_mode == 2; }
static int pex_lan_host(void) { return g_lan_mode == 1; }
static int pex_lan_world_screen(void) {
    return pex_lan_active() && g_loaded_world_dir[0] && g_screen!=SCREEN_TITLE &&
           g_screen!=SCREEN_MULTIPLAYER && g_screen!=SCREEN_CONNECTING &&
           g_screen!=SCREEN_GENERATING && g_screen!=SCREEN_SAVING_QUIT;
}
#if PEX_LAN_SUPPORTED
static int pex_lan_open(void);
static int pex_lan_join(int index);
static int pex_lan_join_address(const char *address);
static void pex_lan_frame(void);
static void pex_lan_tick_end(void);
static void pex_lan_stop(void);
static void pex_lan_shot(void);
static void pex_lan_reload(void);
static void pex_lan_respawn(void);
static void pex_lan_mark_chunk(int cx,int cz);
static int pex_lan_humans(void);
static int pex_lan_hit_human(int id, int damage, const PexDamageSource *source);
static int pex_lan_bot_target(float x,float y,float z,float *tx,float *ty,float *tz);
static int pex_lan_enemy_target(PassiveMob *m,float *x,float *y,float *z);
static void pex_lan_choose_team(int team);
static void pex_lan_start_match(void);
static void pex_lan_toggle_bots(void);
static int pex_lan_friendly(const PassiveMob *m);
static void pex_lan_killcam_from_source(const PexDamageSource *source);
static void pex_lan_killcam_begin(int killer_bot,int killer_id,const char *name);
static void pex_lan_killcam_tick(void);
static void pex_lan_killcam_skip(void);
static int pex_lan_killcam_camera(float *x,float *y,float *z,float *yaw,float *pitch);
static void lan_team_spawn(int team,float *x,float *y,float *z,float *yaw);
static void pex_lan_send_skin(void);
static Texture *pex_lan_skin_texture(int id);
#else
static int pex_lan_open(void) { return 0; }
static int pex_lan_join(int i) { (void)i; return 0; }
static int pex_lan_join_address(const char *a) { (void)a; return 0; }
static void pex_lan_frame(void) { }
static void pex_lan_tick_end(void) { }
static void pex_lan_stop(void) { g_lan_mode = 0; }
static void pex_lan_shot(void) { }
static void pex_lan_reload(void) { }
static void pex_lan_respawn(void) { }
static void pex_lan_mark_chunk(int x,int z) { (void)x;(void)z; }
static int pex_lan_humans(void) { return 1; }
static int pex_lan_hit_human(int id,int damage,const PexDamageSource *s) { (void)id;(void)damage;(void)s;return 0; }
static int pex_lan_bot_target(float x,float y,float z,float *tx,float *ty,float *tz) {
    (void)x;(void)y;(void)z;*tx=g_player_x;*ty=g_player_y;*tz=g_player_z;return g_player_dead?-1:0;
}
static int pex_lan_enemy_target(PassiveMob *m,float *x,float *y,float *z) { return pex_lan_bot_target(m->x,m->y,m->z,x,y,z); }
static void pex_lan_choose_team(int t) { (void)t; }
static void pex_lan_start_match(void) { }
static void pex_lan_toggle_bots(void) { }
static int pex_lan_friendly(const PassiveMob *m) { (void)m;return 0; }
static void pex_lan_killcam_from_source(const PexDamageSource *s) { (void)s; }
static void pex_lan_killcam_begin(int b,int i,const char *n) { (void)b;(void)i;(void)n; }
static void pex_lan_killcam_tick(void) { }
static void pex_lan_killcam_skip(void) { }
static int pex_lan_killcam_camera(float *x,float *y,float *z,float *yaw,float *pitch) { (void)x;(void)y;(void)z;(void)yaw;(void)pitch;return 0; }
static void lan_team_spawn(int t,float *x,float *y,float *z,float *yaw) { (void)t;(void)x;(void)y;(void)z;(void)yaw; }
static void pex_lan_send_skin(void) { }
static Texture *pex_lan_skin_texture(int id) { (void)id;return tex_steve_default.id?&tex_steve_default:&tex_steve; }
#endif
static int pex_lan_bot_count(void) { return g_lan_bots_enabled ? PEX_LAN_MAX_HUMANS - pex_lan_humans() : 0; }
#endif
