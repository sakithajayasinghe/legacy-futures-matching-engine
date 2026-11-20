#!/bin/sh
# RUNSESSION - END OF SESSION MATCHING JOB (FUTSESS)
#
# STEP 1  PICK UP THE BUSINESS DATE FROM THE DATE CARD
# STEP 2  RUN FUTMATCH OVER THE DAY'S ORDER FILE
#
# THE DATE CARD (ctl/DATECARD) IS SET BY OPERATIONS BEFORE THE RUN.
# BUSDATE IN THE ENVIRONMENT OVERRIDES THE CARD (RERUNS).  NO CARD AND
# NO OVERRIDE = TODAY.
#
# usage: run/RUNSESSION.sh [orders-file] [output-dir]
#
# 00-05-15 DSW  REPLACES THE OLD PIT CLOSE JOB
# 03-09-29 PLN  LINUX.  /bin/sh, NO MORE CSH

BASE=$(cd "$(dirname "$0")/.." && pwd)
IN=${1:-$BASE/data/ORDERS.DAT}
OUT=${2:-$BASE/output/$(date +%Y%m%d_%H%M%S)}

if [ ! -f "$IN" ]; then
    echo "FUTSESS - ORDER FILE NOT FOUND: $IN  RC=16"
    exit 16
fi
if [ ! -x "$BASE/bin/futmatch" ]; then
    echo "FUTSESS - PROGRAM NOT BUILT: $BASE/bin/futmatch  RC=16"
    exit 16
fi
mkdir -p "$OUT" || { echo "FUTSESS - CANNOT CREATE $OUT  RC=16"; exit 16; }

if [ -z "$BUSDATE" ] && [ -f "$BASE/ctl/DATECARD" ]; then
    BUSDATE=$(sed -n 's/^BUSDATE=\([0-9]*\).*/\1/p' "$BASE/ctl/DATECARD" | head -1)
fi
export BUSDATE

echo "FUTSESS - STARTED  $(date '+%Y-%m-%d %H:%M:%S')  INPUT=$IN  BUSDATE=${BUSDATE:-TODAY}"
"$BASE/bin/futmatch" "$IN" "$OUT"
RC=$?
echo "FUTSESS - ENDED    $(date '+%Y-%m-%d %H:%M:%S')  OUTPUT=$OUT  RC=$RC"

# RC 0 = CLEAN, RC 4 = COMPLETED WITH REJECTS.  ANYTHING ELSE GOES TO
# THE DUTY SUPERVISOR
if [ "$RC" -gt 4 ]; then
    exit "$RC"
fi
exit 0
