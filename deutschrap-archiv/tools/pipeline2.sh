cd "$(dirname "$0")"
python3 fetch.py > p2_fetch.log 2>&1
python3 tracks.py > p2_tracks.log 2>&1
python3 build.py > p2_build1.log 2>&1
python3 mb.py > p2_mb.log 2>&1
python3 mbresolve.py > p2_mbres.log 2>p2_mbres.err
python3 tracks.py > p2_tracks2.log 2>&1
python3 build.py > build.log 2>&1
echo PIPEDONE >> build.log
