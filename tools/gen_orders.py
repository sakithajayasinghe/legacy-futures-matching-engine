#!/usr/bin/env python3
"""Generate a synthetic day of orders for FUTMATCH.

Writes the fixed-width ORDERS.DAT that the batch reads: 80-byte detail
records in arrival order, then one trailer record with the record count and
the quantity hash.  Traffic is mostly valid (auction orders before the open,
then limit, market, IOC, FOK, stop, amend and cancel records around a moving
price for each contract) with a share of every kind of bad record an
upstream feed produces.  The same seed always gives the same file.

usage: gen_orders.py [--count N] [--seed S] [--out FILE] [--date CCYYMMDD]
                     [--trailer ok|none|bad]
"""
import argparse
import random

MONTHS = "FGHJKMNQUVXZ"

# Must match src/futtab.h
#        code   alg lot tick   bset  carry   lim    cb  prot maxq plim ltd  months
PRODUCTS = [
    ("CRN", "P", 1, 25, 45250, 110, 3000, 2000, 500, 500, 300, 14, "HKNUZ"),
    ("WHT", "P", 1, 25, 61875, 160, 4500, 3000, 750, 400, 250, 14, "HKNUZ"),
    ("SOY", "P", 1, 25, 118650, -90, 7000, 4500, 1000, 400, 250, 13, "FHKNQUX"),
    ("RBR", "P", 5, 5, 16840, 35, 900, 600, 100, 250, 200, 16, MONTHS),
    ("CPR", "F", 2, 5, 38545, 20, 2000, 1300, 250, 300, 200, 26, "HKNUZ"),
    ("GLD", "F", 1, 10, 35240, 45, 2500, 1600, 300, 300, 200, 26, "GJMQVZ"),
    ("CRD", "F", 1, 1, 2145, -3, 150, 100, 25, 600, 400, 20, MONTHS),
]
PRODUCT_WEIGHT = {"CRN": 3.0, "WHT": 2.0, "SOY": 2.5, "RBR": 2.0, "CPR": 1.5, "GLD": 2.0, "CRD": 3.0}

#            id      tier type status weight
ACCOUNTS = [
    ("HGC001", 1, "H", "A", 2.0), ("NRM002", 1, "H", "A", 2.0), ("PQF003", 2, "H", "A", 1.5),
    ("TSR004", 1, "H", "A", 2.0), ("BLM005", 2, "H", "A", 1.5), ("KVT006", 1, "M", "A", 4.0),
    ("ORL007", 1, "M", "A", 4.0), ("DRX008", 1, "M", "A", 3.5), ("FNC009", 2, "S", "A", 1.5),
    ("WYB010", 2, "S", "A", 1.5), ("STK011", 2, "S", "A", 1.5), ("MRL012", 2, "S", "A", 1.5),
    ("QTN013", 3, "S", "A", 1.0), ("EBR014", 3, "S", "A", 1.0), ("LSK015", 3, "S", "A", 1.0),
    ("TVY016", 3, "S", "A", 0.8), ("HLW017", 3, "S", "A", 0.6), ("CDN018", 2, "S", "S", 0.12),
    ("ARV019", 2, "H", "A", 1.5), ("JSP020", 3, "S", "S", 0.08),
]
TIER = {a[0]: a[1] for a in ACCOUNTS}
SUSPENDED = [a[0] for a in ACCOUNTS if a[3] == "S"]
SMALL = [a[0] for a in ACCOUNTS if a[1] == 3 and a[3] == "A"]
NOT_SMALL = [a[0] for a in ACCOUNTS if a[1] != 3 and a[3] == "A"]

PRE_OPEN, OPEN, CLOSE = 8 * 3600 + 1800, 9 * 3600, 14 * 3600 + 1800


