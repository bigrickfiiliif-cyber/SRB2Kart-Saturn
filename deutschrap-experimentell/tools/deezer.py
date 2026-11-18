import json,time,urllib.request,urllib.parse,os,sys,re
from common import norm
b=json.load(open('built.json'))
D=json.load(open('dz.json')) if os.path.exists('dz.json') else {'al':{},'ar':{}}
def get(u):
    for k in range(5):
        try:
            d=json.load(urllib.request.urlopen(u,timeout=30)); time.sleep(0.13)
            if isinstance(d,dict) and d.get('error'):
                if d['error'].get('code')==4: time.sleep(5); continue
                return None
            return d
        except Exception as e: print('ERR',e,file=sys.stderr); time.sleep(3)
def strip(t): return norm(re.sub(r'\(.*?\)|\[.*?\]|- ?ep$| ep$','',t,flags=re.I))
n=0
for a in b['albums']:
    if a['id'] in D['al']: continue
    q=f'artist:"{(a["artists"] or [a["artist"]])[0]}" album:"{re.sub(r"[()\[\]]","",a["title"])}"'
    r=get('https://api.deezer.com/search/album?'+urllib.parse.urlencode({'q':q,'limit':10}))
    hit=None
    for x in (r or {}).get('data',[]):
        if strip(x['title'])==strip(a['title']) and (norm(x['artist']['name']) in norm(a['artist']) or norm((a["artists"] or [a["artist"]])[0]) in norm(x['artist']['name'])):
            hit=x;break
    if hit:
        d=get(f'https://api.deezer.com/album/{hit["id"]}') or {}
        D['al'][a['id']]={'id':hit['id'],'fans':d.get('fans'),'cover':hit.get('cover_medium'),'artist':hit['artist']['id'],'year':(d.get('release_date') or '')[:4]}
    else: D['al'][a['id']]=None
    n+=1
    if n%50==0: json.dump(D,open('dz.json','w')); print(n,sum(1 for v in D['al'].values() if v),flush=True)
json.dump(D,open('dz.json','w'))
for ar in b['artists']:
    if not ar['main'] or ar['name'] in D['ar']: continue
    r=get('https://api.deezer.com/search/artist?'+urllib.parse.urlencode({'q':f'artist:"{ar["name"]}"','limit':5}))
    hit=next((x for x in (r or {}).get('data',[]) if norm(x['name'])==norm(ar['name'])),None)
    D['ar'][ar['name']]={'id':hit['id'],'fans':hit.get('nb_fan')} if hit else None
json.dump(D,open('dz.json','w'))
print('albums',sum(1 for v in D['al'].values() if v),'artists',sum(1 for v in D['ar'].values() if v))
