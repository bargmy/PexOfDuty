/* Headless Windows regression test: includes the real unity build so bot
 * navigation, ray tests, damage and native chunk decoding are not mocked.
 * Compile with the normal Windows libraries, without -mwindows. */
#include "../main.c"
#include <assert.h>

static void test_map_load(void) {
    g_flat_world_origin_x = -(FLAT_WORLD_SIZE / 2);
    g_flat_world_origin_z = -(FLAT_WORLD_SIZE / 2);
    FILE *list = fopen("src/assets/maps/nuketown/chunks.txt", "rb");
    assert(list);
    unsigned char *blocks = malloc(FLAT_CHUNK_BLOCK_COUNT);
    unsigned char *meta = malloc(FLAT_CHUNK_BLOCK_COUNT);
    assert(blocks && meta);
    char name[96], path[MAX_PATHBUF];
    int loaded = 0;
    while (fgets(name, sizeof(name), list)) {
        name[strcspn(name, "\r\n")] = 0;
        snprintf(path, sizeof(path), "src/assets/maps/nuketown/chunks/%s", name);
        FILE *f = fopen(path, "rb");
        assert(f);
        char magic[8]; int cx, cz, count;
        assert(fread(magic, 1, 8, f) == 8);
        assert(fread(&cx, 4, 1, f) == 1 && fread(&cz, 4, 1, f) == 1 && fread(&count, 4, 1, f) == 1);
        int lx = cx*16 - g_flat_world_origin_x, lz = cz*16 - g_flat_world_origin_z;
        if (lx < 0 || lz < 0 || lx+16 > FLAT_WORLD_SIZE || lz+16 > FLAT_WORLD_SIZE) { fclose(f); continue; }
        assert(chunk_delta_read_payload(f, magic, cx, cz, count, cx, cz, blocks, meta));
        fclose(f);
        for (int y = 0; y < 256; ++y) for (int z = 0; z < 16; ++z) {
            memcpy(&g_flat_blocks[y][flat_storage_z_world(cz*16+z)][flat_storage_x_world(cx*16)], blocks+y*256+z*16, 16);
            memcpy(&g_flat_meta[y][flat_storage_z_world(cz*16+z)][flat_storage_x_world(cx*16)], meta+y*256+z*16, 16);
        }
        g_flat_world_chunk_generated[lz/16][lx/16] = 1;
        g_flat_chunk_world_cx[lz/16][lx/16] = cx;
        g_flat_chunk_world_cz[lz/16][lx/16] = cz;
        ++loaded;
    }
    free(blocks); free(meta); fclose(list);
    printf("Loaded native map chunks in active cache: %d\n", loaded);
}

static PassiveMob *test_bot_at(float x, float y, float z) {
    passive_mobs_reset();
    PassiveMob *m = passive_mob_alloc();
    assert(m);
    passive_mob_init(m, PASSIVE_MOB_ZOMBIE, x, y, z);
    m->breach_bot = 1;
    m->breach_bot_ammo = BREACH_BOT_MAGAZINE;
    m->navigation.pass_closed_doors = 0;
    m->navigation.avoid_sun = 0;
    return m;
}

