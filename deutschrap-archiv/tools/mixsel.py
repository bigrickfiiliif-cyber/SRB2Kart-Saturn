# select German-language mixtapes 2000-2009 from MusicBrainz data (excluded rgs in mb.json + DACH mixtape search)
import json,re
from common import norm
seeds={l.split('|')[0]:l.strip().split('|') for l in open('artists.txt') if l.strip() and not l.startswith('#')}
mb=json.load(open('mb.json'))['artists']; R=json.load(open('mbmix.json'))
def credit(x): return ''.join(c['name']+c.get('joinphrase','') for c in x.get('artist-credit',[]))
BLACK=re.compile(r'aguilera|b.real|cunninlynguists|dj clue|lloyd banks|guru|snoop|g.unit|50 cent|neptunes|mercedes|roots rockers|hotta ragga|reggae|kya bamba|marekage|mr\. bil|deza|dogzy|chakal|ikar|oxsa|roccobelly|passenger of shit|chupacabras|blak ryno|kista|machrome|dj boogie|deckstar|shifroc|forces of nature|not your average|club classics|hip hop joints|convivum|crunkshit|easyone|spinback|dj kriz|dj hank|jaleel|dj ron|snare one|new kidz|have a naiz|m-boogie|ming dynasty vol|bandulero|zulu soundsystem|munich sound|day\.din|bump my shit|terror era|old school b-boys|sepalot|ecko unltd|mix 6|bennylicious|backpocket|trigga music|independence day|px records|bandits|juc-e3|j.dawg|schnabel|welcome 2|eimsbush tapes|fäbschä|teddy-o|westcoast|presents club|alles für die cutz|best of freetracks|mindtricks|einblick zurück|ta\$h mixtape|zumo breaks|dj léa|fireworks|gettin|jiggy|player.s cut|clubbers guide mixtape vol.?[12]$|grau|nicon|scram jones|midnight soul|harris im club',re.I)
EXTRA={'Verbal Kint':'DE','DJ Ugly Cut':'DE','Caser und Kraz':'DE','Mad Skill':'DE','Oschtblock Kuabuaba':'CH','Dj D-Nice':'DE','Dr. Stimpy':'DE','DJ Lifeforce':'DE','BTM Squad':'DE','Cone Gorilla':'DE','Ostblokk':'DE','KL52':'DE','DJ Frenetikk':'DE','Boba Fettt':'DE','Creaturen der Nacht':'DE','Beathoavenz':'DE','41 Beatfanatika':'DE','Charnell':'DE'}
seedn=sorted(seeds,key=len,reverse=True)
def seed_of(cr):
    n=norm(cr)
    if re.search(r'dj mad zee',cr,re.I): return None
    for s in seedn:
        if len(norm(s))>=3 and re.search(r'(^|[^a-z0-9])'+re.escape(s.lower())+r'($|[^a-z0-9])',cr.lower()): return s
    for s in seedn:
        if len(norm(s))>=5 and norm(s) in n: return s
    return None
out={}
def add(rgid,title,cr,date,seed,land,rel=None,src=''):
    if BLACK.search(cr+' '+title): return
    y=int(date[:4])
    if not 2000<=y<=2009: return
    if rgid in out: return
    out[rgid]=dict(rg=rgid,title=title,credit=cr,year=y,date=date,ptype='Album',sec=['Mixtape/Street'],seed=seed,land=land,rel=rel,src=src)
# (a) rgs from seeded artists that were filtered as Other/DJ-mix/Compilation
for name,a in mb.items():
    if not a: continue
    for rg in a['rgs']:
        d=rg.get('first-release-date') or ''
        if not d[:4].isdigit(): continue
        sec=set(rg.get('secondary-types',[])); pt=rg.get('primary-type')
        if pt in ('Album','EP') and not sec&{'Compilation','Remix','DJ-mix'}: continue
        if pt=='Single': continue
        if 'Mixtape/Street' in sec or re.search(r'mix ?tape|\btape\b|street ?album|juice exclusive',rg['title'],re.I):
            add(rg['id'],rg['title'],credit(rg) or name,d,name,seeds[name][1],src='rg')
# (b) DACH mixtape search
new={}
for r in R:
    cr=credit(r); lang=(r.get('text-representation') or {}).get('language'); rg=r.get('release-group',{})
    if cr=='Various Artists' and not re.search(r'slangnacht|optik|herz im takt|kays',r['title'],re.I): continue
    if cr=='Various Artists': cr='Diverse'
    if False and not re.search(r'slangnacht|optik|herz im takt|kays',r['title'],re.I): continue
    s=seed_of(cr)
    land=r.get('country') if r.get('country') in ('DE','AT','CH') else 'DE'
    if not s:
        e=next((k for k in EXTRA if k.lower() in cr.lower()),None)
        if e: s=e; new[e]=EXTRA[e]
        elif lang in ('deu','gsw'): s=cr; new[cr]=land
        else: continue
    add(rg.get('id') or r['id'],r['title'],cr,r.get('date') or '0',s,seeds[s][1] if s in seeds else new[s],rel=r['id'],src='search')
json.dump(out,open('mixsel.json','w'),ensure_ascii=False,indent=0)
json.dump(new,open('mixnew.json','w'),ensure_ascii=False)
print(len(out),'mixtapes;',len(new),'new artists')
for v in sorted(out.values(),key=lambda v:v['credit'].lower()): print(v['year'],v['land'],'|',v['credit'],'|',v['title'],'| seed:',v['seed'])
