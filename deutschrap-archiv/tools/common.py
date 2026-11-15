import re, unicodedata, json
def norm(s):
    s=unicodedata.normalize('NFKD',s.lower().replace('ß','ss')).encode('ascii','ignore').decode()
    return re.sub(r'[^a-z0-9]','',s.replace('&','und'))
JUNK=r'\s*[\(\[][^\)\]]*(deluxe|remaster|bonus|edition|version|explicit|premium|limited|re-?release|special|anniversary|jubil|neuauflage|reissue|expanded|uncut|clean|upload)[^\)\]]*[\)\]]'
def base_title(t):
    t=re.sub(r'\s+-\s+(EP|Single)$','',t,flags=re.I)
    t=re.sub(JUNK,'',t,flags=re.I)
    t=re.sub(r'\s+X$','',t)
    return t.strip()
def year_of(r):
    ry=int(r.get('releaseDate','0000')[:4] or 0)
    m=re.search(r'(19|20)\d\d',r.get('copyright',''))
    cy=int(m.group(0)) if m else None
    if cy and cy<ry: return cy
    return ry
def typ_of(r):
    n=r['collectionName']; tc=r.get('trackCount',0)
    if re.search(r'instrumental',n,re.I): return 'Instrumental'
    if re.search(r'best of|greatest|hits|collection|anthology|das beste|essential',n,re.I): return 'Best-of'
    if re.search(r'\blive\b|unplugged',n,re.I): return 'Live'
    if re.search(r'mixtape|\btape\b',n,re.I): return 'Mixtape'
    if re.search(r'remix',n,re.I): return 'Remix'
    if re.search(r'remix',n,re.I): return 'Remix'
    if re.search(r'\(single\)|feat\.',n,re.I) or tc<=3: return 'Single'
    if re.search(r'- EP$',n): return 'EP' if tc>=7 else 'EP?'
    if re.search(r'\bEP\b',n): return 'EP'
    if tc<=6: return 'EP?'
    if tc<=8: return 'EP'
    return 'Album'
EXCL=set()
def select(raw):
    """returns list of chosen itunes collection dicts with meta"""
    chosen={}
    for name,a in raw['artists'].items():
        if not a.get('found'): continue
        for r in a['albums']:
            n=r['collectionName']
            if re.search(r' - Single$',n) or r.get('trackCount',0)<=3: continue
            y=year_of(r)
            if not 2000<=y<=2009: continue
            if norm(name) not in norm(r.get('artistName','')) : continue
            t=typ_of(r)
            if t in ('Single','Best-of','Remix'): continue
            if (norm(r.get('artistName',''))+'|'+norm(base_title(n))) in EXCL or norm(base_title(n)) in EXCL: continue
            key=norm(r['artistName'])+'|'+norm(base_title(n))
            cur=chosen.get(key)
            cand=dict(r,_seed=name,_year=y,_typ=t,_title=base_title(n))
            if not cur or (y,-r.get('trackCount',0))<(cur['_year'],-cur.get('trackCount',0)):
                chosen[key]=cand
    return list(chosen.values())
