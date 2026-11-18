import json, time, urllib.request, urllib.parse, unicodedata, re, sys, os
OUT='raw.json'
data=json.load(open(OUT)) if os.path.exists(OUT) else {"artists":{}, "tracks":{}}
def norm(s):
    s=unicodedata.normalize('NFKD',s.lower()).encode('ascii','ignore').decode()
    return re.sub(r'[^a-z0-9]','',s.replace('&','und'))
def get(url):
    for i in range(6):
        try:
            with urllib.request.urlopen(url,timeout=30) as r:
                time.sleep(1.2); return json.load(r)
        except Exception as e:
            print('ERR',e,url[:90],file=sys.stderr); time.sleep(20*(i+1))
    return {"results":[]}
def save(): json.dump(data,open(OUT,'w'),ensure_ascii=False)
seeds=[l.strip().split('|') for l in open('artists.txt') if l.strip() and not l.startswith('#')]
GOOD=('hip','rap','reggae','dance','pop','alternative','electronic','r&b','soul','deutsch','urban')
for name,land,genres in seeds:
    if name in data["artists"] and data["artists"][name].get('v')==2: continue
    found=None
    for c in (['ch','de','at'] if land=='CH' else ['at','de'] if land=='AT' else ['de']):
        q=urllib.parse.urlencode({'term':name,'entity':'musicArtist','country':c,'limit':15})
        res=get('https://itunes.apple.com/search?'+q)['results']
        ex=[r for r in res if norm(r.get('artistName',''))==norm(name)]
        ex.sort(key=lambda r: 0 if any(g in r.get('primaryGenreName','').lower() for g in ('hip','rap')) else 1)
        if ex: found=(ex[0],c); break
    if not found:
        data["artists"][name]={"land":land,"genres":genres,"found":False,"v":2}; print('MISS',name); save(); continue
    a,c=found
    res=get(f'https://itunes.apple.com/lookup?id={a["artistId"]}&entity=album&limit=200&country={c}')['results']
    def yrs(r):
        ys=[int(r.get('releaseDate','0000')[:4] or 0)]
        m=re.search(r'(19|20)\d\d',r.get('copyright',''))
        if m: ys.append(int(m.group(0)))
        return ys
    albums=[r for r in res if r.get('wrapperType')=='collection' and any(1980<=y<=2026 for y in yrs(r))]
    data["artists"][name]={"land":land,"genres":genres,"found":True,"artistId":a["artistId"],"itunesGenre":a.get('primaryGenreName'),"country":c,"albums":albums,"v":2}
    print('OK',name,a.get('primaryGenreName'),len(albums)); save()
