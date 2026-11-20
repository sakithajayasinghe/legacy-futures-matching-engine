#!/bin/sh
# NEWDAY - OPEN A NEW SESSION DIRECTORY, FETCH THE DAY'S ORDER FILE AND
# RUN FUTSESS ON IT.  PRINTS THE SESSION ID.  KEEPS THE LAST KEEP_RUNS
# SESSIONS ON DISK.
#
# usage: run/NEWDAY.sh [orders-file]
#
# WITH NO FILE A DAY OF TEST TRAFFIC IS GENERATED FOR TODAY'S DATE (THE
# ORDER ENTRY FRONT END THAT USED TO DROP THE FILE IS GONE).

BASE=$(cd "$(dirname "$0")/.." && pwd)
OUTROOT=$BASE/output
KEEP_RUNS=${KEEP_RUNS:-30}
COUNT=${COUNT:-10000}

RUNID=$(date +%Y%m%d_%H%M%S)
while [ -e "$OUTROOT/$RUNID" ]; do
    sleep 1
    RUNID=$(date +%Y%m%d_%H%M%S)
done
OUT=$OUTROOT/$RUNID
mkdir -p "$OUT" || exit 16

if [ -n "$1" ]; then
    cp "$1" "$OUT/ORDERS.DAT" || { rmdir "$OUT"; exit 16; }
else
    BUSDATE=${BUSDATE:-$(date +%Y%m%d)}
    export BUSDATE
    python3 "$BASE/tools/gen_orders.py" --count "$COUNT" --seed "${SEED:-$(date +%s)}" \
        --date "$BUSDATE" --out "$OUT/ORDERS.DAT" > /dev/null || { rm -rf "$OUT"; exit 16; }
fi

"$BASE/run/RUNSESSION.sh" "$OUT/ORDERS.DAT" "$OUT" > "$OUT/JOBLOG.TXT" 2>&1
RC=$?

ls -1 "$OUTROOT" | grep -E '^[0-9]{8}_[0-9]{6}$' | sort -r | tail -n +$((KEEP_RUNS + 1)) |
    while read -r old; do rm -rf "${OUTROOT:?}/$old"; done

echo "$RUNID"
exit $RC
