import json,urllib.request,urllib.error,sys,re,os
from concurrent.futures import ThreadPoolExecutor
b=json.load(open('built.json'))
nc=[a for a in b['albums'] if not a.get('_art') and a.get('mb')]
UA={'User-Agent':'DeutschrapArchiv/1.0 (fan archive)'}
class NR(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*a,**k): return None
op=urllib.request.build_opener(NR)
C=json.load(open('caa2.json')) if os.path.exists('caa2.json') else {}
def one(a):
    for k in range(4):
        try: return one_(a)
        except Exception as ex: import time; time.sleep(3*(k+1))
    print('fail',a['mb'],file=sys.stderr)
def one_(a):
    rg=a['mb']
    if rg in C: return
    try:
        op.open(urllib.request.Request(f'https://coverartarchive.org/release-group/{rg}',headers=UA),timeout=30); C[rg]={'err':'noredir'}; return
    except urllib.error.HTTPError as e:
        if e.code==404: C[rg]={'err':404}; return
        loc=e.headers.get('Location') or ''
    m=re.search(r'mbid-([0-9a-f-]{36})',loc)
    if not m: C[rg]={'err':'loc '+loc}; return
    item='mbid-'+m.group(1)
    try:
        md=json.load(urllib.request.urlopen(urllib.request.Request(f'https://archive.org/metadata/{item}',headers=UA),timeout=30))
        files=[f['name'] for f in md.get('files',[])]
        idx=None
        # front image: CAA index not readable; choose the lowest-numbered image's 250 thumb
        th=sorted(f for f in files if f.endswith('_thumb250.jpg'))
        if not th: th=sorted(f for f in files if re.search(r'-\d+\.(jpg|png)$',f))
        if not th: C[rg]={'err':'nofile'}; return
        C[rg]={'item':item,'file':th[0],'servers':[md.get('d1'),md.get('d2')],'dir':md.get('dir'),'nimg':len(th)}
    except urllib.error.HTTPError as ex: C[rg]={'err':'md'+str(ex.code)}
with ThreadPoolExecutor(3) as ex: list(ex.map(one,nc))
json.dump(C,open('caa2.json','w'))
import collections
ok=[v for v in C.values() if 'file' in v]
print(len(nc),len(ok),collections.Counter(str(v.get('err')) for v in C.values() if 'err' in v).most_common(5))
cnt=collections.Counter(s for v in ok for s in v['servers'] if s)
print(len(cnt))
# greedy cover of items by hosts
need=list(ok);hosts=[]
while need:
    c=collections.Counter(s for v in need for s in v['servers'] if s)
    h,_=c.most_common(1)[0];hosts.append(h);need=[v for v in need if h not in v['servers']]
print(len(hosts),hosts)