class Contract:
    def __init__(self, prod, code, md, bd):
        (self.pc, self.alg, self.lot, self.tick, bset, carry, self.lim, self.cb,
         self.prot, self.maxq, self.plim, self.ltd, _) = prod
        self.code = code
        self.md = md
        self.spot = md == 0
        self.last_day = self.spot and bd == self.ltd
        self.ref = (bset + carry * md) // self.tick * self.tick
        self.mid = self.ref
        self.hot = 0            # +1/-1 once the contract starts to run
        self.halted = False     # what the generator believes
        self.live = []          # (order no, account, side, price, qty, is_stop)
        if self.spot:
            self.maxq //= 2

    def clamp(self):
        room = self.lim - 2 * self.tick if self.hot else self.cb // 2
        self.mid = max(self.ref - room, min(self.ref + room, self.mid))
        if abs(self.mid - self.ref) >= self.cb + 8 * self.tick * max(1, self.cb // self.tick // 60):
            self.halted = True


class Gen:
    def __init__(self, count, seed, date):
        self.rnd = random.Random(seed)
        self.count = count
        self.by, self.bm, self.bd = date // 10000, date // 100 % 100, date % 100
        self.rows = []
        self.next_no = 1000
        self.n_pre = max(10, count * 6 // 100)
        self.build_contracts()

    # ---- reference data -------------------------------------------------
    def code(self, prod, md):
        ym = self.by * 12 + self.bm - 1 + md
        return "%s%s%02d" % (prod[0], MONTHS[ym % 12], ym // 12 % 100)

    def build_contracts(self):
        self.active, self.weights, self.quiet, self.far = [], [], [], []
        self.past_ltd, self.unlisted = [], []
        for prod in PRODUCTS:
            valid = []
            for md in range(0, 19):
                letter = MONTHS[(self.bm - 1 + md) % 12]
                if letter not in prod[12]:
                    if md > 0:
                        self.unlisted.append(self.code(prod, md))
                    continue
                if md == 0 and self.bd > prod[11]:
                    self.past_ltd.append(self.code(prod, 0))
                    continue
                valid.append(Contract(prod, self.code(prod, md), md, self.bd))
            for con, w in zip(valid[:3], (5.0, 3.0, 1.5)):
                if con.spot:
                    w = 2.0
                self.active.append(con)
                self.weights.append(w * PRODUCT_WEIGHT[prod[0]])
            if len(valid) > 3:
                self.quiet.append(valid[3])
            if len(valid) > 4:
                self.far.append(valid[4])
        r = self.rnd.random()
        nhot = 3 if r < 0.08 else 2 if r < 0.20 else 1 if r < 0.50 else 0
        self.hot_plan = []
        for con in self.rnd.sample([c for c in self.active if not c.spot], nhot):
            start = self.rnd.randint(self.count * 35 // 100, self.count * 80 // 100)
            self.hot_plan.append((start, con, self.rnd.choice((1, -1))))
        self.rogue = self.rnd.choice(SMALL) if self.rnd.random() < 0.3 else None
        self.rogue_at = self.rnd.randint(self.count * 30 // 100, self.count * 70 // 100)

    # ---- helpers --------------------------------------------------------
    def clock(self):
        i = len(self.rows)
        if i < self.n_pre:
            t = PRE_OPEN + i * 1800 // self.n_pre
        else:
            t = OPEN + (i - self.n_pre) * (CLOSE - OPEN) // max(1, self.count - self.n_pre)
            t = min(t, CLOSE - 1)
        return t

    @staticmethod
    def hms(t):
        return "%02d%02d%02d" % (t // 3600, t // 60 % 60, t % 60)

    def emit(self, typ, no, acc, con, side=" ", oty=" ", tif=" ", px=0, stp=0, qty=0, tm=None,
             raw=None):
        if raw is None:
            tm = self.hms(self.clock()) if tm is None else tm
            px = "%07d" % px if isinstance(px, int) else px
            stp = "%07d" % stp if isinstance(stp, int) else stp
            qty = "%05d" % qty if isinstance(qty, int) else qty
            no = "%08d" % no if isinstance(no, int) else no
            raw = ("%s%s%-6.6s%-6.6s%s%s%s%s%s%s%s" % (typ, no, acc, con, side, oty, tif, px, stp,
                                                      qty, tm)).ljust(80)
        self.rows.append(raw)

    def new_no(self):
        self.next_no += self.rnd.choice((1, 1, 1, 2, 3))
        return self.next_no

    def account(self, con=None):
        while True:
            acc = self.rnd.choices(ACCOUNTS, [a[4] for a in ACCOUNTS])[0][0]
            if con is not None and con.spot and TIER[acc] == 3:
                continue
            return acc

    def contract(self):
        w = [x * (0.1 if c.halted else 4.0 if c.hot else 1.0) for c, x in zip(self.active, self.weights)]
        return self.rnd.choices(self.active, w)[0]

    def qty(self, con, acc=None):
        r = self.rnd.random()
        lots = self.rnd.randint(1, 10) if r < 0.6 else self.rnd.randint(10, 50) if r < 0.9 \
            else self.rnd.randint(50, 150)
        if acc is not None and TIER[acc] == 3:
            lots = self.rnd.randint(1, 3) if acc == "HLW017" else min(lots, self.rnd.randint(1, 12))
        return max(con.lot, min(lots * con.lot, con.maxq // con.lot * con.lot))

    def drift(self, con):
        r = self.rnd.random()
        if con.hot and r < 0.6:
            con.mid += con.hot * con.tick * self.rnd.randint(1, 2) * max(1, con.cb // con.tick // 60)
        elif r < 0.30:
            con.mid += con.tick * self.rnd.choice((1, -1))
        con.clamp()

    def remember(self, con, no, acc, side, px, qty, is_stop=False):
        con.live.append((no, acc, side, px, qty, is_stop))

    @staticmethod
    def prune(con):
        """Forget orders the market has traded through: they are filled or triggered by now."""
        con.live = [x for x in con.live
                    if (x[3] < con.mid if x[2] == "B" else x[3] > con.mid) != x[5]]

    # ---- valid traffic --------------------------------------------------
    def limit(self, con, acc=None, side=None, px=None, qty=None, tif="D", aggressive=False):
        acc = acc or self.account(con)
        side = side or self.rnd.choice("BS")
        if px is None:
            k = self.rnd.randint(0, 3) if aggressive else -1 - min(12, int(self.rnd.expovariate(0.4)))
            px = con.mid + k * con.tick * (1 if side == "B" else -1)
        qty = qty or self.qty(con, acc)
        no = self.new_no()
        self.emit("N", no, acc, con.code, side, "L", tif, px, 0, qty)
        if tif == "D" and not aggressive:
            self.remember(con, no, acc, side, px, qty)
        return no

    def market(self, con, tif="D"):
        if con.last_day:
            return self.limit(con, aggressive=True)
        acc = self.account(con)
        self.emit("N", self.new_no(), acc, con.code, self.rnd.choice("BS"), "M", tif,
                  0, 0, min(self.qty(con, acc), 40 * con.lot))

    def stop(self, con):
        if con.last_day:
            return self.limit(con)
        acc, side = self.account(con), self.rnd.choice("BS")
        d = 1 if side == "B" else -1
        sp = con.mid + d * con.tick * self.rnd.randint(2, 6)
        no, qty = self.new_no(), min(self.qty(con, acc), 30 * con.lot)
        if self.rnd.random() < 0.5:
            self.emit("N", no, acc, con.code, side, "S", "D", 0, sp, qty)
        else:
            self.emit("N", no, acc, con.code, side, "T", "D",
                      sp + d * con.tick * self.rnd.randint(0, 3), sp, qty)
        self.remember(con, no, acc, side, sp, qty, True)

    def cancel(self, con):
        self.prune(con)
        if not con.live:
            return self.limit(con)
        i = len(con.live) - 1 - min(len(con.live) - 1, int(self.rnd.expovariate(0.15)))
        if len(con.live) > 40 and self.rnd.random() < 0.7:
            i = 0                                                            # oldest first
        no, acc = con.live[i][0], con.live[i][1]
        r = self.rnd.random()
        if r < 0.06:
            acc = self.rnd.choice([a for a in NOT_SMALL if a != acc])       # not the owner
        elif r < 0.10:
            no = self.next_no + 500000                                       # never issued
        else:
            del con.live[i]
        self.emit("C", no, acc, con.code)

    def amend(self, con):
        self.prune(con)
        plain = [x for x in con.live if not x[5]]
        if not plain:
            return self.limit(con)
        no, acc, side, px, qty, _ = self.rnd.choice(plain[-12:])
        r = self.rnd.random()
        d = 1 if side == "B" else -1
        if r < 0.40:
            self.emit("A", no, acc, con.code, qty=max(con.lot, qty // 2 // con.lot * con.lot))
        elif r < 0.60:
            self.emit("A", no, acc, con.code, qty=min(con.maxq, qty + con.lot * self.rnd.randint(1, 20)))
        elif r < 0.90:
            self.emit("A", no, acc, con.code, px=px + d * con.tick * self.rnd.randint(-2, 3))
        elif r < 0.94:
            self.emit("A", no, acc, con.code)                               # no change
        elif r < 0.97:
            other = self.rnd.choice([c for c in self.active if c is not con])
            self.emit("A", no, acc, other.code, qty=qty + con.lot)          # wrong contract
        else:
            stops = [x for x in con.live if x[5]]
            if stops:
                no, acc = stops[0][0], stops[0][1]                          # amend a stop
            self.emit("A", no, acc, con.code, qty=con.lot * 2)

    def prorata_burst(self):
        cons = [c for c in self.active if c.alg == "P" and not c.halted and not c.spot]
        con = self.rnd.choice(cons)
        side = self.rnd.choice("BS")
        accs = self.rnd.sample(NOT_SMALL, self.rnd.randint(4, 7))
        total = 0
        for acc in accs[:-1]:
            q = self.rnd.choice((2, 3, 5, 8, 10, 15, 25, 40, 60)) * con.lot
            self.limit(con, acc, side, con.mid, q)
            total += q
        take = max(con.lot, total * self.rnd.randint(30, 80) // 100 // con.lot * con.lot)
        self.limit(con, accs[-1], "S" if side == "B" else "B", con.mid, min(take, con.maxq),
                   tif=self.rnd.choice("DDI"))

    def sweep(self, con):
        """A contract on the run: fresh interest at the new price, then a seller (or buyer) through it."""
        side = "B" if con.hot < 0 else "S"
        a1, a2 = self.rnd.sample(NOT_SMALL, 2)
        self.limit(con, a1, side, con.mid, self.rnd.randint(2, 20) * con.lot)
        self.limit(con, a2, "S" if side == "B" else "B", con.mid,
                   min(con.maxq, self.rnd.randint(60, 200) * con.lot), tif="I")

    def self_trade(self):
        con = self.contract()
        acc = self.rnd.choice(NOT_SMALL)
        side = self.rnd.choice("BS")
        opp = "S" if side == "B" else "B"
        self.limit(con, acc, side, con.mid, self.qty(con))
        self.limit(con, acc, opp, con.mid, self.qty(con), tif=self.rnd.choice("DIF"))

    def quiet_book(self):
        """Resting interest in a back month that never trades."""
        if not self.quiet:
            return
        con = self.rnd.choice(self.quiet)
        side = "B" if PRODUCTS.index(next(p for p in PRODUCTS if p[0] == con.pc)) % 2 == 0 else "S"
        d = 1 if side == "B" else -1
        self.limit(con, self.rnd.choice(NOT_SMALL), side,
                   con.ref + d * con.tick * self.rnd.randint(-3, 2), self.qty(con))

    # ---- deliberately bad records ----------------------------------------
    def bad(self, pre=False):
        rnd = self.rnd
        con = self.contract()
        acc = self.account(con)
        side = rnd.choice("BS")
        px = con.mid - con.tick * (2 if side == "B" else -2)
        q = self.qty(con, acc)
        no = self.new_no()
        tm = self.hms(self.clock())
        good = ("N", no, acc, con.code, side, "L", "D", px, 0, q)
        kinds = ["short", "long", "type", "ono", "ono0", "time", "time2", "seq", "acct", "susp",
                 "prod", "month", "year", "nolist", "expired", "ltd", "fwd", "side", "otype", "tif",
                 "combo", "qty0", "qtyx", "lot", "maxq", "spotq", "pxx", "px0", "mktpx", "tick",
                 "lup", "ldn", "stp0", "stptick", "stplim", "stptrig", "auc", "lastday", "spot3",
                 "dup", "fok", "pos", "margin", "nomkt", "huge"]
        if pre:
            kinds = ["preioc", "prestop", "premkt", "acct", "tick", "qty0", "lup", "prod"]
        k = rnd.choice(kinds)
        if k == "short":
            self.emit(*good, raw=("N%08d%-6s%-6s" % (no, acc, con.code)).ljust(rnd.randint(30, 79)))
        elif k == "long":
            self.emit(*good, raw=("N%08d%-6s%-6s%sL D" % (no, acc, con.code, side)).ljust(rnd.randint(81, 120)))
        elif k == "huge":
            self.emit(*good, raw="N%08d" % no + "X" * rnd.randint(250, 600))
        elif k == "type":
            self.emit(rnd.choice("XQZ "), *good[1:])
        elif k == "ono":
            self.emit("N", "ORD%05d" % (no % 100000), *good[2:])
        elif k == "ono0":
            self.emit("N", 0, *good[2:])
        elif k == "time":
            self.emit(*good, tm=rnd.choice(("0915A1", "      ", "09:15 ")))
        elif k == "time2":
            self.emit(*good, tm=rnd.choice(("256100", "096000", "091561")))
        elif k == "seq":
            self.emit(*good, tm=self.hms(self.clock() - 600))
        elif k == "acct":
            self.emit("N", no, rnd.choice(("ZZZ999", "KVT999", "      ", "hgc001")), *good[3:])
        elif k == "susp":
            self.emit("N", no, rnd.choice(SUSPENDED), *good[3:])
        elif k == "prod":
            self.emit("N", no, acc, rnd.choice(("OAT", "XXX", "crn")) + con.code[3:], *good[4:])
        elif k == "month":
            self.emit("N", no, acc, con.code[:3] + rnd.choice("ABW1") + con.code[4:], *good[4:])
        elif k == "year":
            self.emit("N", no, acc, con.code[:4] + rnd.choice(("2X", "  ", "A7")), *good[4:])
        elif k == "nolist" and self.unlisted:
            self.emit("N", no, acc, rnd.choice(self.unlisted), *good[4:])
        elif k == "expired":
            ym = self.by * 12 + self.bm - 1 - rnd.choice((1, 2, 12, 25))
            letter = rnd.choice([m for m in "HKNUZ"])
            self.emit("N", no, acc, "CRN%s%02d" % (letter, (ym // 12 - 1) % 100), *good[4:])
        elif k == "ltd" and self.past_ltd:
            self.emit("N", no, acc, rnd.choice(self.past_ltd), *good[4:])
        elif k == "fwd":
            self.emit("N", no, acc, "%s%s%02d" % (con.pc, con.code[3], (self.by + rnd.choice((2, 3, 30))) % 100),
                      *good[4:])
        elif k == "side":
            self.emit("N", no, acc, con.code, rnd.choice("XbL "), *good[5:])
        elif k == "otype":
            self.emit("N", no, acc, con.code, side, rnd.choice("XlP "), *good[6:])
        elif k == "tif":
            self.emit("N", no, acc, con.code, side, "L", rnd.choice("GXd "), *good[7:])
        elif k == "combo" and not con.last_day:
            self.emit("N", no, acc, con.code, side, rnd.choice("ST"), rnd.choice("IF"), px,
                      px, q)
        elif k == "qty0":
            self.emit(*good[:9], 0)
        elif k == "qtyx":
            self.emit(*good[:9], rnd.choice(("   10", "1O   ", "-0005", "00 20")))
        elif k == "lot":
            c2 = rnd.choice([c for c in self.active if c.lot > 1 and not c.spot])
            self.emit("N", no, self.account(), c2.code, side, "L", "D",
                      c2.mid - c2.tick * (2 if side == "B" else -2), 0, c2.lot * rnd.randint(1, 9) + 1)
        elif k == "maxq":
            self.emit(*good[:9], con.maxq * (2 if con.spot else 1) + con.lot * rnd.choice((1, 1, 5, 200)))
        elif k == "spotq":
            sp = [c for c in self.active if c.spot]
            if sp:
                c2 = rnd.choice(sp)
                self.emit("N", no, rnd.choice(NOT_SMALL), c2.code, side, "L", "D",
                          c2.mid - c2.tick * (3 if side == "B" else -3), 0, c2.maxq + c2.lot)
            else:
                self.emit(*good[:9], con.maxq + con.lot)
        elif k == "pxx":
            self.emit(*good[:7], rnd.choice(("  45250", "45250.0", "004525O")), 0, q)
        elif k == "px0":
            self.emit(*good[:7], 0, 0, q)
        elif k == "mktpx" and not con.last_day:
            self.emit("N", no, acc, con.code, side, "M", "D", px, 0, q)
        elif k == "tick":
            c2 = rnd.choice([c for c in self.active if c.tick > 1])
            self.emit("N", no, self.account(c2), c2.code, side, "L", "D" if not pre else "O",
                      c2.mid + rnd.randint(1, c2.tick - 1), 0, c2.lot)
        elif k == "lup":
            self.emit(*good[:7], con.ref + con.lim + con.tick * rnd.choice((1, 1, 2, 40)), 0, q)
        elif k == "ldn":
            self.emit(*good[:7], max(con.tick, con.ref - con.lim - con.tick * rnd.choice((1, 1, 2, 40))), 0, q)
        elif k == "stp0" and not con.last_day:
            self.emit("N", no, acc, con.code, side, "S", "D", 0,
                      rnd.choice((0, "  STOP ", con.ref + con.lim + con.tick)), q)
        elif k == "stptick" and con.tick > 1 and not con.last_day:
            self.emit("N", no, acc, con.code, side, "S", "D", 0, con.mid + 1, q)
        elif k == "stplim" and not con.last_day:
            d = 1 if side == "B" else -1
            sp = con.mid + d * con.tick * 6
            self.emit("N", no, acc, con.code, side, "T", "D", sp - d * con.tick * rnd.randint(1, 3), sp, q)
        elif k == "stptrig" and not con.last_day:
            d = 1 if side == "B" else -1
            self.emit("N", no, acc, con.code, side, "S", "D", 0, con.mid - d * con.tick * rnd.randint(4, 12), q)
        elif k == "auc":
            self.emit(*good[:6], "O", px, 0, q)
        elif k == "lastday":
            ld = [c for c in self.active if c.last_day]
            if ld:
                self.emit("N", no, rnd.choice(NOT_SMALL), ld[0].code, side, "M", "D", 0, 0, ld[0].lot * 2)
            else:
                self.emit(*good[:6], "O", px, 0, q)
        elif k == "spot3":
            sp = [c for c in self.active if c.spot]
            if sp:
                c2 = rnd.choice(sp)
                self.emit("N", no, rnd.choice(SMALL), c2.code, side, "L", "D",
                          c2.mid - c2.tick * (2 if side == "B" else -2), 0, c2.lot)
            else:
                self.emit(*good[:9], 0)
        elif k == "dup" and con.live:
            self.emit("N", con.live[-1][0], *good[2:])
        elif k == "fok":
            self.emit("N", no, rnd.choice(NOT_SMALL), con.code, side, "L", "F",
                      con.mid + con.tick * (1 if side == "B" else -1), 0, con.maxq // con.lot * con.lot)
        elif k == "pos":
            c2 = rnd.choice([c for c in self.active if c.maxq > c.plim and not c.spot])
            self.emit("N", no, rnd.choice(SMALL), c2.code, side, "L", "D",
                      c2.mid - c2.tick * (4 if side == "B" else -4), 0, c2.plim + c2.lot * rnd.randint(1, 10))
        elif k == "margin":
            self.emit("N", no, "HLW017", con.code, side, "L", "D", px, 0,
                      min(con.maxq, con.lot * rnd.randint(60, 90)))
        elif k == "nomkt" and self.far:
            c2 = rnd.choice(self.far)
            self.emit("N", no, rnd.choice(NOT_SMALL), c2.code, side, "M", rnd.choice("DIF"), 0, 0, c2.lot * 2)
        elif k == "preioc":
            self.emit(*good[:6], rnd.choice("IF"), px, 0, q)
        elif k == "prestop":
            self.emit("N", no, acc, con.code, side, "S", "D", 0, con.mid + con.tick * (5 if side == "B" else -5), q)
        elif k == "premkt":
            self.emit("N", no, acc, con.code, side, "M", "D", 0, 0, q)
        else:
            self.emit(*good[:9], 0)

    # ---- the day ----------------------------------------------------------
    def pre_open(self):
        rnd = self.rnd
        r = rnd.random()
        con = self.contract()
        if r < 0.05:
            return self.bad(pre=True)
        if r < 0.09 and con.live:
            return self.cancel(con)
        if r < 0.14 and con.live:
            return self.amend(con)
        side = rnd.choice("BS")
        acc = self.account(con)
        q = self.qty(con, acc)
        r = rnd.random()
        if r < 0.12 and not con.last_day:
            self.emit("N", self.new_no(), acc, con.code, side, "M", "O", 0, 0, min(q, 20 * con.lot))
            return
        px = con.ref + con.tick * int(round(rnd.gauss(0.6 if side == "B" else -0.6, 3.0)))
        self.limit(con, acc, side, px, q, tif="O" if r < 0.45 else "D")

    def continuous(self):
        rnd = self.rnd
        i = len(self.rows)
        for start, con, d in self.hot_plan:
            if i >= start and not con.hot:
                con.hot = d
        if self.rogue and self.rogue_at <= i < self.rogue_at + 400 and rnd.random() < 0.12:
            con = self.contract()
            self.emit("N", self.new_no(), self.rogue, con.code, rnd.choice("BS"), "L", "D",
                      con.mid + rnd.randint(1, 4) * 7 * con.lim, 0, con.lot)
            return
        if sum(1 for c in self.active if c.halted) >= 3 and rnd.random() < 0.7:
            cons = [c for c in self.active if c.live]
            if cons:
                return self.cancel(rnd.choice(cons))
        con = self.contract()
        self.drift(con)
        if con.hot and rnd.random() < 0.4:
            return self.sweep(con)
        if len(con.live) > 70 and rnd.random() < 0.4:
            return self.cancel(con)                 # desks tidy up stale orders
        r = rnd.random()
        if r < 0.050:
            self.bad()
        elif r < 0.140:
            self.cancel(con)
        elif r < 0.210:
            self.amend(con)
        elif r < 0.265:
            self.stop(con)
        elif r < 0.325:
            self.market(con, tif=rnd.choice("DDDIF"))
        elif r < 0.375:
            self.limit(con, tif="I", aggressive=True)
        elif r < 0.392:
            self.limit(con, tif="F", aggressive=True, qty=min(self.qty(con), 8 * con.lot))
        elif r < 0.412:
            self.prorata_burst()
        elif r < 0.422:
            self.self_trade()
        elif r < 0.428:
            self.quiet_book()
        elif r < 0.560:
            self.limit(con, aggressive=True)
        else:
            self.limit(con)

    def run(self):
        n = self.count
        tail = 2 if n >= 50 else 0
        if n >= 50:
            con = self.active[0]
            for _ in range(2):          # sent before the pre-open starts
                self.emit("N", self.new_no(), self.account(con), con.code, "B", "L", "D", con.ref, 0,
                          con.lot, tm=self.hms(PRE_OPEN - self.rnd.randint(1, 900)))
        while len(self.rows) < n - tail:
            if len(self.rows) < self.n_pre:
                self.pre_open()
            else:
                self.continuous()
        del self.rows[n - tail:]
        for _ in range(tail):           # still arriving after the close
            con = self.contract()
            self.emit("N", self.new_no(), self.account(con), con.code, "S", "L", "D", con.mid, 0,
                      con.lot, tm=self.hms(CLOSE + self.rnd.randint(0, 600)))
        return self.rows


def qty_hash(rows):
    h = 0
    for r in rows:
        if len(r) == 80 and r[0] in "NAC" and r[38:43].isascii() and r[38:43].isdigit():
            h += int(r[38:43])
    return h % 10 ** 10


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--out", default="data/ORDERS.DAT")
    ap.add_argument("--date", type=int, default=20261116, help="business date CCYYMMDD")
    ap.add_argument("--trailer", choices=("ok", "none", "bad"), default="ok")
    args = ap.parse_args()

    rows = Gen(args.count, args.seed, args.date).run()
    with open(args.out, "w", newline="\n") as f:
        for r in rows:
            f.write(r + "\n")
        if args.trailer != "none":
            n = len(rows) + (3 if args.trailer == "bad" else 0)
            f.write(("T%07d%010d" % (n, qty_hash(rows))).ljust(80) + "\n")
        if args.seed % 9 == 4:
            # a late record from the feed after the trailer was cut
            f.write(rows[-1] + "\n")
    print("wrote %d records to %s" % (len(rows), args.out))


if __name__ == "__main__":
    main()
