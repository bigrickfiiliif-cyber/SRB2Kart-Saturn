cd "$(dirname "$0")"
python3 fetch.py > p3_fetch.log 2>&1
python3 tracks.py > p3_tracks.log 2>&1
python3 build.py > p3_build1.log 2>&1
python3 mb.py > p3_mb.log 2>&1
python3 mbresolve.py > p3_mbres.log 2>p3_mbres.err
python3 tracks.py > p3_tracks2.log 2>&1
python3 build.py > build.log 2>&1
echo PIPEDONE >> build.log
