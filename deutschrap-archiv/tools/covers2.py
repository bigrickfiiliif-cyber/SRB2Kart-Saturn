import json,os,zlib,urllib.request,sys,io
from PIL import Image
def synth(i): return 9000000000000+zlib.crc32(i.encode())
b=json.load(open('built.json'))
DZ=json.load(open('dz.json'))['al']
CA=json.load(open('caa2.json')) if os.path.exists('caa2.json') else {}
UA={'User-Agent':'DeutschrapArchiv/1.0 (fan archive)'}
n=dz=ca=0
for a in b['albums']:
    if a.get('_art'): continue
    p=f"out/covers/{synth(a['id'])}.jpg"
    if os.path.exists(p): continue
    urls=[]
    z=DZ.get(a['id'])
    if z and z.get('cover'): urls.append(('dz',z['cover']))
    c=CA.get(a.get('mb') or '')
    if c and 'file' in c:
        urls.append(('ca',f"https://archive.org/services/img/{c['item']}"))
    for kind,u in urls:
        try:
            raw=urllib.request.urlopen(urllib.request.Request(u,headers=UA),timeout=30).read()
            im=Image.open(io.BytesIO(raw)).convert('RGB')
            if min(im.size)<100: continue
            w,h=im.size;m=min(w,h);im=im.crop(((w-m)//2,(h-m)//2,(w-m)//2+m,(h-m)//2+m)).resize((200,200),Image.LANCZOS)
            im.save(p,quality=85); n+=1
            if kind=='dz': dz+=1
            else: ca+=1
            break
        except Exception as e: print('x',kind,u[:90],e,file=sys.stderr)
print('new covers',n,'deezer',dz,'caa',ca)
