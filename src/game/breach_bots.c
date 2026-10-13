/* Native local shooter opponents. Reuse mob A* navigation, collision, gravity,
 * animation and damage, but replace zombie melee AI with ranged tactics. */
#define BREACH_BOT_COUNT 5
#define BREACH_BOT_MAGAZINE 24

/* Project tactical destinations onto supported, clear cells. In particular,
 * a strafe destination inside a bus/house must not become an impossible goal. */
static int breach_bot_set_safe_goal(PassiveMob *m,float gx,float gy,float gz,float speed) {
    int bx=(int)floorf(gx),by=(int)floorf(gy+.5f),bz=(int)floorf(gz);
    float best=1e12f;int px=0,py=0,pz=0,found=0;
    for(int dz=-3;dz<=3;++dz)for(int dx=-3;dx<=3;++dx)for(int dy=-3;dy<=2;++dy){
        int x=bx+dx,y=by+dy,z=bz+dz;
        if(!flat_chunk_generated_at_block(x,z)||!passive_path_can_stand_at(m->type,x,y,z))continue;
        int floor=flat_get_block(x,y-1,z);
        if(block_is_liquid(floor)||block_is_liquid(flat_get_block(x,y,z)))continue;
        float ox=x+.5f-gx,oz=z+.5f-gz,score=ox*ox+oz*oz+dy*dy*2.0f;
        for(int i=0;i<MAX_PASSIVE_MOBS;++i){PassiveMob *other=&g_passive_mobs[i];if(other==m||!other->active||other->death_time)continue;
            float sx=x+.5f-other->x,sz=z+.5f-other->z;if(sx*sx+sz*sz<1.44f)score+=12;}
        if(score<best){best=score;px=x;py=y;pz=z;found=1;}
    }
    if(!found)return 0;
    passive_navigation_set_goal(m,px+.5f,(float)py,pz+.5f,speed);return 1;
}

