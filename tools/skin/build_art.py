import json,math,os,re,shutil,sys
from PIL import Image
import numpy as np
A='/mnt/user-data/attachments/101ec232-c82a-59a6-be8e-3dc0857e7bf4/'
SRC=os.environ.get('SRC') or "w/ALPHA-1.1.0/portable/L'Cronx - VST - ALPHA/Plugin Skins/"
DST=sys.argv[1]
if os.path.exists(DST): shutil.rmtree(DST)
shutil.copytree(SRC,DST); DST+='/'
MAG=(192,29,124); MAGHEX='ffc01d7c'
NF=64
# parts (user's own images)
body=Image.open(A+'a5cfb3af-image.png').convert('RGBA')      # 344 body
ptr=Image.open(A+'9ddf62c2-image.png').convert('RGBA')
tick=Image.open(A+'913fa0f4-image.png').convert('RGBA')      # 652x604 (4x)
from mkshadow import make_shadow
shad=make_shadow()   # soft cast shadow of the knob silhouette (4x, 95x103 art px)
bg=Image.open(A+'fc81d60e-image.png').convert('RGB'); assert bg.size==(1280,628)
bg.save(DST+'sh_bg_0.png')
knobfull=Image.alpha_composite(body,ptr)
# ring scale per knob type: ring outer diameter 143 art px * s
RING={'52':146,'54':220,'56':228,'58':236}
OLD={'52':114,'54':118,'56':122,'58':126}
def strip(typ):
    ring=RING[typ]; s=ring/143.0; F=int(math.ceil(ring+2)); F+=F%2
    kb=knobfull.resize((round(86*s*4)//4*4 if False else round(86*s),)*2,Image.LANCZOS) if False else None
    tk=tick.resize((round(652*s/4),round(604*s/4)),Image.LANCZOS)
    from PIL import ImageFilter
    sh=shad.resize((round(95*s),round(103*s)),Image.LANCZOS)
    kdia=round(86*s)
    out=Image.new('RGBA',(F,F*NF),(0,0,0,0))
    for i in range(NF):
        fr=Image.new('RGBA',(F,F),(0,0,0,0))
        fr.alpha_composite(tk,(round(F/2-81*s),round(F/2-80.8*s)))
        fr.alpha_composite(sh,(round(F/2-kdia/2),round(F/2-kdia/2)))
        k=knobfull.rotate(-(-135+270*i/(NF-1)),resample=Image.BICUBIC).resize((kdia,kdia),Image.LANCZOS)
        fr.alpha_composite(k,(round(F/2-kdia/2),round(F/2-kdia/2)))
        out.alpha_composite(fr,(0,i*F))
    out.save(DST+f'sh_knob_r{typ}.png',optimize=True)
    return F
FS={t:strip(t) for t in RING}
print(FS)
# panels & tiles -> translucent
def conv(path,on=False):
    a=np.asarray(Image.open(path).convert('RGB')).astype(int); L=a[...,0]
    out=np.zeros(a.shape[:2]+(4,),np.uint8)
    if on:
        out[...,:3]=MAG; out[...,3]=60
    else:
        out[...,:3]=0
        out[L==58,3]=60; out[L==28,3]=6; out[L==14,3]=0
        out[(L!=58)&(L!=28)&(L!=14),3]=0
    Image.fromarray(out,'RGBA').save(path)
for f in os.listdir(DST):
    if f.startswith('sh_mode_') : conv(DST+f)
    elif f.startswith('sh_tile_'): conv(DST+f,on=f.endswith('_on.png'))
# TUI.json
d=json.load(open(DST+'TUI.json'))
defs=d['pageData']['componentDefinitions']['localComponentDefinitions']
dmap={x['key']:x['value'] for x in defs}
def recol(o):
    if isinstance(o,dict): return {k:recol(v) for k,v in o.items()}
    if isinstance(o,list): return [recol(v) for v in o]
    if o=='ffffffff': return 'ff000000'
    if o=='ff1ab463': return MAGHEX
    return o
for x in defs: x['value']=recol(x['value'])
dmap={x['key']:x['value'] for x in defs}
geo={}
for typ in RING:
    key=f'shKnob{typ}_ls1.7_bw150'; v=dmap[key]
    F=FS[typ]; f=OLD[typ]
    W=max(150,F); dx=(W-150)//2
    oldH=None
    for s in v['componentsData']:
        b=s['bounds']; x,y,w,h=map(int,b['bounds'].split()); n=s['componentData']['name']
        if n=='Focus': oldH=h
    dy=max(0,(F-f)//2)
    Hn=oldH+dy
    for s in v['componentsData']:
        b=s['bounds']; x,y,w,h=map(int,b['bounds'].split()); n=s['componentData']['name']
        if n=='Focus': b['bounds']=f'0 0 {W} {Hn}'
        elif n=='Knob':
            b['bounds']=f'{(W-F)//2} 0 {F} {F}'; s['componentData']['data']['numFrames']=NF
        else: b['bounds']=f'{x+dx} {y+dy} {w} {h}'
    geo[key]=(dx,dy,W,Hn)
comps=dmap['ALPHA|ALPHA']['componentsData']
for c in comps:
    t=c['componentData']['type']
    if t in geo:
        dx,dy,W,Hn=geo[t]; x,y,w,h=map(int,c['bounds']['bounds'].split())
        c['bounds']['bounds']=f'{x-dx} {y-dy} {W} {Hn}'
json.dump(d,open(DST+'TUI.json','w'),indent=1)
print('ok',geo)
