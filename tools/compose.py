from PIL import Image
import numpy as np
import os

# Folder with the EVBikes art. Set EVB_ASSETS to change it.
A=os.path.join(os.environ.get('EVB_ASSETS', '/Users/adesh/EVBikes/assets'), '')
def hexc(h): h=h.lstrip('#'); return tuple(int(h[i:i+2],16) for i in (0,2,4))
def mask(p): return np.array(Image.open(A+p).convert('RGBA'))[...,3].astype(np.float32)/255
def over(dst,x,y,m,color,op=1.0):
    h,w=m.shape; H,W,_=dst.shape
    x0,y0=max(x,0),max(y,0); x1,y1=min(x+w,W),min(y+h,H)
    if x1<=x0 or y1<=y0: return
    a=m[y0-y:y1-y,x0-x:x1-x,None]*op
    dst[y0:y1,x0:x1]=dst[y0:y1,x0:x1]*(1-a)+np.array(color,np.float32)*a
def over_rgba(dst,x,y,p):
    a=np.array(Image.open(A+p).convert('RGBA')).astype(np.float32)/255
    h,w,_=a.shape
    al=a[...,3:4]; dst[y:y+h,x:x+w]=dst[y:y+h,x:x+w]*(1-al)+a[...,:3]*255*al
GLOW={'eco':'#57F2C9','sport':'#E0402A'}
HL={'eco':'#191B1E','sport':'#37100A'}
def shell(mode, header=True):
    c=np.zeros((480,1280,3),np.float32)
    over(c,84,45,mask('cluster/trimmed/ride_outline.png'),hexc('#5C5C5C'),0.85)
    over(c,128,5,mask('cluster/trimmed/shell_vignette.png'),(255,255,255),0.03)
    over(c,112,8,mask('cluster/trimmed/panel_haze.png'),hexc(GLOW[mode]),0.22)
    over(c,105,94,mask('cluster/trimmed/bar_channel.png'),hexc('#010101'))
    over(c,351,409,mask('cluster/trimmed/housing_bottom.png'),hexc('#141414'))
    if header:
        over_rgba(c,320,0,'design/header_ride.png')
        over(c,320,0,mask('design/header_light.png'),hexc(HL[mode]))
    over(c,144,93,mask('design/bar_inner_lines.png'),hexc(GLOW[mode]))
    over(c,84,45,mask('cluster/trimmed/glow_ride.png'),hexc(GLOW[mode]))
    return c
