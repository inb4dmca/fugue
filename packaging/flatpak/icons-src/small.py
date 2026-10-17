import json, sys
from PIL import Image, ImageDraw, ImageFont
BG=(10,10,10,255); BONE=(232,226,212,255); RED=(138,28,28,255)
def small(name, size, mono=False):
    p=json.load(open(name+'.json')); F0,U0,upem,sc,ux=p['F0'],p['U0'],p['upem'],p['sc'],p['ux']
    im=Image.new('RGBA',(size,size),BG); d=ImageDraw.Draw(im)
    b=max(1,round(size/42)); ins=max(1,round(size*8/128)) if size>=32 else (1 if size>=20 else 0)
    d.rectangle([ins,ins,size-1-ins,size-1-ins],outline=RED,width=b)
    inner=size-2*(ins+b)-2*max(1,round(size/16))      # room for the mark
    left=min(F0[0],ux+U0[0]*sc); right=max(F0[2],ux+U0[2]*sc); top=F0[3]; bot=min(0,U0[1]*sc)
    s=min(inner/(right-left), inner/(top-bot))          # px per font unit
    fpx=max(6,round(upem*s)); s=fpx/upem; upx=max(4,round(upem*s*sc))
    w=(right-left)*s; h=(top-bot)*s
    x0=round((size-w)/2-left*s); base=round((size+h)/2+bot*s)
    mode='1' if mono else 'L'
    for ch,px,pos in (('F',fpx,x0),('u',upx,x0+round(ux*s))):
        m=Image.new(mode,(size,size)); dm=ImageDraw.Draw(m)
        if mono: dm.fontmode='1'
        dm.text((pos,base),ch,font=ImageFont.truetype(p['path'],px),fill=255,anchor='ls')
        im.paste(BONE,(0,0),m.convert('L'))
    return im
for name in sys.argv[1:]:
    for mono in (False,True):
        row=[small(name,s,mono).resize((s*4,s*4),Image.NEAREST) for s in (16,24,32,48)]
        W=sum(r.width for r in row)+10*5; H=max(r.height for r in row)+20
        sheet=Image.new('RGBA',(W,H),(43,43,43,255)); x=10
        for r in row: sheet.paste(r,(x,10)); x+=r.width+10
        sheet.save(f'{name}-small{"-mono" if mono else ""}.png')
    for s in (16,24,32,48): small(name,s).save(f'{name}-{s}px.png')
