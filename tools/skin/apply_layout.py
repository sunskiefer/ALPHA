import json,copy,math
from PIL import Image
K='newskin/';A='/mnt/user-data/attachments/101ec232-c82a-59a6-be8e-3dc0857e7bf4/'
EXTRA=0   # knob name labels exactly at the exported editor positions
MP={int(k):v for k,v in json.load(open('idmap.json')).items()};INV={v:k for k,v in MP.items()}
j=json.load(open('layout_export.json'));P={p['id']:p for p in j['parts']}
t=json.load(open(K+'TUI.json'))
LL=t['pageData']['componentDefinitions']['localComponentDefinitions']
D={x['key']:x for x in LL}
cs=D['ALPHA|ALPHA']['value']['componentsData']
def bb(c):return list(map(float,c['bounds']['bounds'].split()))
def sb(c,v):c['bounds']['bounds']=' '.join(str(int(round(a))) for a in v)
def clone(i,c):
    k=c['componentData']['type'];nk=f'{k}_i{i}'
    if nk not in D:
        e={'key':nk,'value':copy.deepcopy(D[k]['value'])};LL.insert(len(LL)-1,e);D[nk]=e
    c['componentData']['type']=nk;return D[nk]['value']['componentsData']
def comp(cl,n):return [x for x in cl if x['componentData']['name']==n][0]
# shifted-shadow knob strip for 144
def strip144(dx,dy):
    from mkshadow import make_shadow
    import numpy as np
    body=Image.open(A+'a5cfb3af-image.png').convert('RGBA');ptr=Image.open(A+'9ddf62c2-image.png').convert('RGBA');tick=Image.open(A+'913fa0f4-image.png').convert('RGBA')
    kf=Image.alpha_composite(body,ptr);s=228/143.0;F=int(math.ceil(230));F+=F%2
    tk=tick.resize((round(652*s/4),round(604*s/4)),Image.LANCZOS);sh=make_shadow().resize((round(95*s),round(103*s)),Image.LANCZOS);kd=round(86*s)
    out=Image.new('RGBA',(F,F*64),(0,0,0,0))
    for i in range(64):
        fr=Image.new('RGBA',(F,F),(0,0,0,0))
        fr.alpha_composite(tk,(round(F/2-81*s),round(F/2-80.8*s)))
        fr.alpha_composite(sh,(round(F/2-kd/2+dx),round(F/2-kd/2+dy)))
        k=kf.rotate(-(-135+270*i/63),resample=Image.BICUBIC).resize((kd,kd),Image.LANCZOS)
        fr.alpha_composite(k,(round(F/2-kd/2),round(F/2-kd/2)))
        out.alpha_composite(fr,(0,i*F))
    out.save(K+'sh_knob_r56_i144.png',optimize=True)
for i,c in enumerate(cs):
    ty=c['componentData']['type'];ix,iy,iw,ih=bb(c)
    oi=INV.get(i)
    if ty.startswith('shKnob'):
        p=P.get(f'{oi}:name') if oi is not None else None
        cl=clone(i,c);n=comp(cl,'Name')
        if not p:
            a_,b_,w_,h_=bb(n);sb(n,(a_,b_+EXTRA-28,w_,h_));p=dict(x=ix+a_,y=iy+b_+EXTRA-28-EXTRA,w=w_,h=h_)
        else:sb(n,(p['x']-ix,p['y']+EXTRA-iy,p['w'],p['h']))
        # keep Focus/instance tall enough
        need=p['y']+EXTRA-iy+p['h']
        if need>ih:ih=need
        sb(c,(ix,iy,iw,ih));sb(comp(cl,'Focus'),(0,0,iw,ih))
        if oi==144:
            sh=P['144:shadow'];strip144(sh['x']-731.4,sh['y']-183.4)
            comp(cl,'Knob')['componentData']['data']['filmStrip']='sh_knob_r56_i144.png'
    elif ty.startswith('shToggle'):
        b=P.get(f'{oi}:bg');t0=P.get(f'{oi}:t0')
        if not b:continue
        nx,ny=b['x'],b['y']
        cl=clone(i,c);bt=comp(cl,'Button');nm=comp(cl,'Name');bw,bh=bb(bt)[2:]
        if t0:sb(nm,(t0['x']-nx,t0['y']-ny+0,t0['w'],t0['h']))
        ih2=max(ih,bb(nm)[1]+bb(nm)[3]);sb(c,(nx,ny,iw,ih2));sb(comp(cl,'Focus'),(0,0,iw,ih2))
    elif ty.startswith('shReadout') and oi is not None and f'{oi}:t0' in P:
        p=P[f'{oi}:t0'];lab=[x for x in D[ty]['value']['componentsData'] if x['componentData']['type']=='Label'][0];lx,ly,lw,lh=bb(lab)
        dx=p['x']-(ix+lx)
        if abs(dx)>.5:
            cl=clone(i,c);l=[x for x in cl if x['componentData']['type']=='Label'][0];sb(l,(lx+dx,ly,lw,lh))
json.dump(t,open(K+'TUI.json','w'),indent=1)
print('done')
