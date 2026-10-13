"""Convert Java Anvil terrain to PexCraft's native chunk-delta format.

Source files are only read. Run --inspect before conversion to audit palettes.
"""
import argparse
import collections
import gzip
import json
import pathlib
import struct
import zlib


class NBTReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, count):
        if count < 0 or self.pos + count > len(self.data):
            raise ValueError("Truncated or invalid NBT")
        value = self.data[self.pos:self.pos + count]
        self.pos += count
        return value

    def number(self, fmt):
        return struct.unpack(fmt, self.take(struct.calcsize(fmt)))[0]

    def string(self):
        return self.take(self.number('>H')).decode('utf-8', errors='replace')

    def payload(self, tag):
        formats = {1: '>b', 2: '>h', 3: '>i', 4: '>q', 5: '>f', 6: '>d'}
        if tag in formats:
            return self.number(formats[tag])
        if tag == 7:
            return self.take(self.number('>i'))
        if tag == 8:
            return self.string()
        if tag == 9:
            element = self.number('>B')
            count = self.number('>i')
            if count < 0 or count > 10000000:
                raise ValueError('Invalid NBT list count')
            return [self.payload(element) for _ in range(count)]
        if tag == 10:
            result = {}
            while True:
                child = self.number('>B')
                if child == 0:
                    return result
                name = self.string()
                result[name] = self.payload(child)
        if tag in (11, 12):
            count = self.number('>i')
            if count < 0 or count > 10000000:
                raise ValueError('Invalid NBT array count')
            fmt = '>i' if tag == 11 else '>Q'
            return [self.number(fmt) for _ in range(count)]
        raise ValueError(f'Unsupported NBT tag {tag}')


def read_nbt(data):
    reader = NBTReader(data)
    tag = reader.number('>B')
    reader.string()
    return reader.payload(tag)


def iter_chunks(source):
    for path in sorted((source / 'region').glob('*.mca')):
        data = path.read_bytes()
        for slot in range(1024):
            location = int.from_bytes(data[slot * 4:slot * 4 + 4], 'big')
            offset, sectors = location >> 8, location & 255
            if not offset:
                continue
            start = offset * 4096
            length = struct.unpack_from('>I', data, start)[0]
            if length < 1 or length + 4 > sectors * 4096:
                raise ValueError(f'Bad region record: {path}, slot {slot}')
            compression = data[start + 4]
            payload = data[start + 5:start + 4 + length]
            if compression == 1:
                payload = gzip.decompress(payload)
            elif compression == 2:
                payload = zlib.decompress(payload)
            elif compression != 3:
                raise ValueError(f'Unsupported region compression {compression}')
            root = read_nbt(payload)
            yield root.get('Level', root), root.get('DataVersion', 0)


def sections(chunk):
    return chunk.get('sections', chunk.get('Sections', []))


def palette(section):
    return section.get('block_states', {}).get('palette', section.get('Palette', []))


