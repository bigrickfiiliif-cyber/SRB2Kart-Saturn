cd "$(dirname "$0")"
python3 fetch.py > x_fetch.log 2>&1
python3 tracks.py > x_tracks.log 2>&1
python3 build.py > x_build1.log 2>&1
python3 mb.py > x_mb.log 2>&1
python3 mbresolve.py > x_mbres.log 2>x_mbres.err
python3 tracks.py > x_tracks2.log 2>&1
python3 build.py > build.log 2>&1
echo PIPEDONE >> build.log
