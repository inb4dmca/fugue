import sys, numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import binary_dilation
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.boundsPen import BoundsPen
bone='#e8e2d4'; red='#8a1c1c'
def fit(path, name, clear=0.045, overhang=0.04):
    tt=TTFont(path); upem=tt['head'].unitsPerEm; gs=tt.getGlyphSet(); cm=tt.getBestCmap()
    bF=BoundsPen(gs); gs[cm[ord('F')]].draw(bF); F0=bF.bounds; cap=F0[3]-F0[1]
    W,H,O,B=int(upem*1.6),int(upem*1.3),100,int(upem*1.1)
    def mask(ch,size,x):
        im=Image.new('L',(W,H)); ImageDraw.Draw(im).text((x,B),ch,font=ImageFont.truetype(path,size),fill=255,anchor='ls'); return np.array(im)>128
    Fm=mask('F',upem,O); Fd=binary_dilation(Fm,iterations=int(clear*cap))
    Fright=O+F0[2]
    for sc in np.arange(0.62,0.24,-0.01):
        um0=mask('u',int(upem*sc),0)
        cols=np.where(um0.any(0))[0]; ul,ur=cols[0],cols[-1]
        ok=[]
        for px in range(O, int(Fright+overhang*cap-ur), 4):
            um=np.roll(um0,px,axis=1)
            if not (um & Fd).any(): ok.append(px)
        if ok:
            px=ok[len(ok)//2]; break
    else: return None
    def d(ch):
        p=SVGPathPen(gs); gs[cm[ord(ch)]].draw(p); return p.getCommands()
    bU=BoundsPen(gs); gs[cm[ord('u')]].draw(bU); U0=bU.bounds
    ux=px-O   # font units (1 px = 1 unit at size=upem)
    left=min(F0[0],ux+U0[0]*sc); right=max(F0[2],ux+U0[2]*sc); top=F0[3]; bot=min(0,U0[1]*sc)
    s=min(96/(right-left),96/(top-bot))
    ox=(128-(right-left)*s)/2-left*s; oy=(128+(top-bot)*s)/2+bot*s
    body=f'''<rect width="128" height="128" fill="#0a0a0a"/>
<rect x="8" y="8" width="112" height="112" fill="none" stroke="{red}" stroke-width="3"/>
<g transform="translate({ox:.2f} {oy:.2f}) scale({s:.5f} {-s:.5f})" fill="{bone}">
<path d="{d('F')}"/>
<path transform="translate({ux:.1f} 0) scale({sc:.3f})" d="{d('u')}"/>
</g>'''
    open(name+'.svg','w').write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128">\n{body}\n</svg>\n')
    import json; open(name+".json","w").write(json.dumps(dict(path=path,sc=float(sc),ux=float(ux),F0=F0,U0=U0,upem=upem)))
    return round(sc,2)
for path,name in [(a.split('=')[0],a.split('=')[1]) for a in sys.argv[1:]]:
    print(name, fit(path,name))