static int breach_bot_pick_patrol(PassiveMob *m) {
    static const int patrol[][2] = {
        {-20,-115}, {0,-120}, {24,-115}, {24,-70}, {0,-65}, {-20,-70}
    };
    int first = (m->breach_bot_patrol + 1 + rand() % 3) % 6;
    for (int i = 0; i < 6; ++i) {
        int index = (first + i) % 6;
        for (int r = 0; r <= 4; ++r) {
            for (int dz = -r; dz <= r; ++dz) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (abs(dx) != r && abs(dz) != r) continue;
                    int x = patrol[index][0] + dx, z = patrol[index][1] + dz;
                    if (!flat_chunk_generated_at_block(x, z)) continue;
                    for (int y = 66; y >= 63; --y) {
                        if (!passive_path_can_stand_at(m->type, x, y, z)) continue;
                        m->breach_bot_patrol = index;
                        passive_navigation_set_goal(m, x + 0.5f, (float)y, z + 0.5f, 0.65f);
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

static void breach_bot_fire(PassiveMob *m, float distance, int target_id, float tx, float ty, float tz) {
    float ox = m->x, oy = m->y + 1.5f, oz = m->z;
    float dx = tx - ox;
    float dy = (ty - 0.35f) - oy;
    float dz = tz - oz;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.001f) return;
    dx /= len; dy /= len; dz /= len;
    /* Bots need time to acquire the target and can miss. Jumping/running
       increases their aim error; no unavoidable damage merely from seeing you. */
    float spread = 0.018f + (g_player_sprinting ? 0.012f : 0.0f) +
                   (g_player_on_ground ? 0.0f : 0.010f);
    dx += (pex_rand_float01() - pex_rand_float01()) * spread;
    dy += (pex_rand_float01() - pex_rand_float01()) * spread;
    dz += (pex_rand_float01() - pex_rand_float01()) * spread;
    len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.001f) return;
    dx /= len; dy /= len; dz /= len;
    --m->breach_bot_ammo;
    breach_spawn_bullet_visual(ox,oy,oz,dx,dy,dz,48);
    ++m->breach_bot_burst;
    m->breach_bot_flash = 2;
    m->attack_time = m->breach_bot_burst >= 3 ? 14 : 4;
    if (m->breach_bot_burst >= 3) m->breach_bot_burst = 0;
    const char *paths[] = {"src/assets/bbench/mac10_oneshot.ogg", "assets/bbench/mac10_oneshot.ogg", "mac10_oneshot.ogg"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        if (file_exists(paths[i])) {
            pex_sound_play_file_direct(paths[i], PEX_SHOT_REMOTE_VOLUME / (1.0f + distance * 0.10f), 1.0f);
            break;
        }
    }
    FlatAABB player = {tx-0.3f, ty-1.62f, tz-0.3f, tx+0.3f, ty+0.18f, tz+0.3f};
    float hit = 0;
    if (!passive_ray_intersect_aabb(&player, ox, oy, oz, dx, dy, dz, 48.0f, &hit) ||
        !passive_ray_clear_blocks_125(ox, oy, oz, ox+dx*hit, oy+dy*hit, oz+dz*hit)) return;
    PexDamageSource source = pex_damage_source_simple(PEX_DAMAGE_ARROW);
    source.damage_type = "bullet";
    source.true_kind = PEX_DAMAGE_ENTITY_MOB;
    source.true_mob_index = passive_mob_index_of(m);
    source.true_mob_type = m->type;
    source.true_x = ox; source.true_y = oy; source.true_z = oz;
    if(pex_lan_host()&&target_id>=6){
        PassiveMob *target=&g_passive_mobs[target_id-6];
        if(target->breach_team!=m->breach_team)pex_mob_attack_entity_from(target,source,2);
    }else if(!pex_lan_hit_human(target_id,2,&source))(void)player_attack_entity_from(source, 2);
}

static float breach_bot_tick_ai(PassiveMob *m, int in_liquid) {
    if (!m || !m->breach_bot || m->breach_remote_player || g_mp_connected || pex_lan_client()) return 0.0f;
    if (m->breach_bot_flash > 0) --m->breach_bot_flash;
    m->move_controller.update = 0;
    m->look_controller.update = 0;
    m->jump_controller.jump = 0;
    if(m->breach_bot_reposition_ticks>0)--m->breach_bot_reposition_ticks;
    if(m->breach_bot_recovery_ticks>0)--m->breach_bot_recovery_ticks;
    float tx,ty,tz;
    int target_id=pex_lan_enemy_target(m,&tx,&ty,&tz);
    if (target_id<0 || (g_screen != SCREEN_INGAME && !pex_lan_host())) { passive_navigation_clear(m); return 0.0f; }
    float dx = tx - m->x, dz = tz - m->z;
    float distance = sqrtf(dx*dx + dz*dz);
    int visible = distance < 48.0f && passive_ray_clear_blocks_125(
        m->x, m->y+1.5f, m->z, tx, ty-0.35f, tz);
    if (visible) {
        m->breach_bot_memory = 120;
        if (m->breach_bot_seen_ticks < 1000) ++m->breach_bot_seen_ticks;
        m->breach_bot_last_x = tx;
        m->breach_bot_last_y = ty - 1.62f;
        m->breach_bot_last_z = tz;
        passive_look_helper_set_look(m, tx, ty-0.35f, tz, 35, 25);
    } else {
        m->breach_bot_seen_ticks = 0;
        if (m->breach_bot_memory > 0) --m->breach_bot_memory;
    }
    if (m->breach_bot_reload > 0) {
        if (--m->breach_bot_reload == 0) m->breach_bot_ammo = BREACH_BOT_MAGAZINE;
    } else if (m->breach_bot_ammo <= 0) {
        m->breach_bot_reload = 40;
        m->breach_bot_burst = 0;
    }

    /* Reposition instead of rushing into melee: approach if hidden/distant,
       retreat when too close, and change firing angles while in range. */
    if ((!m->has_path_target || m->breach_bot_reposition_ticks<=0) && m->breach_bot_recovery_ticks<=0) {
        m->breach_bot_reposition_ticks=40+(m->entity_id%15);
        if (m->breach_bot_memory > 0) {
            float tx = m->breach_bot_last_x, ty = m->breach_bot_last_y, tz = m->breach_bot_last_z;
            if (visible && distance > 0.1f) {
                float retreat = distance < 8.0f ? -7.0f : 0.0f;
                if (distance < 24.0f) {
                    float side = (m->breach_bot_patrol & 1) ? 5.0f : -5.0f;
                    tx = m->x + dx/distance*retreat - dz/distance*side;
                    tz = m->z + dz/distance*retreat + dx/distance*side;
                    ty = m->y;
                    ++m->breach_bot_patrol;
                }
            }
            if(!breach_bot_set_safe_goal(m,tx,ty,tz,m->breach_bot_reload>0?.75f:.65f))
                (void)breach_bot_pick_patrol(m);
        } else if (!m->has_path_target || m->path_index >= m->path_len || m->stuck_ticks > 35) {
            (void)breach_bot_pick_patrol(m);
        }
    }
    /* Recover after one second of genuine blocked movement, not five seconds
       of repeatedly jumping into a wall. Hold the detour long enough to move. */
    if(m->stuck_ticks>=20&&m->breach_bot_recovery_ticks<=0){
        float a=(m->yaw+((m->breach_bot_patrol++&1)?90.0f:-90.0f))*(float)M_PI/180.0f;
        passive_navigation_clear(m);m->path_recalc_cooldown=0;m->stuck_ticks=0;
        if(!breach_bot_set_safe_goal(m,m->x-sinf(a)*3,m->y,m->z+cosf(a)*3,.75f))
            (void)breach_bot_pick_patrol(m);
        m->breach_bot_recovery_ticks=30;m->breach_bot_reposition_ticks=30;
    }
    passive_navigation_update(m);
    float forward = passive_move_helper_update(m, in_liquid);
    if (!visible) passive_look_helper_set_look(m, m->target_x, m->y+1.5f, m->target_z, 30, 20);
    passive_look_helper_update(m);
    if (m->collided_horizontal && m->on_ground && forward > 0.0f && m->target_y>m->y+.25f)
        passive_jump_helper_set_jumping(m);
    passive_jump_helper_update(m);
    if (visible && m->breach_bot_seen_ticks >= 8 && m->breach_bot_reload == 0 &&
        m->breach_bot_ammo > 0 && m->attack_time <= 0) breach_bot_fire(m, distance,target_id,tx,ty,tz);
    return forward;
}

static void breach_bots_update(void) {
    if (pex_lan_client() || (!breach_active() && !pex_lan_host()) || !breach_map_read_spawn(g_loaded_world_dir, NULL, NULL, NULL, NULL)) return;
    if(pex_lan_host()&&g_lan_phase==0)return;
    int desired=pex_lan_bot_count();
    int team_bots[2]={0,0};
    if(pex_lan_host()){
        for(int i=0;i<MAX_PASSIVE_MOBS;++i){PassiveMob *m=&g_passive_mobs[i];
            if(!m->active||!m->breach_bot||m->breach_remote_player)continue;
            int team=m->breach_team;
            if(team<0||team>1||team_bots[team]>=3-pex_lan_team_count(team)){
                passive_path_release_storage(m);memset(m,0,sizeof(*m));
            }else ++team_bots[team];
        }
    }
    int count = 0;
    for (int i = 0; i < MAX_PASSIVE_MOBS; ++i)
        if (g_passive_mobs[i].active && g_passive_mobs[i].breach_bot && !g_passive_mobs[i].breach_remote_player) ++count;
    for(int i=MAX_PASSIVE_MOBS-1;i>=0&&count>desired;--i){
        PassiveMob *m=&g_passive_mobs[i];if(m->active&&m->breach_bot&&!m->breach_remote_player){
            passive_path_release_storage(m);memset(m,0,sizeof(*m));--count;
        }
    }
    if (count >= desired || (!pex_lan_host() && g_ingame_ticks % 40 != 0)) return;
    float points[16][4];
    int point_count = breach_map_spawn_points(g_loaded_world_dir, points);
    if (!point_count) return;
    while (count < desired) {
        int team=pex_lan_host()?(team_bots[0]<3-pex_lan_team_count(0)?0:1):0;
        int best = -1;
        float best_score = -1;
        int start = rand() % point_count;
        for (int i = 0; i < point_count; ++i) {
            int j = (start+i) % point_count;
            if(pex_lan_host()&&(points[j][0]<0?0:1)!=team)continue;
            int x = (int)floorf(points[j][0]), y = (int)floorf(points[j][1]), z = (int)floorf(points[j][2]);
            if (!flat_chunk_generated_at_block(x,z) || flat_get_block(x,y,z) || flat_get_block(x,y+1,z)) continue;
            float dx = points[j][0]-g_player_x, dz = points[j][2]-g_player_z;
            float player_distance = dx*dx+dz*dz;
            if (player_distance < 6.25f) continue;
            float spacing = 1.0e12f;
            for (int k = 0; k < MAX_PASSIVE_MOBS; ++k) {
                const PassiveMob *other = &g_passive_mobs[k];
                if (!other->active || !other->breach_bot) continue;
                dx = points[j][0]-other->x; dz = points[j][2]-other->z;
                spacing = fminf(spacing, dx*dx+dz*dz);
            }
            if (spacing < 4.0f) continue;
            float score = fminf(player_distance, 10000.0f) + fminf(spacing, 400.0f);
            if (score > best_score) { best = j; best_score = score; }
        }
        if (best < 0) break;
        PassiveMob *m = passive_mob_alloc();
        if (!m) break;
        passive_mob_init(m, PASSIVE_MOB_ZOMBIE, points[best][0], points[best][1], points[best][2]);
        m->breach_bot = 1;
        m->breach_team=team;++team_bots[team];
        m->breach_bot_ammo = BREACH_BOT_MAGAZINE;
        m->breach_bot_patrol = rand()%6;
        m->yaw = m->prev_yaw = points[best][3];
        m->head_yaw = m->prev_head_yaw = m->yaw;
        m->render_yaw = m->prev_render_yaw = m->yaw;
        m->navigation.avoid_sun = 0;
        m->navigation.pass_closed_doors = 0; /* never walk through closed doors */
        ++count;
    }
}
