import json, sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw, ImageFont
import small as S
BONE=(232,226,212); RED=(138,28,28); BG=(10,10,10)
# Hand-drawn u per size: (left col, top row, rows). '#' = bone.
U={16:(9,10,["#..#",
             "#..#",
             "#..#",
             ".###"]),
   24:(14,15,["##..##",
              "##..##",
              "##..##",
              "##..##",
              ".#####"]),
   32:(17,21,["###..###",
              ".##...##",
              ".##...##",
              ".##...##",
              ".###.###",
              "..#######"])}
ERASE={16:[(11,9)],24:[(15,13),(15,14),(14,12)]}
def icon(size):
    p=json.load(open('u-free.json'))
    im=S.small('u-free',size,True).convert('RGB')
    if size in U:
        # redraw the F alone (no font u), then stamp the hand-drawn u
        p2=dict(p); json.dump(p2,open('u-free-Fonly.json','w'))
        orig=S.small.__globals__['ImageFont'].truetype
        im=Fonly(size)
        x0,y0,rows=U[size]; px=im.load()
        for (ex,ey) in ERASE.get(size,[]): px[ex,ey]=BG   # trim the middle arm's lower serif so the u has air
        for dy,r in enumerate(rows):
            for dx,c in enumerate(r):
                if c=='#': px[x0+dx,y0+dy]=BONE
    return im
def Fonly(size):
    # same as small(...,mono=True) but drawing only the F
    p=json.load(open('u-free.json')); F0,U0,upem,sc,ux=p['F0'],p['U0'],p['upem'],p['sc'],p['ux']
    im=Image.new('RGB',(size,size),BG); d=ImageDraw.Draw(im)
    b=max(1,round(size/42)); ins=max(1,round(size*8/128)) if size>=32 else (1 if size>=20 else 0)
    d.rectangle([ins,ins,size-1-ins,size-1-ins],outline=RED,width=b)
    inner=size-2*(ins+b)-2*max(1,round(size/16))
    left=min(F0[0],ux+U0[0]*sc); right=max(F0[2],ux+U0[2]*sc); top=F0[3]; bot=min(0,U0[1]*sc)
    s=min(inner/(right-left), inner/(top-bot)); fpx=max(6,round(upem*s)); s=fpx/upem
    w=(right-left)*s; h=(top-bot)*s; x0=round((size-w)/2-left*s); base=round((size+h)/2+bot*s)
    m=Image.new('1',(size,size)); dm=ImageDraw.Draw(m); dm.fontmode='1'
    dm.text((x0,base),'F',font=ImageFont.truetype(p['path'],fpx),fill=1,anchor='ls')
    im.paste(BONE,(0,0),m.convert('L'))
    return im
if __name__=='__main__':
    row=[]
    for s in (16,24,32,48):
        im=icon(s); im.save(f'fu-{s}.png'); row.append(im.resize((s*6,s*6),Image.NEAREST))
        px=im.load(); print(s); [print(''.join('#' if px[x,y][0]>200 else ('r' if px[x,y][0]>100 else '.') for x in range(s))) for y in range(s)] if s<48 else None
    W=sum(r.width for r in row)+12*5; H=max(r.height for r in row)+24
    sheet=Image.new('RGB',(W,H),(43,43,43)); x=12
    for r in row: sheet.paste(r,(x,12)); x+=r.width+12
    real=Image.new('RGB',(16+24+32+48+60,60),(43,43,43)); x=10
    for s in (16,24,32,48): real.paste(Image.open(f'fu-{s}.png'),(x,(60-s)//2)); x+=s+10
    out=Image.new('RGB',(W,H+60),(43,43,43)); out.paste(sheet,(0,0)); out.paste(real,(12,H)); out.save('sheet10.png')
