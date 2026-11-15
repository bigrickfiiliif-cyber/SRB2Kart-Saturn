import json,time,urllib.request,sys
from common import select
raw=json.load(open('raw.json'))
tr=json.load(open('tracks.json')) if __import__('os').path.exists('tracks.json') else {}
sel=[r for r in select(raw) if str(r['collectionId']) not in tr]
groups={}
for r in sel: groups.setdefault(raw['artists'][r['_seed']]['country'],[]).append(str(r['collectionId']))
jobs=[(c,v[i:i+12]) for c,v in groups.items() for i in range(0,len(v),12)]
print(len(sel),'to fetch',len(jobs),'calls')
for i,(cc,b) in enumerate(jobs):
    for k in range(6):
        try:
            with urllib.request.urlopen(f'https://itunes.apple.com/lookup?id={",".join(b)}&entity=song&limit=400&country={cc}',timeout=40) as r: res=json.load(r)['results']; break
        except Exception as e: print('ERR',e,file=sys.stderr); time.sleep(20*(k+1)); res=None
    if res is None: continue
    for x in b: tr.setdefault(x,[])
    for s in res:
        if s.get('wrapperType')=='track':
            tr.setdefault(str(s['collectionId']),[]).append({'d':s.get('discNumber',1),'n':s.get('trackNumber',0),'t':s['trackName'],'a':s['artistName'],'ms':s.get('trackTimeMillis',0)})
    json.dump(tr,open('tracks.json','w'),ensure_ascii=False); print(i); time.sleep(1.5)
