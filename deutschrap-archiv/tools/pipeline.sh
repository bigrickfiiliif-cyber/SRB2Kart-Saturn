cd "$(dirname "$0")"
while pgrep -f "python3 mb.py" >/dev/null; do sleep 30; done
python3 mbresolve.py > mbres.log 2>mbres.err
python3 tracks.py > tracks4.log 2>&1
python3 build.py > build.log 2>&1
echo PIPEDONE >> build.log
