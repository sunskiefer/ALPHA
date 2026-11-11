import json,re
from PIL import Image,ImageDraw,ImageFont,ImageFilter
K='newskin/';Q=4
MAG=(192,29,124,255);BLK=(12,12,16,255);WH=(240,240,248,255);DK=(48,48,58,255);GR=(150,150,164,255)
FB='f/TitilliumWeb-Bold.ttf'
def shadow(im,dx,dy,b,op):
    m=Image.new('L',im.size,0);m.paste(im.getchannel('A'),(dx,dy));m=m.filter(ImageFilter.GaussianBlur(b)).point(lambda v:int(v*op))
    s=Image.new('RGBA',im.size,(18,18,26,0));s.putalpha(m);return Image.alpha_composite(s,im)
# ---- ON/OFF selector (picture 4), bigger: W x 72
def selector(on,W,H=60,fs=26):
    w,h=W*Q,H*Q;im=Image.new('RGBA',(w,h),(0,0,0,0));d=ImageDraw.Draw(im);p=6*Q
    d.rounded_rectangle((0,0,w-1,h-1),14*Q,fill=BLK)
    L=(p,p,w//2-2*Q,h-p);R=(w//2+2*Q,p,w-p,h-p)
    d.rounded_rectangle(L,9*Q,fill=MAG if on else DK);d.rounded_rectangle(R,9*Q,fill=DK if on else WH)
    f=ImageFont.truetype(FB,fs*Q)
    d.text(((L[0]+L[2])/2,h/2),'ON',font=f,fill=WH if on else GR,anchor='mm')
    d.text(((R[0]+R[2])/2,h/2),'OFF',font=f,fill=GR if on else BLK,anchor='mm')
    return shadow(im,0,0,1,0).resize((W,H),Image.LANCZOS)
# ---- sliding block cells (picture 8): black track cell, selected = magenta block with grip lines
def cell(on,W,H):
    w,h=W*Q,H*Q;im=Image.new('RGBA',(w,h),(0,0,0,0));d=ImageDraw.Draw(im)
    r=int(.26*h);bd=int(3.2*Q)
    d.rounded_rectangle((0,0,w-1,h-1),r,fill=BLK)
    d.rounded_rectangle((bd,bd,w-1-bd,h-1-bd),r-bd,fill=MAG if on else WH)   # white cell, magenta only when selected
    return im.resize((W,H),Image.LANCZOS)
t=json.load(open(K+'TUI.json'))
L=t['pageData']['componentDefinitions']['localComponentDefinitions']
EXTRA=38;SELH=60
for x in L:
    k=x['key']
    if k.startswith('shToggle'):
        bw=int(re.search(r'bw(\d+)',k).group(1))
        for ph,on in (('on',True),('off',False)):selector(on,bw).save(K+f'sh_sel_{bw}_{ph}.png')
        for c in x['value']['componentsData']:
            nm=c['componentData']['name'];b=c['bounds']
            if nm=='Focus':b['bounds']=f'0 0 {bw} {86+EXTRA}'
            elif nm=='Button':
                b['bounds']=f'0 0 {bw} {SELH}'
                d=c['componentData']['data'];d['onImage']=f'sh_sel_{bw}_on.png';d['offImage']=f'sh_sel_{bw}_off.png'
            elif nm=='Name':b['bounds']=f'0 {SELH+16} {bw} 48'
    m=re.match(r'shRow_(\d+)x(\d+)_',k)
    if m and (m.group(1),m.group(2)) in {('124','52'),('201','52'),('243','60'),('177','70'),('300','90')}:
        W,H=int(m.group(1)),int(m.group(2))
        for ph,on in (('on',True),('off',False)):cell(on,W,H).save(K+f'sh_tile_{W}x{H}_{ph}.png')
        s=json.dumps(x)
        s=s.replace('"ffc01d7c"','"ff0e0e0e"')   # black text: readable on the white cell and on the magenta one
        x['value']=json.loads(s)['value']
for c in L[21]['value']['componentsData']:
    if c['componentData']['type'].startswith('shToggle') and c['componentData']['name'].strip().upper() in ('ON','OFF'):
        c['componentData']['name']=''   # the switch already says ON / OFF
GAP=28
for x in L:
    if x['key'].startswith('shKnob'):
        x['value']['componentsData']=[c for c in x['value']['componentsData'] if c['componentData']['name']!='Value']   # no number under the knobs
        for c in x['value']['componentsData']:
            if c['componentData']['name'] in('Name','Value'):
                a,b,w,h=c['bounds']['bounds'].split();c['bounds']['bounds']=f'{a} {int(b)+GAP} {w} {h}'
cs=L[21]['value']['componentsData']
for c in cs:
    if c['componentData']['type'].startswith('shKnob'):
        a,b,w,h=c['bounds']['bounds'].split();c['bounds']['bounds']=f'{a} {b} {w} {int(h)+GAP}'
for c in cs:
    if c['componentData']['type'].startswith('shToggle'):
        xx,y,w,h=c['bounds']['bounds'].split();c['bounds']['bounds']=f'{xx} {y} {w} {int(h)+EXTRA}'

import copy
CR="shCredit_920x60_34"
nm=[c for c in L[5]['value']['componentsData'] if c['componentData']['name']=='Name'][0]
cn=copy.deepcopy(nm);cn['bounds']['bounds']='0 0 920 60'
cn['componentData']['data']['textStyle']['font']['height']=34.0
cn['componentData']['data']['textStyle']['font']['style']='Bold'
cn['componentData']['data']['textStyle']['colour']='ff0e0e0e'
dv={k:v for k,v in L[3]['value'].items() if k!='componentsData'}
L.insert(len(L)-1,{"key":CR,"value":dict(dv,componentsData=[cn])})
cs=[x for x in L if x['key']=='ALPHA|ALPHA'][0]['value']['componentsData']
inst=copy.deepcopy([c for c in cs if c['componentData']['name']=='upa 1'][0])
inst['componentData']['name']="L'Cronx & DreamX Wilson";inst['componentData']['type']=CR
inst['handle remapping']['map']=[];inst['bounds']['acceptsHWFocus']='No'
inst['bounds']['bounds']='330 450 920 60'
cs.append(inst)
json.dump(t,open(K+'TUI.json','w'),indent=1)