def decode_section(section, version):
    entries = palette(section)
    if not entries:
        blocks = section.get('Blocks', bytes(4096))
        meta = section.get('Data', bytes(2048))
        add = section.get('Add', bytes(2048))
        if len(blocks) != 4096 or len(meta) != 2048 or len(add) != 2048:
            raise ValueError('Invalid legacy block array')
        return [(blocks[i] | (((add[i // 2] >> ((i & 1) * 4)) & 15) << 8),
                 (meta[i // 2] >> ((i & 1) * 4)) & 15) for i in range(4096)]
    if len(entries) == 1:
        return [entries[0]] * 4096
    words = section.get('block_states', {}).get('data', section.get('BlockStates', []))
    bits = max(4, (len(entries) - 1).bit_length())
    mask = (1 << bits) - 1
    padded = version >= 2529
    per_word = 64 // bits
    result = []
    for i in range(4096):
        if padded:
            index = (words[i // per_word] >> ((i % per_word) * bits)) & mask
        else:
            word, shift = divmod(i * bits, 64)
            index = words[word] >> shift
            if shift + bits > 64:
                index |= words[word + 1] << (64 - shift)
            index &= mask
        if index >= len(entries):
            raise ValueError('Block palette index out of bounds')
        result.append(entries[index])
    return result


def inspect(source, radius):
    level = read_nbt(gzip.decompress((source / 'level.dat').read_bytes()))['Data']
    print('LEVEL', json.dumps({k: level.get(k) for k in
          ('DataVersion', 'Version', 'LevelName', 'SpawnX', 'SpawnY', 'SpawnZ')}, indent=2))
    player = level.get('Player', {})
    print('PLAYER', player.get('Pos'), player.get('Rotation'))
    names = collections.Counter()
    coords, versions, heights = [], collections.Counter(), collections.Counter()
    sample = None
    for chunk, version in iter_chunks(source):
        coords.append((chunk.get('xPos'), chunk.get('zPos')))
        versions[version] += 1
        for sec in sections(chunk):
            heights[sec['Y']] += 1
            for entry in palette(sec):
                names[entry['Name']] += 1
        if sample is None:
            sample = {k: type(v).__name__ for k, v in chunk.items()}
    print('CHUNKS', len(coords), 'VERSIONS', dict(versions), 'SECTION_HEIGHTS', dict(heights))
    print('COORD_BOUNDS', min(x for x, z in coords), max(x for x, z in coords),
          min(z for x, z in coords), max(z for x, z in coords))
    print('CHUNK_FIELDS', sample)
    print('PALETTE_NAMES', json.dumps(dict(sorted(names.items())), indent=2))
    sx, sz = level['SpawnX'] // 16, level['SpawnZ'] // 16
    local = collections.Counter()
    states = {}
    chunks = 0
    for chunk, version in iter_chunks(source):
        if abs(chunk['xPos'] - sx) > radius or abs(chunk['zPos'] - sz) > radius:
            continue
        chunks += 1
        for sec in sections(chunk):
            if not 0 <= sec['Y'] < 16:
                continue
            for entry in decode_section(sec, version):
                name = entry['Name'] if isinstance(entry, dict) else f'legacy:{entry[0]}'
                local[name] += 1
                if isinstance(entry, dict):
                    states.setdefault(name, set()).add(json.dumps(entry.get('Properties', {}), sort_keys=True))
    print('LOCAL_CHUNKS', chunks, 'RADIUS', radius)
    print('LOCAL_BLOCK_COUNTS', json.dumps(dict(local.most_common()), indent=2))
    print('LOCAL_STATES', json.dumps({k: sorted(v) for k, v in sorted(states.items())}, indent=2))


COLORS = ('white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink',
          'gray', 'light_gray', 'cyan', 'purple', 'blue', 'brown', 'green', 'red', 'black')
NUKETOWN_SPAWNS = [(-35, 65, -90, -90), (-35, 65, -93, -90), (-35, 65, -98, -90),
                  (48, 65, -85, 90), (48, 65, -89, 90), (48, 65, -93, 90)]
SIMPLE = {
    'air': 0, 'cave_air': 0, 'void_air': 0, 'stone': 1, 'grass_block': 2,
    'dirt': 3, 'cobblestone': 4, 'bedrock': 7, 'sand': 12, 'gravel': 13,
    'gold_ore': 14, 'iron_ore': 15, 'coal_ore': 16, 'glass': 20,
    'lapis_ore': 21, 'lapis_block': 22, 'note_block': 25, 'cobweb': 30,
    'dead_bush': 32, 'dandelion': 37, 'poppy': 38, 'brown_mushroom': 39,
    'red_mushroom': 40, 'gold_block': 41, 'iron_block': 42, 'bricks': 45,
    'bookshelf': 47, 'mossy_cobblestone': 48, 'obsidian': 49, 'fire': 51,
    'spawner': 52, 'diamond_ore': 56, 'diamond_block': 57, 'crafting_table': 58,
    'iron_bars': 101, 'glass_pane': 102, 'melon': 103, 'netherrack': 87,
    'soul_sand': 88, 'glowstone': 89, 'snow_block': 80, 'ice': 79, 'clay': 82,
    'jukebox': 84, 'nether_bricks': 112, 'nether_brick_fence': 113,
}
APPROX = {
    'granite': (1, 0), 'diorite': (1, 0), 'andesite': (1, 0),
    'polished_andesite': (1, 0), 'smooth_stone': (1, 0), 'coarse_dirt': (3, 0),
    'quartz_block': (35, 0), 'quartz_pillar': (35, 0), 'bone_block': (35, 0),
    'coal_block': (35, 15), 'redstone_block': (35, 14), 'terracotta': (35, 12),
    'white_glazed_terracotta': (35, 0), 'barrier': (0, 0),
    'command_block': (0, 0), 'chain_command_block': (0, 0),
    'repeating_command_block': (0, 0), 'hopper': (118, 0), 'anvil': (42, 0),
    'flower_pot': (0, 0), 'end_rod': (50, 5), 'daylight_detector': (44, 0),
    'tripwire': (0, 0), 'tripwire_hook': (0, 0), 'composter': (5, 0),
    'lectern': (47, 0), 'bell': (0, 0), 'hay_block': (35, 4),
    'cobblestone_wall': (85, 0), 'sandstone_wall': (85, 0),
}


def map_block(entry):
    """Return (legacy ID, metadata, approximate) using 1.2.5 state values."""
    if isinstance(entry, tuple):
        block, meta = entry
        if block <= 124:
            return block, meta, False
        raise ValueError(f'Unmapped legacy block {block}:{meta}')
    name = entry['Name'].removeprefix('minecraft:')
    props = entry.get('Properties', {})
    facing = props.get('facing', 'north')
    wall = {'north': 2, 'south': 3, 'west': 4, 'east': 5}
    stair = {'east': 0, 'west': 1, 'south': 2, 'north': 3}
    wood = next((i for i, w in enumerate(('oak', 'spruce', 'birch', 'jungle'))
                 if name.startswith(w + '_') or name.startswith('stripped_' + w + '_')), 0)
    if name in SIMPLE:
        return SIMPLE[name], 0, False
    if name in APPROX:
        return *APPROX[name], True
    for suffix in ('_wool', '_concrete', '_terracotta', '_carpet', '_bed', '_wall_banner'):
        if name.endswith(suffix):
            color = name[:-len(suffix)]
            if color not in COLORS:
                break
            if suffix == '_carpet':
                return 78, 0, True  # thin layer: avoid blocking doorways with full cubes
            if suffix == '_wall_banner':
                return 68, wall[facing], True
            if suffix == '_bed':
                meta = {'south': 0, 'west': 1, 'north': 2, 'east': 3}[facing]
                meta |= 8 if props.get('part') == 'head' else 0
                return 26, meta, color != 'red'
            return 35, COLORS.index(color), suffix != '_wool'
    if name.endswith('_planks'):
        return 5, wood, name.startswith('dark_oak')
    if name.endswith(('_log', '_wood')):
        return 17, wood, name.startswith('stripped_') or name.endswith('_wood')
    if name.endswith('_leaves'):
        return 18, wood | 4, False  # persistent leaves, no decay after import
    if name.endswith('_stairs'):
        ids = {'cobblestone': 67, 'brick': 108, 'stone_brick': 109, 'nether_brick': 114}
        material = name[:-7]
        approx = material not in ids and material != 'oak'
        block = ids.get(material, 109 if material in ('quartz', 'sandstone', 'smooth_sandstone') else 53)
        return block, stair[facing] | (4 if props.get('half') == 'top' else 0), approx
    if name.endswith('_slab'):
        variants = {'stone': 0, 'smooth_stone': 0, 'sandstone': 1, 'smooth_sandstone': 1,
                    'oak': 2, 'spruce': 2, 'birch': 2, 'cobblestone': 3, 'brick': 4,
                    'stone_brick': 5, 'nether_brick': 6, 'quartz': 0}
        material = name[:-5]
        if material not in variants:
            raise ValueError(f'Unmapped slab {name}')
        kind = props.get('type', 'bottom')
        return (43 if kind == 'double' else 44), variants[material] | (8 if kind == 'top' else 0), material in ('quartz', 'spruce', 'birch')
    if name in ('sandstone', 'chiseled_sandstone', 'cut_sandstone', 'smooth_sandstone'):
        return 24, {'sandstone': 0, 'chiseled_sandstone': 1, 'cut_sandstone': 2, 'smooth_sandstone': 2}[name], False
    if name == 'stone_bricks':
        return 98, 0, False
    if name.endswith('_fence'):
        return 85, 0, name != 'oak_fence'
    if name.endswith('_fence_gate'):
        meta = {'south': 0, 'west': 1, 'north': 2, 'east': 3}[facing]
        return 107, meta | (4 if props.get('open') == 'true' else 0), name != 'oak_fence_gate'
    if name.endswith('_trapdoor'):
        meta = {'south': 0, 'north': 1, 'east': 2, 'west': 3}[facing]
        meta |= 4 if props.get('open') == 'true' else 0
        meta |= 8 if props.get('half') == 'top' else 0
        return 96, meta, name != 'oak_trapdoor'
    if name.endswith('_door'):
        if props.get('half') == 'upper':
            meta = 8 | (1 if props.get('hinge') == 'right' else 0)
        else:
            meta = {'east': 0, 'south': 1, 'west': 2, 'north': 3}[facing]
            meta |= 4 if props.get('open') == 'true' else 0
        return (71 if name == 'iron_door' else 64), meta, name not in ('oak_door', 'iron_door')
    if name.endswith('_wall_sign'):
        return 68, wall[facing], not name.startswith('oak')
    if name.endswith('_sign'):
        return 63, int(props.get('rotation', 0)), not name.startswith('oak')
    if name.endswith('_stained_glass_pane'):
        return 102, 0, True
    if name.startswith('potted_'):
        return 0, 0, True
    if name in ('grass', 'fern'):
        return 31, 2 if name == 'fern' else 1, False
    if name in ('water', 'lava'):
        return (9 if name == 'water' else 11), int(props.get('level', 0)), False
    if name in ('torch', 'wall_torch', 'redstone_torch', 'redstone_wall_torch'):
        meta = {'east': 1, 'west': 2, 'south': 3, 'north': 4}[facing] if 'wall' in name else 5
        block = (76 if props.get('lit', 'true') == 'true' else 75) if name.startswith('redstone') else 50
        return block, meta, False
    if name in ('chest', 'trapped_chest', 'barrel', 'furnace', 'smoker'):
        block = 54 if name in ('chest', 'trapped_chest', 'barrel') else (62 if props.get('lit') == 'true' else 61)
        return block, wall.get(facing, 2), name in ('trapped_chest', 'barrel', 'smoker')
    if name == 'ladder':
        return 65, wall[facing], False
    if name == 'rail':
        shapes = ('north_south', 'east_west', 'ascending_east', 'ascending_west',
                  'ascending_north', 'ascending_south', 'south_east', 'south_west', 'north_west', 'north_east')
        return 66, shapes.index(props.get('shape', 'north_south')), False
    if name in ('wheat', 'carrots', 'potatoes', 'beetroots', 'melon_stem'):
        return (105 if name == 'melon_stem' else 59), min(7, int(props.get('age', 7))), name not in ('wheat', 'melon_stem')
    if name == 'farmland':
        return 60, int(props.get('moisture', 0)), False
    if name == 'cactus':
        return 81, int(props.get('age', 0)), False
    if name == 'snow':
        return 78, int(props.get('layers', 1)) - 1, False
    if name in ('oak_pressure_plate', 'stone_pressure_plate'):
        return (72 if name.startswith('oak') else 70), int(props.get('powered') == 'true'), False
    if name.endswith('_button') or name == 'lever':
        face = props.get('face', 'wall')
        meta = {'east': 1, 'west': 2, 'south': 3, 'north': 4}[facing]
        if face == 'floor':
            meta = 5 if facing in ('east', 'west') else 6
        elif face == 'ceiling':
            meta = 0 if facing in ('east', 'west') else 7
        return (69 if name == 'lever' else 77), meta | (8 if props.get('powered') == 'true' else 0), name not in ('stone_button', 'lever')
    if name in ('sticky_piston', 'piston', 'piston_head'):
        meta = {'down': 0, 'up': 1, **wall}[facing]
        meta |= 8 if props.get('extended') == 'true' or props.get('type') == 'sticky' else 0
        return {'sticky_piston': 29, 'piston': 33, 'piston_head': 34}[name], meta, False
    if name == 'redstone_wire':
        return 55, int(props.get('power', 0)), False
    if name == 'redstone_ore':
        return (74 if props.get('lit') == 'true' else 73), 0, False
    if name in ('repeater', 'comparator'):
        meta = {'north': 0, 'east': 1, 'south': 2, 'west': 3}[facing]
        meta |= (int(props.get('delay', 1)) - 1) << 2
        return (94 if props.get('powered') == 'true' else 93), meta, name == 'comparator'
    if name == 'cauldron':
        return 118, 0, False
    raise ValueError(f'Unmapped block {name}, properties {props}')


def base36(value):
    sign = '-' if value < 0 else ''
    value = abs(value)
    result = ''
    while value:
        value, remainder = divmod(value, 36)
        result = '0123456789abcdefghijklmnopqrstuvwxyz'[remainder] + result
    return sign + (result or '0')


def convert(source, output, radius):
    if output.exists():
        raise ValueError(f'Refusing to overwrite existing output: {output}')
    if not 1 <= radius <= 32:
        raise ValueError('Radius must be in 1..32')
    level = read_nbt(gzip.decompress((source / 'level.dat').read_bytes()))['Data']
    origin = (level['SpawnX'] // 16, level['SpawnZ'] // 16)
    selected = {}
    approximations = collections.Counter()
    block_counts = collections.Counter()
    state_cache = {}
    skipped_vertical = 0
    for chunk, version in iter_chunks(source):
        cx, cz = chunk['xPos'], chunk['zPos']
        if abs(cx - origin[0]) > radius or abs(cz - origin[1]) > radius:
            continue
        blocks, metadata = bytearray(65536), bytearray(65536)
        for sec in sections(chunk):
            sy = sec['Y']
            if not 0 <= sy < 16:
                skipped_vertical += 1
                continue
            offset = sy * 4096
            for i, entry in enumerate(decode_section(sec, version)):
                key = json.dumps(entry, sort_keys=True) if isinstance(entry, dict) else entry
                if key not in state_cache:
                    state_cache[key] = map_block(entry)
                block, meta, approx = state_cache[key]
                assert 0 <= block <= 124 and 0 <= meta <= 15
                blocks[offset + i], metadata[offset + i] = block, meta
                name = entry['Name'] if isinstance(entry, dict) else f'legacy:{entry[0]}'
                block_counts[name] += 1
                if approx:
                    approximations[name] += 1
        selected[(cx - origin[0], cz - origin[1])] = (blocks, metadata)
    expected = (radius * 2 + 1) ** 2
    if len(selected) != expected:
        raise ValueError(f'Incomplete map: {len(selected)} of {expected} chunks exist')
    sx, sz = level['SpawnX'] - origin[0] * 16, level['SpawnZ'] - origin[1] * 16

    def block_at(x, y, z):
        if not 0 <= y < 256:
            return 0
        pair = selected.get((x // 16, z // 16))
        return pair[0][y * 256 + (z % 16) * 16 + x % 16] if pair else 0

    # Prefer the original spawn elevation, but do not spawn inside a converted
    # decorative block. Player coordinates in PexCraft are eye coordinates.
    non_ground = {0, 8, 9, 10, 11, 30, 31, 32, 37, 38, 39, 40, 50, 51, 55, 59, 63, 68, 75, 76, 78}
    candidates = []
    for z in range(sz - 8, sz + 9):
        for x in range(sx - 8, sx + 9):
            for y in range(max(1, level['SpawnY'] - 8), min(254, level['SpawnY'] + 9)):
                if block_at(x, y - 1, z) not in non_ground and block_at(x, y, z) == 0 and block_at(x, y + 1, z) == 0:
                    candidates.append(((x - sx)**2 + (z - sz)**2 + abs(y - level['SpawnY'])*4, x, y, z))
    if not candidates:
        raise ValueError('No safe spawn in converted map')
    _, sx, sy, sz = min(candidates)
    spawn = (sx + 0.5, sy + 1.62, sz + 0.5, 134.55)
    output.mkdir(parents=True)
    (output / 'chunks').mkdir()
    files = []
    for (cx, cz), (blocks, meta) in sorted(selected.items()):
        bc, mc = zlib.compress(blocks, 9), zlib.compress(meta, 9)
        payload = b'PXCDLTZ1' + struct.pack('<iiiII', cx, cz, 65536, len(bc), len(mc)) + bc + mc
        filename = f'c.{base36(cx)}.{base36(cz)}.dat'
        (output / 'chunks' / filename).write_bytes(payload)
        # Independently read back the on-disk container before declaring success.
        written = (output / 'chunks' / filename).read_bytes()
        assert struct.unpack_from('<iii', written, 8) == (cx, cz, 65536)
        assert zlib.decompress(written[28:28 + len(bc)]) == blocks
        assert zlib.decompress(written[28 + len(bc):]) == meta
        files.append(filename)
    (output / 'breach_map.txt').write_text(' '.join(map(str, spawn)) + '\n', encoding='ascii')
    (output / 'breach_spawns.txt').write_text(
        '\n'.join(' '.join(map(str, point)) for point in NUKETOWN_SPAWNS) + '\n', encoding='ascii')
    (output / 'chunks.txt').write_text('\n'.join(files) + '\n', encoding='ascii')
    report = {
        'map': 'CoD Nuketown by Mewok', 'source_version': level.get('Version'),
        'source_spawn': [level['SpawnX'], level['SpawnY'], level['SpawnZ']],
        'coordinate_offset': [origin[0] * 16, 0, origin[1] * 16],
        'size_blocks': [(2 * radius + 1) * 16, 256, (2 * radius + 1) * 16],
        'chunks': len(files), 'spawn_eye': list(spawn),
        'player_and_bot_spawn_points_feet': NUKETOWN_SPAWNS,
        'skipped_sections_outside_y_0_255': skipped_vertical,
        'approximated_or_removed_blocks': dict(sorted(approximations.items())),
        'source_block_counts': dict(sorted(block_counts.items())),
        'notes': ['Terrain only: original entities, inventories, command logic and sign text are not imported.',
                  'Modern decorative materials use legacy substitutes; carpets use thin snow layers.',
                  'Barrier and command blocks are removed; the original source is unchanged.'],
    }
    (output / 'conversion_report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: report[k] for k in ('map', 'size_blocks', 'chunks', 'spawn_eye')}, indent=2))
    print('Native chunk payloads verified:', len(files))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=pathlib.Path)
    parser.add_argument('--inspect', action='store_true')
    parser.add_argument('--radius', type=int, default=12)
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    if args.inspect:
        inspect(args.source, args.radius)
    elif args.output:
        convert(args.source, args.output, args.radius)
    else:
        parser.error('Use --inspect or --output')
