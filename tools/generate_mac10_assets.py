#!/usr/bin/env python3
"""Create static native PexCraft MAC-10 viewmodel data from Blockbench .bbmodel.

Texture 3 (the character arm material) is deliberately NOT embedded: it is
resolved at runtime using the active player's native skin texture.
"""
import base64, io, json, math, sys, zlib
from pathlib import Path
from PIL import Image

src = Path(sys.argv[1]); dst=Path(sys.argv[2]); model=json.loads(src.read_text())
groups = model['groups']; gid = {g['uuid']: i for i,g in enumerate(groups)}
by_el={e['uuid']:e for e in model['elements']}
faces={i:[] for i in range(len(groups))}; children={i:[] for i in range(len(groups))}
parents={}

def rotate(v, deg):
 x,y,z=v; a,b,c=[math.radians(t) for t in deg]
 y,z=y*math.cos(a)-z*math.sin(a), y*math.sin(a)+z*math.cos(a)
 x,z=x*math.cos(b)+z*math.sin(b), -x*math.sin(b)+z*math.cos(b)
 x,y=x*math.cos(c)-y*math.sin(c), x*math.sin(c)+y*math.cos(c)
 return [x,y,z]

cube_order=('north','east','south','west','up','down')
def gen_el(e,i):
 inf=e.get('inflate',0); f=[v-inf for v in e['from']];t=[v+inf for v in e['to']]
 x0,y0,z0=f;x1,y1,z1=t
 corners={
 'north':[(x1,y1,z0),(x0,y1,z0),(x0,y0,z0),(x1,y0,z0)],
 'south':[(x0,y1,z1),(x1,y1,z1),(x1,y0,z1),(x0,y0,z1)],
 'east':[(x1,y1,z1),(x1,y1,z0),(x1,y0,z0),(x1,y0,z1)],
 'west':[(x0,y1,z0),(x0,y1,z1),(x0,y0,z1),(x0,y0,z0)],
 'up':[(x0,y1,z0),(x1,y1,z0),(x1,y1,z1),(x0,y1,z1)],
 'down':[(x0,y0,z1),(x1,y0,z1),(x1,y0,z0),(x0,y0,z0)]}
 eo=e.get('origin',[0,0,0]); go=groups[i]['origin']; er=e.get('rotation',[0,0,0]);
 for name in cube_order:
  fc=e.get('faces',{}).get(name,{}); ti=fc.get('texture');
  if ti is None or ti is False: continue
  ti=int(ti); uv=fc.get('uv',[0,0,1,1]);uw=model['textures'][ti].get('uv_width') or model['resolution']['width'];uh=model['textures'][ti].get('uv_height') or model['resolution']['height'];
  [u1,v1,u2,v2]=uv; uvcs=((u1,v1),(u2,v1),(u2,v2),(u1,v2)); k=int(fc.get('rotation',0)/90+4)%4
  xyz=[];uvs=[]
  for j,p in enumerate(corners[name]):
   r=rotate([p[a]-eo[a] for a in range(3)],er)
   xyz.extend([(r[a]+eo[a]-go[a])/16 for a in range(3)])
   q=uvcs[(j-k+4)%4];uvs.extend([q[0]/uw,q[1]/uh])
  # triangles: 0,3,2; 0,2,1, preserve Three's winding & double-sided draw
  faces[i].append((ti,xyz,uvs,e['name']))

def traverse(entry,parent=None):
 if isinstance(entry,str):
  if parent is not None and entry in by_el: gen_el(by_el[entry],parent)
  return
 i=gid[entry['uuid']]
 if parent is not None:children[parent].append(i)
 else: roots.append(i)
 parents[i]=parent
 for child in entry.get('children',[]):traverse(child,i)
