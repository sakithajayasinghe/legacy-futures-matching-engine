#!/bin/sh
# OPCON - START THE OPERATOR CONSOLE.
# IF NO SESSION IS ON FILE YET THE SAMPLE DAY IS RUN FIRST SO THAT THE
# CONSOLE HAS SOMETHING TO SHOW.

BASE=$(cd "$(dirname "$0")/.." && pwd)
FUTHOME=${FUTHOME:-$BASE}
export FUTHOME

mkdir -p "$FUTHOME/output"
if [ -z "$(ls -1 "$FUTHOME/output" | grep -E '^[0-9]{8}_[0-9]{6}$')" ]; then
    "$BASE/run/NEWDAY.sh" "$BASE/data/ORDERS.DAT" > /dev/null
fi

exec "$BASE/bin/opcon"
