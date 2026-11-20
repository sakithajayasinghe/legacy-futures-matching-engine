# FUTMATCH: legacy futures order-matching batch (C)

A second **"before"** system for TransitionX 2026: the end-of-session matching run of a fictional regional commodities and futures exchange, written in C89 in the style of the mid-1990s, with a line-mode operator console.

> **This is a synthetic legacy system.** It was written for TransitionX 2026 as a test subject for modernization tooling. The exchange ("Port Halden Commodity and Futures Exchange"), its products, its clearing accounts and the people behind the initials in the change logs are all invented. Its problems are deliberate: one very large source file, global state shared by every routine, `goto` control flow, magic numbers, reference data compiled into headers, logic copied between routines and between the batch and the console, dead code nobody dared delete, hard-coded paths, no tests and no written rules. The code itself is correct and stable. It is legacy, not broken.

It is deliberately a different animal from the COBOL equity engine in `legacy-order-matching-engine`: futures contracts with delivery months, an opening auction, several order types, two allocation methods, risk checks, fees and an end-of-day settlement.

## What it does

```
                    ctl/DATECARD (business date)
                               |
ORDERS.DAT ──► FUTMATCH (one C program) ──► FILLS.DAT    trades
 fixed-width    pre-open · opening auction    REJECTS.DAT  rejected records, with reason
 80-byte recs   continuous matching · stops   BOOK.DAT     orders still working at the close
 + trailer      risk · fees · settlement      SETTLE.DAT   settlement prices, variation, margin
                                              SESSION.RPT  end-of-session report
                                              AUDIT.LOG    event trail
                               ▲
                    OPCON (operator console) reads the files, can submit a run
```

The batch reads the day's orders in arrival order and replays the session: orders entered before the open are uncrossed in an auction, then the rest are matched one by one against the book. At the end it prices every contract, marks every account to market and prints control totals.

## Layout

| Path | Contents |
|---|---|
| `src/futmatch.c` | The whole engine |
| `src/futmatch.h` | Record layout, table sizes, session constants, reject reasons |
| `src/futtab.h` | Product master and clearing account master, compiled in |
| `src/opcon.c` | Operator console (carries its own copy of the product table) |
| `run/RUNSESSION.sh` | Job script: picks up the business date and runs the batch |
| `run/NEWDAY.sh` | Opens a new session directory, fetches or generates the order file, runs the job |
| `run/OPCON.sh` | Starts the console (container entry point) |
| `ctl/DATECARD` | Business date card, `BUSDATE=CCYYMMDD` |
| `tools/gen_orders.py` | Generates a synthetic day of orders |
| `data/ORDERS.DAT` | Sample input: 10,000 records for business date 2026-11-16 |

## Run it

With Docker (any OS):

```sh
make docker-build
make docker-run          # operator console; sessions appear in ./output/<timestamp>/
make docker-batch        # batch only, on the sample file
```

With gcc installed locally (`sudo apt install gcc make python3`):

```sh
make build
make run                 # batch on data/ORDERS.DAT, output in ./output/<timestamp>/
make console             # operator console against ./output
```

The job takes the order file and the output directory:

```sh
./run/RUNSESSION.sh data/ORDERS.DAT /tmp/out
```

It ends with a console line `FUTSESS - ENDED ... RC=<n>`.

Generate a different day:

```sh
python3 tools/gen_orders.py --count 100000 --seed 7 --out /tmp/ORDERS_100K.DAT
./run/RUNSESSION.sh /tmp/ORDERS_100K.DAT /tmp/out100k
```

### Business date

The run is for one business date. It comes from the `BUSDATE` environment variable (`CCYYMMDD`) if set, otherwise from the date card `ctl/DATECARD`, otherwise from the system clock. The shipped date card and the generator's default `--date` are both 2026-11-16, so the sample runs the same way on any day. The date is printed on the session report.

The same order file with the same business date always produces the same six output files, byte for byte. Nothing in them depends on the wall clock.

## Operator console (legacy UI)

A line-mode menu program, the kind that ran on the machine-room console or over telnet from the floor supervisor's PC. It reads the session files directly and can submit a new run.

```
 PORT HALDEN COMMODITY AND FUTURES EXCHANGE
 FUTMATCH OPERATOR CONSOLE V2.3        SESSION 20261116_091500
 ------------------------------------------------------------
  1  SESSION REPORT             6  SETTLEMENT PRICES
  2  FILLS                      7  PRICE LIMIT INQUIRY
  3  REJECTS                    8  RUN NEW SESSION
  4  ORDER BOOK BY CONTRACT     9  SELECT SESSION
  5  ACCOUNT POSITIONS          X  EXIT
 SELECT ==>
```

```sh
docker run --rm -it futmatch-legacy          # in a terminal
make docker-web                              # in a browser: http://localhost:7681
```

**Run new session** generates a day of orders for today's date and runs the batch on it. The console takes numbered choices and plain typed answers, so it can also be driven by a script; when input is not a terminal it does not page and exits at end of input:

```sh
printf '1\n4\nCRNH27\nX\n' | docker run --rm -i futmatch-legacy
```

The browser view is the same program behind `ttyd`, a web terminal added to the image for demonstrations. It is not part of the legacy system.

## Return codes

| RC | Meaning |
|---|---|
| `0` | Clean run |
| `4` | Completed, some records rejected |
| `8` | Completed, but the control totals do not agree (trailer missing or wrong) |
| `16` | Aborted: file error or invalid business date |

`RUNSESSION.sh` exits 0 for RC 0 and 4, and with the return code otherwise.

## Notes for whoever inherits this

- Built with `gcc -std=gnu89`. It needs a 64-bit `long` (it was ported from a 32-bit Unix in 2003 and the money fields rely on it).
- Sessions are kept under `output/`; `NEWDAY.sh` keeps the newest 30.
- There are no tests and no specification. The change logs at the top of each source file are the only history.
