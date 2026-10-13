/* Native local shooter opponents. Reuse mob A* navigation, collision, gravity,
 * animation and damage, but replace zombie melee AI with ranged tactics. */
#define BREACH_BOT_COUNT 3
#define BREACH_BOT_MAGAZINE 24

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

static void breach_bot_fire(PassiveMob *m, float distance) {
    float ox = m->x, oy = m->y + 1.5f, oz = m->z;
    float dx = g_player_x - ox;
    float dy = (g_player_y - 0.35f) - oy;
    float dz = g_player_z - oz;
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
    ++m->breach_bot_burst;
    m->breach_bot_flash = 2;
    m->attack_time = m->breach_bot_burst >= 3 ? 14 : 4;
    if (m->breach_bot_burst >= 3) m->breach_bot_burst = 0;
    const char *paths[] = {"src/assets/bbench/mac10_oneshot.ogg", "assets/bbench/mac10_oneshot.ogg", "mac10_oneshot.ogg"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        if (file_exists(paths[i])) {
            pex_sound_play_file_direct(paths[i], 0.65f / (1.0f + distance * 0.08f), 1.0f);
            break;
        }
    }
    FlatAABB player = {g_player_x-0.3f, g_player_y-1.62f, g_player_z-0.3f,
                       g_player_x+0.3f, g_player_y+0.18f, g_player_z+0.3f};
    float hit = 0;
    if (!passive_ray_intersect_aabb(&player, ox, oy, oz, dx, dy, dz, 48.0f, &hit) ||
        !passive_ray_clear_blocks_125(ox, oy, oz, ox+dx*hit, oy+dy*hit, oz+dz*hit)) return;
    PexDamageSource source = pex_damage_source_simple(PEX_DAMAGE_ARROW);
    source.damage_type = "bullet";
    source.true_kind = PEX_DAMAGE_ENTITY_MOB;
    source.true_mob_index = passive_mob_index_of(m);
    source.true_mob_type = m->type;
    source.true_x = ox; source.true_y = oy; source.true_z = oz;
    (void)player_attack_entity_from(source, 2);
}

static float breach_bot_tick_ai(PassiveMob *m, int in_liquid) {
    if (!m || !m->breach_bot || g_mp_connected) return 0.0f;
    if (m->breach_bot_flash > 0) --m->breach_bot_flash;
    m->move_controller.update = 0;
    m->look_controller.update = 0;
    m->jump_controller.jump = 0;
    if (g_player_dead || g_screen != SCREEN_INGAME) { passive_navigation_clear(m); return 0.0f; }
    float dx = g_player_x - m->x, dz = g_player_z - m->z;
    float distance = sqrtf(dx*dx + dz*dz);
    int visible = distance < 48.0f && passive_ray_clear_blocks_125(
        m->x, m->y+1.5f, m->z, g_player_x, g_player_y-0.35f, g_player_z);
    if (visible) {
        m->breach_bot_memory = 120;
        if (m->breach_bot_seen_ticks < 1000) ++m->breach_bot_seen_ticks;
        m->breach_bot_last_x = g_player_x;
        m->breach_bot_last_y = g_player_y - 1.62f;
        m->breach_bot_last_z = g_player_z;
        passive_look_helper_set_look(m, g_player_x, g_player_y-0.35f, g_player_z, 35, 25);
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
    if (m->age % 20 == (m->entity_id % 20) || !m->has_path_target) {
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
            passive_navigation_set_goal(m, tx, ty, tz, m->breach_bot_reload > 0 ? 0.75f : 0.65f);
        } else if (!m->has_path_target || m->path_index >= m->path_len || m->stuck_ticks > 35) {
            (void)breach_bot_pick_patrol(m);
        }
    }
    passive_navigation_update(m);
    float forward = passive_move_helper_update(m, in_liquid);
    if (!visible) passive_look_helper_set_look(m, m->target_x, m->y+1.5f, m->target_z, 30, 20);
    passive_look_helper_update(m);
    if (m->collided_horizontal && m->on_ground && forward > 0.0f) passive_jump_helper_set_jumping(m);
    passive_jump_helper_update(m);
    if (visible && m->breach_bot_seen_ticks >= 8 && m->breach_bot_reload == 0 &&
        m->breach_bot_ammo > 0 && m->attack_time <= 0) breach_bot_fire(m, distance);
    return forward;
}

static void breach_bots_update(void) {
    if (!breach_active() || !breach_map_read_spawn(g_loaded_world_dir, NULL, NULL, NULL, NULL)) return;
    int count = 0;
    for (int i = 0; i < MAX_PASSIVE_MOBS; ++i)
        if (g_passive_mobs[i].active && g_passive_mobs[i].breach_bot) ++count;
    if (count >= BREACH_BOT_COUNT || g_ingame_ticks % 40 != 0) return;
    float points[16][4];
    int point_count = breach_map_spawn_points(g_loaded_world_dir, points);
    if (!point_count) return;
    while (count < BREACH_BOT_COUNT) {
        int best = -1;
        float best_score = -1;
        int start = rand() % point_count;
        for (int i = 0; i < point_count; ++i) {
            int j = (start+i) % point_count;
            int x = (int)floorf(points[j][0]), y = (int)floorf(points[j][1]), z = (int)floorf(points[j][2]);
            if (!flat_chunk_generated_at_block(x,z) || flat_get_block(x,y,z) || flat_get_block(x,y+1,z)) continue;
            float dx = points[j][0]-g_player_x, dz = points[j][2]-g_player_z;
            float player_distance = dx*dx+dz*dz;
            if (player_distance < 144.0f) continue;
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