static void test_wall(int block) {
    for (int y = 63; y <= 68; ++y) for (int x = -36; x <= -34; ++x)
        g_flat_blocks[y][flat_storage_z_index(-88)][flat_storage_index(x)] = (unsigned char)block;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    srand(argc > 1 ? (unsigned int)atoi(argv[1]) : 1);
    g_screen = SCREEN_INGAME;
    g_opts.difficulty = 2;
    g_opts.sound = 0;
    g_game_mode = 0;
    g_world_time = 6000;
    g_world_type = 0;
    snprintf(g_loaded_world_dir, sizeof(g_loaded_world_dir), "src/assets/maps/nuketown");
    test_map_load();
    float points[16][4];
    assert(breach_map_spawn_points(g_loaded_world_dir, points) == 6);
    for (int i = 0; i < 6; ++i)
        assert(!flat_player_aabb_collides(points[i][0], points[i][1]+1.62f, points[i][2]));
    puts("PASS: six supplied spawn points are collision-free");

    g_player_x = -35; g_player_y = 65.62f; g_player_z = -90;
    g_player_health = 20; g_player_on_ground = 1;
    PassiveMob *m = test_bot_at(48,64,-85);
    float start_x = m->x, start_z = m->z;
    for (int i = 0; i < 160; ++i) passive_mob_tick_living(m);
    float traveled = hypotf(m->x-start_x, m->z-start_z);
    printf("Native patrol movement: %.3f blocks, path nodes=%d\n", traveled, m->path_len);
    assert(m->active && traveled > 2.0f);
    puts("PASS: bot traverses actual Nuketown terrain using native A* and physics");

    m = test_bot_at(-35,64,-85);
    int fired = 0, damage = 0, reloaded = 0;
    for (int i = 0; i < 400; ++i) {
        int reload = m->breach_bot_reload;
        if (m->attack_time > 0) --m->attack_time;
        if (m->path_recalc_cooldown > 0) --m->path_recalc_cooldown;
        ++m->age; g_hearts_life = 0; g_player_health = 20;
        (void)breach_bot_tick_ai(m, 0);
        if (m->breach_bot_flash == 2) ++fired;
        if (g_player_health < 20) ++damage;
        if (reload > 0 && m->breach_bot_reload == 0) ++reloaded;
    }
    printf("Visible target: shots=%d, damage events=%d, reloads=%d\n", fired, damage, reloaded);
    assert(fired > 24 && damage > 0 && reloaded > 0);
    puts("PASS: aimed bursts, actual player damage, magazine exhaustion and reload");

    test_wall(BLOCK_STONE);
    m->breach_bot_reload = 0; m->breach_bot_ammo = 24;
    for (int i = 0; i < 40; ++i) { m->attack_time=0; ++m->age; (void)breach_bot_tick_ai(m,0); }
    assert(m->breach_bot_ammo == 24 && m->breach_bot_seen_ticks == 0);
    g_player_yaw = 0; g_player_pitch = 0; g_breach_ammo = 32; g_breach_last_shot=-100;
    breach_fire(now_seconds());
    assert(m->health == 20);
    puts("PASS: wall blocks bot fire and player hitscan damage");
    test_wall(0);
    g_player_dead = 1;
    (void)breach_bot_tick_ai(m,0);
    assert(m->breach_bot_ammo == 24);
    g_player_dead = 0;
    puts("PASS: bot stops attacking a dead player");
    g_screen = SCREEN_TITLE;
    (void)breach_bot_tick_ai(m,0);
    assert(m->breach_bot_ammo == 24);
    g_screen = SCREEN_INGAME;
    puts("PASS: bot does not shoot outside gameplay");

    for (int i = 0; i < 5 && m->death_time <= 0; ++i) {
        m->damage_cooldown=0; g_breach_last_shot=-100;
        breach_fire(now_seconds());
    }
    assert(m->death_time > 0);
    puts("PASS: player can kill an armed bot");

    passive_mobs_reset();
    g_ingame_ticks = 40;
    breach_bots_update();
    int bots = 0;
    for (int i = 0; i < MAX_PASSIVE_MOBS; ++i) if (g_passive_mobs[i].active && g_passive_mobs[i].breach_bot) {
        PassiveMob *bot = &g_passive_mobs[i];
        assert(hypotf(bot->x-g_player_x, bot->z-g_player_z) >= 12.0f);
        ++bots;
    }
    assert(bots == 3);
    puts("PASS: three bots spawn away from player at supplied points");
    float sx, sy, sz, yaw;
    assert(breach_map_pick_spawn(g_loaded_world_dir, &sx, &sy, &sz, &yaw));
    assert(sx == -35 && sy == 66.62f);
    puts("PASS: player respawn selects the side away from living bots");
    for (int i = 0; i < MAX_PASSIVE_MOBS; ++i) if (g_passive_mobs[i].breach_bot) {
        g_passive_mobs[i].active = 0;
        break;
    }
    breach_bots_update();
    bots = 0;
    for (int i = 0; i < MAX_PASSIVE_MOBS; ++i)
        if (g_passive_mobs[i].active && g_passive_mobs[i].breach_bot) ++bots;
    assert(bots == 3);
    puts("PASS: eliminated bot slots are replenished");
    if (argc > 2) {
        FILE *f = fopen(argv[2], "w+b");
        assert(f);
        passive_mobs_write_to_file(f, g_passive_mobs);
        rewind(f);
        int saved = -1;
        assert(fread(&saved, sizeof(saved), 1, f) == 1 && saved == 0);
        fclose(f);
        puts("PASS: runtime bots are not saved as ordinary zombies");
    }
    return 0;
}
