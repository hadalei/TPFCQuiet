"""Build a small vector-style fan mark and multi-resolution Windows icon."""
from pathlib import Path
from PIL import Image, ImageDraw
import math
root=Path(__file__).resolve().parents[1]
assets=root/'quiet/assets';assets.mkdir(exist_ok=True)
path='M128 113 C104 105 88 84 91 61 C95 36 126 35 142 53 C157 70 151 98 139 115 Z'
svg=f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256"><rect width="256" height="256" rx="52" fill="#007369"/><g fill="#ffffff">'+''.join(f'<path d="{path}" transform="rotate({a} 128 128)"/>' for a in (0,120,240))+'<circle cx="128" cy="128" r="20"/></g></svg>'
(assets/'app.svg').write_text(svg,'utf-8')
scale=4;im=Image.new('RGBA',(256*scale,256*scale));d=ImageDraw.Draw(im)
d.rounded_rectangle((0,0,256*scale-1,256*scale-1),radius=52*scale,fill='#007369')
def bezier(a,b,c,e):
 for i in range(33):
  t=i/32;u=1-t
  yield (u**3*a[0]+3*u*u*t*b[0]+3*u*t*t*c[0]+t**3*e[0],u**3*a[1]+3*u*u*t*b[1]+3*u*t*t*c[1]+t**3*e[1])
points=list(bezier((128,113),(104,105),(88,84),(91,61)))+list(bezier((91,61),(95,36),(126,35),(142,53)))+list(bezier((142,53),(157,70),(151,98),(139,115)))
for angle in (0,120,240):
 a=math.radians(angle);d.polygon([(round((128+(x-128)*math.cos(a)-(y-128)*math.sin(a))*scale),round((128+(x-128)*math.sin(a)+(y-128)*math.cos(a))*scale)) for x,y in points],fill='white')
d.ellipse((108*scale,108*scale,148*scale,148*scale),fill='white')
im=im.resize((256,256),Image.Resampling.LANCZOS)
im.save(assets/'app.png')
im.save(assets/'app.ico',sizes=[(s,s) for s in (16,20,24,32,40,48,64,128,256)])
print('Created app.svg, app.png and app.ico')