roots=[]
for node in model['outliner']:traverse(node)
clips=['idle_boltout','idle_empty','ads','auto_fire','fire','reload','reload_empty','inspect','draw']
keys=[];tracks=[];lengths=[]
for ai, name in enumerate(clips):
 anim=next((a for a in model['animations'] if a['name'].endswith('.'+name)),None)
 lengths.append(anim.get('length',0.0) if anim else 0)
 if not anim: continue
 for channel in ('position','rotation','scale'):
  for animator in anim.get('animators',{}).values():
   gn=animator.get('name'); bi=next((i for i,g in enumerate(groups) if g['name']==gn),None)
   if bi is None:continue
   arr=sorted((k for k in animator.get('keyframes',[]) if k.get('channel')==channel),key=lambda k:k['time'])
   if not arr: continue
   at=len(keys)
   for k in arr:
    p=(k.get('data_points') or [{}])[0]
    keys.append((k['time'],[float(p.get(a,0)) for a in 'xyz']))
   tracks.append((ai,bi,('position','rotation','scale').index(channel),at,len(arr)))

def fl(n):
 n=float(n)
 if not math.isfinite(n): n=0
 return f'{n:.7g}f' if abs(n)>1e-9 and '.' in f'{n:.7g}' else f'{n:.7g}.0f'

def arr(v):return '{'+','.join(fl(x) for x in v)+'}'

out=['/* Generated from '+src.name+'; do not modify by hand. */', '#ifndef PEX_MAC10_MODEL_DATA_H', '#define PEX_MAC10_MODEL_DATA_H',
 'typedef struct { float xyz[12]; float uv[8]; unsigned char tex; } Mac10Face;',
 'typedef struct { float origin[3], rotation[3]; signed char parent; unsigned short first_face, face_count; } Mac10Bone;',
 'typedef struct { float time, xyz[3]; } Mac10Key;',
 'typedef struct { unsigned char clip, bone, channel; unsigned short first, count; } Mac10Track;',
 'enum { MAC_IDLE=0, MAC_EMPTY, MAC_ADS, MAC_AUTO, MAC_FIRE, MAC_RELOAD, MAC_RELOAD_EMPTY, MAC_INSPECT, MAC_DRAW, MAC_ANIM_COUNT };',
 'static const float mac10_clip_lengths[MAC_ANIM_COUNT] = {'+','.join(fl(x) for x in lengths)+'};']
flat=[];bonedata=[]
for i,g in enumerate(groups):
 first=len(flat); flat.extend(faces[i]); parent=parents.get(i)
 bonedata.append((g['origin'],g.get('rotation',[0,0,0]), -1 if parent is None else parent,first,len(flat)-first))
out.append('static const Mac10Bone mac10_bones[] = {')
for origin,rot,parent,first,count in bonedata:out.append(' {'+arr(origin)+','+arr(rot)+f',{parent},{first},{count}'+ '},')
out.append('};')
out.append('static const Mac10Face mac10_faces[] = {')
for ti,xyz,uv,name in flat:out.append(' {'+arr(xyz)+','+arr(uv)+f',{ti}'+'}, /* '+name+' */')
out.append('};')
out.append('static const Mac10Key mac10_keys[] = {')
for t,xyz in keys:out.append(' {'+fl(t)+','+arr(xyz)+'},')
out.append('};')
out.append('static const Mac10Track mac10_tracks[] = {')
for a,b,c,f,n in tracks:out.append(f' {{{a},{b},{c},{f},{n}}},')
out.append('};')
out.append('static const unsigned char mac10_skin_texture_index = 3;')
for j,t in enumerate(model['textures'][:3]):
 # .source is a base64 data url
 d=t.get('source') or t.get('layers',[{}])[0].get('data_url','')
 if not d.startswith('data:image/'):raise ValueError(t['name'])
 image=Image.open(io.BytesIO(base64.b64decode(d.split(',',1)[1]))).convert('RGBA'); w,h=image.size
 raw=image.tobytes();compressed=zlib.compress(raw,9)
 out.append(f'static const unsigned char mac10_texture_{j}_z[] = {{')
 for k in range(0,len(compressed),24):out.append(' '+','.join(str(v) for v in compressed[k:k+24])+',')
 out.append('};')
 out.append(f'#define MAC10_TEX_{j}_W {w}')
 out.append(f'#define MAC10_TEX_{j}_H {h}')
out.append('#endif /* PEX_MAC10_MODEL_DATA_H */')
dst.write_text('\n'.join(out)+'\n')
print('generated',dst.stat().st_size,'bytes',len(flat),'faces',len(tracks),'tracks',len(keys),'keys')
