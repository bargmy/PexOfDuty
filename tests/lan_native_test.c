/* Native socket integration fixture. Python drives discovery and multiple
 * framed TCP clients; this process uses the actual game LAN implementation. */
#define PEX_LAN_DISCOVERY_PORT 27567 /* keep fixtures separate from a running game */
#define PEX_LAN_HEADLESS_TEST 1 /* validate CPU skins/relay without a GL context */
#define main breach_bot_regression_main
#include "breach_bots_native_test.c"
#undef main

static int count_bots(void) {
    int count=0;
    for(int i=0;i<MAX_PASSIVE_MOBS;++i)if(g_passive_mobs[i].active&&g_passive_mobs[i].breach_bot&&!g_passive_mobs[i].breach_remote_player)++count;
    return count;
}

int main(int argc,char **argv) {
    assert(argc>=3);
    setvbuf(stdout,NULL,_IONBF,0);srand(7);
    g_opts.sound=0;g_opts.difficulty=2;g_game_mode=0;
    g_world_type=0;g_world_time=6000;g_ingame_ticks=40;
    g_player_x=-35;g_player_y=65.62f;g_player_z=-90;
    g_player_health=20;g_player_on_ground=1;
    snprintf(g_mc_dir,sizeof(g_mc_dir),"%s",argv[2]);
    snprintf(g_loaded_world_dir,sizeof(g_loaded_world_dir),"src/assets/maps/nuketown");
    snprintf(g_loaded_world_name,sizeof(g_loaded_world_name),"Nuketown LAN Test");
    test_map_load();
    static unsigned char fixture_skin[64*64*4];
    for(int i=0;i<64*64;++i){fixture_skin[i*4]=!strcmp(argv[1],"host")?180:20;
        fixture_skin[i*4+1]=!strcmp(argv[1],"host")?20:180;fixture_skin[i*4+2]=40;fixture_skin[i*4+3]=255;}
    tex_steve.w=64;tex_steve.h=64;tex_steve.rgba=fixture_skin;
    if(!strcmp(argv[1],"host")) {
        g_screen=SCREEN_PAUSE;
        assert(pex_lan_open());assert(ingame_screen_allows_tick());
        breach_bots_update();assert(count_bots()==0);
        printf("HOST_READY %d HUMAN_SIZE %u SNAPSHOT_SIZE %u\n",g_lan_port,(unsigned)sizeof(LanHuman),(unsigned)sizeof(LanSnapshot));
        double started=now_seconds(),next_tick=started;
        int last_humans=-1,last_bots=-1;
        int changed=0;char change_file[MAX_PATHBUF];snprintf(change_file,sizeof(change_file),"%s/block.change",argv[2]);
        while(now_seconds()-started<60){
            if(argc>3&&file_exists(argv[3]))break;
            pex_lan_frame();
            char start_file[MAX_PATHBUF];snprintf(start_file,sizeof(start_file),"%s/match.start",argv[2]);
            if(g_lan_phase==0&&file_exists(start_file))pex_lan_start_match();
            if(!changed&&file_exists(change_file)){
                g_flat_blocks[90][flat_storage_z_index(100)][flat_storage_index(100)]=BLOCK_GLASS;
                mark_flat_chunk_modified_at(100,100);changed=1;puts("HOST_BLOCK_UPDATED");
            }
            if(now_seconds()>=next_tick){
                ++g_ingame_ticks;pex_lan_tick_end();next_tick+=0.05;
                int humans=pex_lan_humans(),bots=count_bots();
                if(humans!=last_humans||bots!=last_bots){printf("SLOTS %d %d\n",humans,bots);last_humans=humans;last_bots=bots;}
            }
            Sleep(2);
        }
        pex_lan_stop();puts("HOST_STOPPED");return 0;
    }
    if(!strcmp(argv[1],"client")) {
        assert(argc>=4);g_screen=SCREEN_MULTIPLAYER;
        assert(pex_lan_join_address(argv[3]));
        double started=now_seconds();int announced=0,block_seen=0;
        double duration=argc>4?atof(argv[4]):12.0;
        while(now_seconds()-started<duration && pex_lan_client()){
            char stop_file[MAX_PATHBUF];snprintf(stop_file,sizeof(stop_file),"%s/client.stop",argv[2]);
            if(file_exists(stop_file))break;
            pex_lan_frame();
            /* Map loading is covered by native decoder tests. Do not start
               rendering/stream workers in this headless socket fixture. */
            if(g_lan_loading==2&&g_screen==SCREEN_GENERATING){g_worldgen.active=0;g_screen=SCREEN_INGAME;}
            if(g_lan_client_entered){
                pex_lan_tick_end();
                if(!announced){printf("CLIENT_READY %d CHUNKS %d CACHE %s\n",g_lan_self_id,g_lan_chunks_received,g_lan_cache);announced=1;}
                if(!block_seen&&flat_get_block(100,90,100)==BLOCK_GLASS){block_seen=1;puts("CLIENT_BLOCK_UPDATE_SEEN");}
            }
            Sleep(50);
        }
        assert(announced);pex_lan_stop();puts("CLIENT_STOPPED");return 0;
    }
    if(!strcmp(argv[1],"unit")) {
        assert(lan_init());g_lan_mode=1;g_lan_self_id=0;g_screen=SCREEN_PAUSE;
        g_lan_phase=2;
        for(int n=1;n<=6;++n){
            for(int i=1;i<6;++i){g_lan_peers[i].joined=i<n;g_lan_peers[i].human.health=20;}
            lan_lobby_names();breach_bots_update();assert(count_bots()==6-n);
        }
        for(int i=1;i<6;++i)g_lan_peers[i].joined=0;
        lan_lobby_names();breach_bots_update();assert(count_bots()==5);
        g_lan_peers[1].joined=1;g_lan_peers[1].ready=1;g_lan_peers[1].human.health=20;
        g_lan_peers[1].human.x=-35;g_lan_peers[1].human.y=65.62f;g_lan_peers[1].human.z=-85;
        g_player_dead=1;float x,y,z;
        assert(pex_lan_bot_target(-35,65.5f,-80,&x,&y,&z)==1);
        PexDamageSource source=pex_damage_source_simple(PEX_DAMAGE_ARROW);
        assert(pex_lan_hit_human(1,7,&source));assert(g_lan_peers[1].human.health==13);
        LanInput invalid={0};invalid.x=NAN;assert(!lan_input(&g_lan_peers[1],1,&invalid));
        g_lan_peers[1].human.health=0;invalid.x=-35;invalid.y=65.62f;invalid.z=-85;invalid.respawns=1;
        assert(lan_input(&g_lan_peers[1],1,&invalid));assert(g_lan_peers[1].human.health==20);
        assert(g_lan_peers[1].human.epoch==1);
        g_lan_phase=0;
        g_lan_peers[1].human.health=20;g_lan_peers[1].human.ammo=32;
        g_lan_peers[1].human.acknowledged_shots=0;g_lan_peers[1].shots=1;
        lan_human_fire(&g_lan_peers[1],1,1);assert(g_lan_peers[1].human.ammo==32);
        g_lan_teams[0]=0;g_lan_teams[1]=0;lan_lobby_names();
        assert(lan_set_team(1,1));assert(g_lan_teams[1]==1);
        for(int i=2;i<4;++i){g_lan_peers[i].joined=1;g_lan_teams[i]=1;}
        assert(!lan_set_team(0,1));assert(g_lan_teams[0]==0);
        for(int i=2;i<4;++i)g_lan_peers[i].joined=0;
        int mode=g_lan_mode;g_lan_mode=2;pex_lan_start_match();assert(g_lan_phase==0);g_lan_mode=mode;
        g_lan_phase=1;assert(!lan_set_team(1,0));
        assert(pex_lan_hit_human(1,7,&source));assert(g_lan_peers[1].human.health==20);
        g_lan_phase=2;lan_lobby_names();breach_bots_update();
        int teams[2]={0,0};PassiveMob *friendly=NULL;
        for(int i=0;i<MAX_PASSIVE_MOBS;++i){PassiveMob *m=&g_passive_mobs[i];
            if(m->active&&m->breach_bot&&!m->breach_remote_player){++teams[m->breach_team];if(!m->breach_team)friendly=m;}}
        assert(teams[0]==2&&teams[1]==2);assert(friendly&&pex_lan_friendly(friendly));
        int target=pex_lan_enemy_target(friendly,&x,&y,&z);assert(target>=0);
        assert(target<6?g_lan_teams[target]!=friendly->breach_team:g_passive_mobs[target-6].breach_team!=friendly->breach_team);
        source.true_kind=PEX_DAMAGE_ENTITY_MOB;source.true_mob_index=passive_mob_index_of(friendly);
        g_player_dead=0;g_player_health=20;assert(!player_attack_entity_from(source,2));assert(g_player_health==20);
        g_lan_teams[1]=0;g_lan_peers[1].hurt_until=0;
        assert(pex_lan_hit_human(1,7,&source));assert(g_lan_peers[1].human.health==20);
        puts("PASS team capacity/balance, team locks, lobby/countdown combat suppression, enemy-only bot targeting and friendly-fire protection");
        LanSkin red={0};red.id=0;red.width=64;red.height=64;red.bytes=64*64*4;memset(red.rgba,31,red.bytes);
        LanSkin blue=red;blue.id=1;blue.height=32;blue.bytes=64*32*4;memset(blue.rgba,71,blue.bytes);
        assert(lan_skin_valid(&red,16+red.bytes));assert(lan_skin_valid(&blue,16+blue.bytes));
        lan_store_skin(&red);lan_store_skin(&blue);
        assert(g_lan_skins[0].rgba[0]==31&&g_lan_skins[1].rgba[0]==71);
        blue.rgba[0]=99;lan_store_skin(&blue);assert(g_lan_skins[0].rgba[0]==31);
        blue.width=128;assert(!lan_skin_valid(&blue,16+blue.bytes));blue.width=64;
        assert(!lan_skin_valid(&blue,15+blue.bytes));blue.id=6;assert(!lan_skin_valid(&blue,16+blue.bytes));
        puts("PASS independent 64x32/64x64 skin caches, update isolation and invalid payload rejection");
        g_lan_view_x=100;g_lan_view_y=91.6f;g_lan_view_z=110;g_lan_view_yaw=180;g_lan_view_pitch=0;
        assert(lan_name_tag_visible(NULL,100,90,100,20));
        FlatFrustum view_box={{{1,0,0,-99},{-1,0,0,101},{0,1,0,-89},{0,-1,0,94},{0,0,1,-99},{0,0,-1,111}}};
        assert(lan_name_tag_visible(&view_box,100,90,100,20));
        view_box.p[0][3]=-102;assert(!lan_name_tag_visible(&view_box,100,90,100,20));
        assert(!lan_name_tag_visible(NULL,100,90,120,20));
        assert(!lan_name_tag_visible(NULL,100,90,100,0));
        for(int y=89;y<=93;++y)for(int x=99;x<=101;++x)g_flat_blocks[y][flat_storage_z_index(105)][flat_storage_index(x)]=BLOCK_STONE;
        assert(!lan_name_tag_visible(NULL,100,90,100,20));
        for(int y=89;y<=93;++y)for(int x=99;x<=101;++x)g_flat_blocks[y][flat_storage_z_index(105)][flat_storage_index(x)]=0;
        assert(lan_name_tag_visible(NULL,100,90,100,20));
        puts("PASS nametag visibility: in front, behind camera, dead, wall-occluded and reopened line of sight");
        memset(g_breach_gun_drops,0,sizeof(g_breach_gun_drops));
        int atomic_drops=0,regular_drops=0;
        for(int i=0;i<512;++i){
            memset(g_breach_gun_drops,0,sizeof(g_breach_gun_drops));
            breach_drop_gun(100,90,102,BREACH_WEAPON_M16);
            assert(g_breach_gun_drops[0].active);
            if(g_breach_gun_drops[0].weapon==BREACH_WEAPON_ATOMIC)++atomic_drops;
            else {assert(g_breach_gun_drops[0].weapon==BREACH_WEAPON_M16);++regular_drops;}
        }
        assert(atomic_drops>180&&atomic_drops<332&&atomic_drops+regular_drops==512);
        g_breach_gun_drops[0]=(BreachGunDrop){1,BREACH_WEAPON_ATOMIC,100,90.8f,102,1800};
        float drop_t=48;
        assert(breach_gun_drop_hit(100,90.8f,100,0,0,1,48,&drop_t)==0);
        assert(drop_t>1&&drop_t<2);
        assert(breach_gun_drop_hit(100,90.8f,96,0,0,1,48,NULL)==-1);
        assert(breach_gun_drop_hit(100,90.8f,100,0,0,1,.5f,NULL)==-1);
        g_flat_blocks[90][flat_storage_z_index(101)][flat_storage_index(100)]=BLOCK_STONE;
        assert(breach_gun_drop_hit(100,90.8f,100,0,0,1,48,NULL)==-1);
        g_flat_blocks[90][flat_storage_z_index(101)][flat_storage_index(100)]=0;
        g_breach_gun_drops[0].life=1;breach_gun_drops_tick();assert(!g_breach_gun_drops[0].active);
        g_lan_phase=2;g_lan_peers[1].ready=g_lan_peers[1].joined=1;
        g_lan_peers[1].human.x=100;g_lan_peers[1].human.y=90.8f;g_lan_peers[1].human.z=100;
        g_lan_peers[1].human.yaw=g_lan_peers[1].human.pitch=0;
        g_lan_peers[1].human.health=20;g_lan_peers[1].human.ammo=32;
        g_lan_peers[1].last_shot=-100;g_lan_peers[1].reload_end=-1;
        g_breach_gun_drops[0]=(BreachGunDrop){1,BREACH_WEAPON_ATOMIC,100,90.8f,102,1800};
        lan_human_fire(&g_lan_peers[1],1,1);
        assert(g_lan_peers[1].weapon==BREACH_WEAPON_ATOMIC&&g_lan_peers[1].pickup_locked);
        assert(!g_breach_gun_drops[0].active&&g_lan_peers[1].human.ammo==32);
        LanInput no_cheat={0};no_cheat.x=100;no_cheat.y=90.8f;no_cheat.z=100;
        no_cheat.shots=g_lan_peers[1].shots;no_cheat.reloads=g_lan_peers[1].reloads;no_cheat.respawns=g_lan_peers[1].respawns;
        assert(lan_input(&g_lan_peers[1],1,&no_cheat));assert(g_lan_peers[1].weapon==BREACH_WEAPON_ATOMIC);
        g_lan_peers[1].pickup_locked=0;g_lan_peers[1].weapon=BREACH_WEAPON_MAC10;no_cheat.weapon=BREACH_WEAPON_ATOMIC;
        assert(lan_input(&g_lan_peers[1],1,&no_cheat));assert(g_lan_peers[1].weapon==BREACH_WEAPON_MAC10);
        breach_weapon_equip(BREACH_WEAPON_MAC10);int normal_damage=breach_weapon_damage();
        breach_weapon_equip(BREACH_WEAPON_ATOMIC);assert(breach_weapon_damage()==2*normal_damage);
        assert(!breach_weapon_select(BREACH_WEAPON_ATOMIC));
        g_breach_selected_weapon=BREACH_WEAPON_M16;breach_weapon_on_spawn();assert(breach_weapon_current()==BREACH_WEAPON_M16);
        PassiveMob *hurt=&g_passive_mobs[0];passive_mob_init(hurt,PASSIVE_MOB_ZOMBIE,100,90,104);hurt->breach_bot=1;hurt->breach_team=1;
        source=pex_damage_source_simple(PEX_DAMAGE_ARROW);source.damage_type="bullet";source.breach_atomic=1;
        source.true_kind=PEX_DAMAGE_ENTITY_PLAYER;
        assert(pex_mob_attack_entity_from(hurt,source,8));assert(hurt->breach_atomic_hurt&&hurt->hurt_time>0);
        source.breach_atomic=0;assert(pex_mob_attack_entity_from(hurt,source,4));assert(!hurt->breach_atomic_hurt);
        printf("PASS drop variants (%d atomic / %d regular), proximity/occlusion, host pickup, no spoofed legendary, double damage, pink hit state and loss on respawn\n",atomic_drops,regular_drops);
        g_lan_phase=0;pex_lan_toggle_bots();assert(!g_lan_bots_enabled);
        g_lan_phase=2;breach_bots_update();assert(count_bots()==0);
        g_lan_phase=0;pex_lan_toggle_bots();assert(g_lan_bots_enabled);
        g_lan_phase=2;g_player_dead=0;g_lan_replay_head=g_lan_replay_count=0;
        g_player_x=10;lan_replay_record();g_player_x=11;lan_replay_record();
        pex_lan_killcam_begin(0,0,"Test killer");assert(g_lan_replay_length==3);
        g_player_x=200;float yaw,pitch;assert(pex_lan_killcam_camera(&x,&y,&z,&yaw,&pitch));assert(x==10);
        pex_lan_killcam_skip();assert(g_lan_killcam_ticks==100);
        pex_lan_killcam_begin(1,0,"Bot");pex_lan_killcam_skip();assert(g_lan_killcam_ticks==0);
        g_lan_killcam_active=0;
        puts("PASS bot toggle, recorded killer camera (not live), human skip and unskippable bot timer");
        g_lan_mode=2;g_lan_self_id=1;g_lan_client_entered=1;
        g_lan_killcam_active=1;g_lan_killcam_ticks=40;g_lan_killcam_respawn_sent=1;
        LanSnapshot respawn={0};respawn.phase=2;respawn.bots_enabled=1;
        respawn.humans[1].active=1;respawn.humans[1].epoch=g_lan_epoch+1;
        respawn.humans[1].health=20;respawn.humans[1].ammo=32;
        respawn.humans[1].x=-35;respawn.humans[1].y=66.62f;respawn.humans[1].z=-90;
        assert(lan_snapshot_apply(&respawn));
        assert(!g_lan_killcam_active&&!g_lan_killcam_respawn_sent&&!g_lan_replay_length);
        assert(!g_player_dead&&g_player_x==-35&&g_player_z==-90);
        puts("PASS client respawn restores spawn coordinates and clears replay camera/state");
        pex_lan_stop();puts("PASS LAN slots, disconnect refill, remote bot target/damage, NaN rejection, respawn epoch");return 0;
    }
    return 1;
}
