/* libbbench -- Blockbench .bbmodel geometry, texture and animation loader.
 * Portable C99; does not depend on GL, SDL, or PexCraft's source files.
 * Coordinates are Blockbench units (usually 16 px per Minecraft block).
 * MIT license -- see src/libbbench/README.md. */
#ifndef LIBBBENCH_H
#define LIBBBENCH_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BB_NAME 96
#define BB_UUID 48
#define BB_MAX_TEXTURES 32
#define BB_MAX_NODES 128
#define BB_MAX_CUBES 1024
#define BB_MAX_CLIPS 128
#define BB_MAX_TRACKS 8192
#define BB_MAX_KEYS 65536
#define BB_MAX_FACES 6

typedef struct BBTexture {
    char name[BB_NAME];
    int width,height,uv_width,uv_height;
    unsigned char *png;
    size_t png_size;  /* decoded PNG bytes, owned by model */
} BBTexture;
typedef struct BBFace {
    int texture; /* -1: omit face, >=0: material index; -2: no texture */
    float uv[4]; /* pixel atlas rectangle, can be flipped */
    int uv_rotation;
} BBFace;
typedef struct BBCube {
    char name[BB_NAME];
    float from[3], to[3], origin[3], rotation[3];
    float inflate;
    int node;
    int visible;
    BBFace faces[BB_MAX_FACES]; /* north,east,south,west,up,down */
} BBCube;
typedef struct BBNode {
    char uuid[BB_UUID], name[BB_NAME];
    int parent;
    float origin[3], rotation[3];
    int visible;
} BBNode;
typedef struct BBKey {
    float time, value[3];
    unsigned char interpolation; /* 0 linear, 1 step, 2 catmullrom */
} BBKey;
typedef struct BBTrack {
    int node, channel; /* 0 position,1 rotation,2 scale */
    int first_key, key_count;
} BBTrack;
typedef struct BBClip {
    char name[BB_NAME];
    float length;
    int loop;
    int first_track, track_count;
} BBClip;
typedef struct BBPose {
    float translation[BB_MAX_NODES][3];
    float rotation[BB_MAX_NODES][3];
    float scale[BB_MAX_NODES][3];
} BBPose;
typedef struct BBModel {
    BBTexture textures[BB_MAX_TEXTURES];
    BBCube cubes[BB_MAX_CUBES];
    BBNode nodes[BB_MAX_NODES];
    BBClip clips[BB_MAX_CLIPS];
    BBTrack tracks[BB_MAX_TRACKS];
    BBKey keys[BB_MAX_KEYS];
    int texture_count, cube_count, node_count, clip_count, track_count, key_count;
    char error[180];
} BBModel;
/* Load a .bbmodel JSON file or its in-memory JSON bytes.
 * Caller allocates BBModel (large struct; use heap), and calls bb_free when done.
 * These functions only accept Blockbench 'free' cube models with JSON animations;
 * meshes/expressions are deliberately not supported. */
int bb_load_file(BBModel *m, const char *path);
int bb_load_memory(BBModel *m, const char *data, size_t length);
void bb_free(BBModel *m);
int bb_find_clip(const BBModel *m, const char *name); /* suffix / full name */
int bb_find_node(const BBModel *m, const char *name);
void bb_pose_identity(const BBModel *m, BBPose *out);
void bb_sample_pose(const BBModel *m, int clip_index, float seconds, BBPose *out);
void bb_blend_pose(const BBModel *m, BBPose *inout, const BBPose *other, float weight);
#ifdef __cplusplus
}
#endif
#endif
