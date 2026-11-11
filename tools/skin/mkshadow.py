from PIL import Image,ImageFilter,ImageChops
import numpy as np
A='/mnt/user-data/attachments/101ec232-c82a-59a6-be8e-3dc0857e7bf4/'
def make_shadow():
    """4x canvas, knob top-left at (0,0); returns RGBA 380x412 (=95x103 art px at 4x)"""
    body=Image.open(A+'a5cfb3af-image.png').convert('RGBA')
    sil=body.getchannel('A')
    W,H=380,412
    def layer(dx,dy,blur,op):
        m=Image.new('L',(W,H),0);m.paste(sil,(dx,dy))
        m=m.filter(ImageFilter.GaussianBlur(blur))
        return np.asarray(m).astype(float)/255*op
    a=layer(22,46,9,0.34); b=layer(12,26,4,0.26); c=layer(5,11,2,0.18)
    al=1-(1-a)*(1-b)*(1-c)
    al=np.clip(al,0,1)
    rgb=np.zeros((H,W,3),np.uint8);rgb[...]=(18,18,26)
    return Image.fromarray(np.dstack([rgb,(al*255).astype(np.uint8)]),'RGBA')
if __name__=='__main__':
    s=make_shadow();s.save('shadow_hi.png')
    body=Image.open(A+'a5cfb3af-image.png').convert('RGBA');ptr=Image.open(A+'9ddf62c2-image.png').convert('RGBA')
    k=Image.alpha_composite(body,ptr)
    bk=Image.new('RGBA',s.size,(236,236,242,255));bk.alpha_composite(s);bk.alpha_composite(k,(0,0));bk.save('shadow_prev.png')
