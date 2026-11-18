import json,time,urllib.request,urllib.parse,os,sys
UA={'User-Agent':'DeutschrapArchiv/1.0 (fan archive)'}
def get(path,**q):
    q['fmt']='json'; url='https://musicbrainz.org/ws/2/'+path+'?'+urllib.parse.urlencode(q)
    for k in range(8):
        try:
            with urllib.request.urlopen(urllib.request.Request(url,headers=UA),timeout=30) as r:
                time.sleep(1.2); return json.load(r)
        except Exception as e:
            print('ERR',e,file=sys.stderr); time.sleep(min(60,6*(k+1)))
mb=json.load(open('mb.json'))['artists']
R=json.load(open('mbrate.json')) if os.path.exists('mbrate.json') else {'done':[],'rg':{},'artist':{}}
for name,a in mb.items():
    if not a or a['id'] in R['done']: continue
    off=0
    while True:
        b=get('release-group',artist=a['id'],limit=100,offset=off,inc='ratings')
        if not b: break
        for g in b['release-groups']:
            r=g.get('rating') or {}
            if r.get('votes-count'): R['rg'][g['id']]=[r['value'],r['votes-count']]
        off+=100
        if off>=b.get('release-group-count',0): break
    ar=get('artist/'+a['id'],inc='ratings')
    if ar and (ar.get('rating') or {}).get('votes-count'): R['artist'][name]=[ar['rating']['value'],ar['rating']['votes-count']]
    R['done'].append(a['id']); json.dump(R,open('mbrate.json','w'))
print('rated rgs',len(R['rg']),'artists',len(R['artist']))
