/*
 * FUTMATCH.C
 *
 * PORT HALDEN COMMODITY AND FUTURES EXCHANGE
 * END OF SESSION ORDER MATCHING RUN
 *
 * READS THE DAY'S ORDER FILE IN ARRIVAL SEQUENCE, RUNS THE OPENING
 * UNCROSS AND THE CONTINUOUS SESSION, AND WRITES FILLS, REJECTS, THE
 * CLOSING BOOK, SETTLEMENT PRICES AND VARIATION, THE SESSION REPORT
 * AND THE AUDIT TRAIL.
 *
 *      FUTMATCH ORDERS.DAT OUTDIR
 *
 * BUSINESS DATE IS TAKEN FROM THE ENVIRONMENT (BUSDATE=CCYYMMDD), SET
 * BY THE JOB SCRIPT FROM THE DATE CARD.  NO DATE CARD = TODAY.
 *
 * RETURN CODES   0  CLEAN
 *                4  COMPLETED, RECORDS REJECTED
 *                8  COMPLETED, CONTROL TOTALS DO NOT AGREE
 *               16  ABORTED
 *
 * CHANGE LOG
 * ----------
 * 94-03-14 RJT  V1.0 INITIAL VERSION.  GRAINS, LIMIT AND MARKET, FIFO.
 * 94-07-05 RJT  V1.1 REJECT FILE CARRIES RECORD IMAGE (CR-94-011)
 * 95-02-20 RJT  V1.2 DAILY PRICE LIMITS FROM PRODUCT TABLE (CR-95-003)
 * 95-11-02 RJT  V1.3 METALS.  POSITION LIMITS BY PRODUCT (CR-95-017)
 * 96-02-12 RJT  V2.0 STOP AND STOP LIMIT ORDERS (CR-96-002)
 * 96-06-20 MKO  V2.1 PRO-RATA ALLOCATION FOR GRAINS AND RUBBER, TOP
 *               ORDER PRIORITY, LOT ROUNDING (CR-96-009)
 * 96-10-07 MKO  V2.2 IOC AND FOK (CR-96-021)
 * 97-04-28 MKO  V2.3 AMEND RECORD.  QTY DOWN KEEPS PLACE (CR-97-008)
 * 97-09-08 MKO  V2.4 CRUDE.  MARKET ORDER PROTECTION (CR-97-031)
 * 98-03-16 MKO  V2.5 SELF TRADE PREVENTION AFTER CLEARING HOUSE
 *               COMPLAINT RE WASH TRADES (CR-98-006)
 * 98-11-30 DSW  V2.6 Y2K.  CENTURY WINDOW ON CONTRACT YEAR, 8 DIGIT
 *               BUSINESS DATE (CR-98-041)
 * 99-01-25 DSW  V2.7 MARGIN CHECK AGAINST COLLATERAL, TIERS (CR-99-004)
 * 99-06-14 DSW  V2.8 SETTLEMENT FILE SORTED PRODUCT/EXPIRY FOR
 *               CLEARING HOUSE (CR-99-027)
 * 00-05-15 DSW  V3.0 OPENING AUCTION.  PIT OPEN ROUTINE RETIRED
 *               (CR-00-012)
 * 00-06-26 DSW  V3.0A REFERENCE PRICE IS AN OPENING CANDIDATE WHEN BOTH
 *               SIDES HOLD MARKET ORDERS - CRUDE OPENED LIMIT DOWN ON A
 *               STRAY BID 00-06-21 (CR-00-019)
 * 00-09-04 DSW  V3.0B FOK USES CANCEL-RESTING ON OWN ORDERS SO THAT IT
 *               CAN ALWAYS COMPLETE (CR-00-030)
 * 01-03-12 DSW  V3.1 CIRCUIT BREAKER, EXCHANGE HALT (CR-01-009)
 * 01-11-19 PLN  V3.2 VOLUME DISCOUNT ON FEES, MINIMUM FEE (CR-01-061)
 * 02-04-08 PLN  V3.2A ACCOUNT LOCK AFTER EXCESS REJECTS - RUNAWAY
 *               ORDER ENTRY TERMINAL INCIDENT 02-03-27 (CR-02-014)
 * 02-08-19 PLN  V3.3 ORDER TABLE ENLARGED.  TRAILER HASH (CR-02-027)
 * 03-02-17 PLN  V3.3A SPOT MONTH: HALF MAX QTY, NO TIER 3, LIMIT ORDERS
 *               ONLY ON LAST DAY (CR-03-006, CR-03-007)
 * 03-09-29 PLN  V3.4 PORT TO LINUX.  LONG IS 64 BIT (CR-03-118)
 * 04-05-10 PLN  V3.4A DOS LINE ENDS FROM PC ORDER ENTRY (CR-04-020)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include "futmatch.h"
#include "futtab.h"

#if LONG_MAX < 2147483648L
#error FUTMATCH NEEDS 64 BIT LONG (CR-03-118)
#endif

#define VERS "V3.4A"

/* ---- FILES ---------------------------------------------------------- */
static FILE *fin, *ffil, *frej, *fbok, *fstl, *frpt, *faud;

/* ---- CURRENT RECORD ------------------------------------------------- */
static char rec[LBUFSZ];
static int  rlen;
static long nrec;               /* records read                         */
static int  rcls;               /* 0 other 1 new 2 amend 3 cancel       */
static int  curacc;             /* account index of current record      */
static long curono;             /* order number of current record       */

/* ---- SESSION STATE -------------------------------------------------- */
static long busdt;
static int  by, bm, bd;
static int  phase;              /* 0 pre-open  1 continuous             */
static long clk;                /* session clock HHMMSS                 */
static int  xhalt, nhalt;
static int  trlseen;
static long trlcnt, trlhsh, qhash;
static int  ctlerr;
static long gseq;               /* priority sequence                    */
static long ntrd;               /* trade number                         */
static int  trgflg;             /* current aggressor is triggered stop  */
static int  rc;

/* ---- ORDER TABLE ---------------------------------------------------- */
static struct OREC otab[MAXORD];
static int  ofree[MAXORD], nfree, nlive;
static int  hbkt[HSZ];

/* ---- CONTRACT TABLE ------------------------------------------------- */
static int  ncon;
static char c_cd[MAXCON][7];
static int  c_prd[MAXCON], c_yy[MAXCON], c_mi[MAXCON], c_md[MAXCON];
static long c_ref[MAXCON];
static int  c_bhd[MAXCON], c_ahd[MAXCON], c_shd[MAXCON], c_stl[MAXCON];
static long c_opn[MAXCON], c_hi[MAXCON], c_lo[MAXCON], c_lst[MAXCON];
static long c_set[MAXCON], c_vol[MAXCON], c_ntr[MAXCON];
static int  c_hlt[MAXCON];
static char c_mth[MAXCON];
static long c_lpx[MAXCON][NSETL], c_lq[MAXCON][NSETL];
static int  cix[MAXCON];        /* sorted index for output              */

/* ---- ACCOUNT STATE -------------------------------------------------- */
static long a_rej[NACC], a_vol[NACC], a_fee[NACC], a_nfl[NACC];
static int  a_crj[NACC], a_lck[NACC];  /* run of rejected new orders, lock */
static long p_bq[NACC][MAXCON], p_sq[NACC][MAXCON];
static long p_bv[NACC][MAXCON], p_sv[NACC][MAXCON];
static long p_ob[NACC][MAXCON], p_os[NACC][MAXCON];

/* ---- TIER TABLES (TIER 1,2,3) --------------------------------------- */
static int mrgpct[3] = { 100, 125, 150 };
static int posmul[3] = { 5, 2, 1 };

/* ---- COUNTERS ------------------------------------------------------- */
static long n_new, n_amd, n_cxl, n_rej, n_rjn, n_rja, n_rjc, n_rjx, n_trl;
static long n_ful, n_kil, n_sti, n_str, n_exp, n_trg, n_auc, v_auc;
static long t_vol, t_fee;
static long rjc[NREJ];

static char abuf[200];

static void aggress(int s);

/* ===================================================================== */
/* UTILITIES                                                             */
/* ===================================================================== */

static void fatal(char *msg)
{
    printf("FUTMATCH - %s  RC=16\n", msg);
    exit(16);
}

/* NUMERIC FIELD.  RETURNS -1 IF ANY BYTE IS NOT A DIGIT */
static int numfld(char *p, int n, long *v)
{
    register int i;
    long x;

    x = 0;
    for (i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9')
            return -1;
        x = x * 10 + (p[i] - '0');
    }
    *v = x;
    return 0;
}

static void audit(char *ev, long ono, int ac, int cn, char *tx)
{
    fprintf(faud, "%07ld %06ld %s %08ld %-6s %-6s %s\n", nrec, clk, ev, ono,
        ac >= 0 ? acctab[ac].id : "", cn >= 0 ? c_cd[cn] : "", tx);
}

static void reject(int rx)
{
    fprintf(frej, "%07ld %s %-32s %-80.80s\n", nrec, rejtab[rx].cd,
        rejtab[rx].tx, rec);
    n_rej++;
    rjc[rx]++;
    if (curacc >= 0)
        a_rej[curacc]++;
    if (rcls == 1) n_rjn++;
    else if (rcls == 2) n_rja++;
    else if (rcls == 3) n_rjc++;
    else n_rjx++;
    sprintf(abuf, "%s %s", rejtab[rx].cd, rejtab[rx].tx);
    audit("REJ", curono, curacc, -1, abuf);
    /* CR-02-014.  A RUN OF REJECTED NEW ORDERS LOCKS THE ACCOUNT */
    if (curacc >= 0 && rcls == 1 && !a_lck[curacc]) {
        if (++a_crj[curacc] >= REJLOCK) {
            a_lck[curacc] = 1;
            sprintf(abuf, "REJECTED IN A ROW=%d", a_crj[curacc]);
            audit("LCK", 0L, curacc, -1, abuf);
        }
    }
}

/* CENTS TO DOLLARS.CC FOR THE REPORT */
static char *mny(long v)
{
    static char b[4][32];
    static int k;
    char *p;

    k = (k + 1) & 3;
    p = b[k];
    if (v < 0)
        sprintf(p, "-%ld.%02ld", (-v) / 100, (-v) % 100);
    else
        sprintf(p, "%ld.%02ld", v / 100, v % 100);
    return p;
}

/* READ NEXT RECORD.  -1 AT END OF FILE */
static int getrec(void)
{
    int n, ch;

    if (fgets(rec, LBUFSZ, fin) == NULL)
        return -1;
    n = strlen(rec);
    if (n > 0 && rec[n - 1] == '\n')
        rec[--n] = 0;
    else if (n == LBUFSZ - 1) {
        /* OVERLENGTH - THROW THE REST OF THE LINE AWAY */
        while ((ch = getc(fin)) != EOF && ch != '\n')
            ;
    }
    if (n > 0 && rec[n - 1] == '\r')    /* CR-04-020 */
        rec[--n] = 0;
    rlen = n;
    return 0;
}

/* BUSINESS DATE FROM DATE CARD OR SYSTEM CLOCK */
static void getbus(void)
{
    char *e;
    long v;
    time_t t;
    struct tm *lt;

    e = getenv("BUSDATE");
    if (e != NULL && *e != 0) {
        if (strlen(e) != 8 || numfld(e, 8, &v) < 0)
            fatal("INVALID BUSINESS DATE ON DATE CARD");
    } else {
        t = time(NULL);
        lt = localtime(&t);
        if (lt == NULL)
            fatal("SYSTEM DATE NOT AVAILABLE");
        v = (lt->tm_year + 1900L) * 10000L + (lt->tm_mon + 1) * 100L + lt->tm_mday;
    }
    busdt = v;
    by = (int)(v / 10000);
    bm = (int)(v / 100 % 100);
    bd = (int)(v % 100);
    if (by < 1970 || by > 2069 || bm < 1 || bm > 12 || bd < 1 || bd > 31)
        fatal("INVALID BUSINESS DATE ON DATE CARD");
}

/* ===================================================================== */
/* TABLE LOOKUPS                                                         */
/* ===================================================================== */

static int fndprd(char *p)
{
    register int i;

    for (i = 0; i < NPRD; i++)
        if (memcmp(p, prdtab[i].cd, 3) == 0)
            return i;
    return -1;
}

static int fndacc(char *p)
{
    register int i;

    for (i = 0; i < NACC; i++)
        if (memcmp(p, acctab[i].id, 6) == 0)
            return i;
    return -1;
}

static int fndord(long no)
{
    register int s;

    for (s = hbkt[no % HSZ]; s >= 0; s = otab[s].hn)
        if (otab[s].no == no)
            return s;
    return -1;
}

/* FIND OR OPEN A CONTRACT.  REFERENCE PRICE IS LAST SETTLEMENT PLUS   */
/* CARRY FOR THE MONTHS OUT, BROUGHT DOWN TO A TICK                    */
static int getcon(int pr, int yy, int mi, int md)
{
    register int c;
    int j;

    for (c = 0; c < ncon; c++)
        if (c_prd[c] == pr && c_yy[c] == yy && c_mi[c] == mi)
            return c;
    if (ncon >= MAXCON)
        fatal("CONTRACT TABLE FULL");
    c = ncon++;
    memcpy(c_cd[c], rec + O_CON, 6);
    c_cd[c][6] = 0;
    c_prd[c] = pr;
    c_yy[c] = yy;
    c_mi[c] = mi;
    c_md[c] = md;
    c_ref[c] = ((prdtab[pr].bset + prdtab[pr].carry * md) / prdtab[pr].tick)
        * prdtab[pr].tick;
    c_bhd[c] = c_ahd[c] = c_shd[c] = c_stl[c] = -1;
    c_opn[c] = c_hi[c] = c_lo[c] = c_lst[c] = c_set[c] = 0;
    c_vol[c] = c_ntr[c] = 0;
    c_hlt[c] = 0;
    c_mth[c] = ' ';
    for (j = 0; j < NSETL; j++)
        c_lpx[c][j] = c_lq[c][j] = 0;
    return c;
}

/* ===================================================================== */
/* ORDER TABLE AND BOOK CHAINS                                           */
/* ===================================================================== */

static int ordget(void)
{
    int s;

    s = ofree[--nfree];
    nlive++;
    return s;
}

static void ordfree(int s)
{
    int h, q, p;

    h = (int)(otab[s].no % HSZ);
    p = -1;
    for (q = hbkt[h]; q >= 0 && q != s; q = otab[q].hn)
        p = q;
    if (q == s) {
        if (p < 0) hbkt[h] = otab[s].hn;
        else otab[p].hn = otab[s].hn;
    }
    otab[s].st = S_FREE;
    ofree[nfree++] = s;
    nlive--;
}

/* BID CHAIN: HIGHEST PRICE FIRST, THEN ARRIVAL.  MARKET-ON-OPEN LEADS */
static void insbid(int s)
{
    int c, p, q;
    long k, kq;

    c = otab[s].cn;
    k = otab[s].ty == 'M' ? PXMAX : otab[s].px;
    p = -1;
    q = c_bhd[c];
    while (q >= 0) {
        kq = otab[q].ty == 'M' ? PXMAX : otab[q].px;
        if (kq < k)
            break;
        p = q;
        q = otab[q].nx;
    }
    otab[s].pv = p;
    otab[s].nx = q;
    if (p < 0) c_bhd[c] = s;
    else otab[p].nx = s;
    if (q >= 0) otab[q].pv = s;
    otab[s].st = S_REST;
    p_ob[otab[s].ac][c] += otab[s].rm;
}

/* ASK CHAIN: LOWEST PRICE FIRST, THEN ARRIVAL.  MARKET-ON-OPEN LEADS  */
static void insask(int s)
{
    int c, p, q;
    long k, kq;

    c = otab[s].cn;
    k = otab[s].ty == 'M' ? 0L : otab[s].px;
    p = -1;
    q = c_ahd[c];
    while (q >= 0) {
        kq = otab[q].ty == 'M' ? 0L : otab[q].px;
        if (kq > k)
            break;
        p = q;
        q = otab[q].nx;
    }
    otab[s].pv = p;
    otab[s].nx = q;
    if (p < 0) c_ahd[c] = s;
    else otab[p].nx = s;
    if (q >= 0) otab[q].pv = s;
    otab[s].st = S_REST;
    p_os[otab[s].ac][c] += otab[s].rm;
}

/* STOP CHAIN: ARRIVAL ORDER */
static void insstp(int s)
{
    int c;

    c = otab[s].cn;
    otab[s].nx = -1;
    otab[s].pv = c_stl[c];
    if (c_stl[c] >= 0) otab[c_stl[c]].nx = s;
    else c_shd[c] = s;
    c_stl[c] = s;
    otab[s].st = S_PARK;
    if (otab[s].sd == 'B') p_ob[otab[s].ac][c] += otab[s].rm;
    else p_os[otab[s].ac][c] += otab[s].rm;
}

/* TAKE AN ORDER OFF WHICHEVER CHAIN IT IS ON AND RELEASE ITS WORKING  */
/* QUANTITY                                                            */
static void unlnk(int s)
{
    int c, a;

    c = otab[s].cn;
    a = otab[s].ac;
    if (otab[s].pv >= 0)
        otab[otab[s].pv].nx = otab[s].nx;
    else if (otab[s].st == S_PARK)
        c_shd[c] = otab[s].nx;
    else if (otab[s].sd == 'B')
        c_bhd[c] = otab[s].nx;
    else
        c_ahd[c] = otab[s].nx;
    if (otab[s].nx >= 0)
        otab[otab[s].nx].pv = otab[s].pv;
    else if (otab[s].st == S_PARK)
        c_stl[c] = otab[s].pv;
    if (otab[s].sd == 'B') p_ob[a][c] -= otab[s].rm;
    else p_os[a][c] -= otab[s].rm;
    otab[s].nx = otab[s].pv = -1;
    otab[s].st = S_ACT;
}

/* ===================================================================== */
/* RISK                                                                  */
/* ===================================================================== */

/* POSITION PLUS WORKING ORDERS ON ONE SIDE, ALL MONTHS OF A PRODUCT   */
static long posuse(int a, int pr, char sd)
{
    register int c;
    long net, wk;

    net = wk = 0;
    for (c = 0; c < ncon; c++) {
        if (c_prd[c] != pr)
            continue;
        net += p_bq[a][c] - p_sq[a][c];
        wk += (sd == 'B') ? p_ob[a][c] : p_os[a][c];
    }
    return (sd == 'B' ? net : -net) + wk;
}

static long poslim(int a, int pr)
{
    long l;

    l = (long)prdtab[pr].plim * posmul[acctab[a].tier - '1'];
    if (acctab[a].typ == 'H')           /* HEDGE EXEMPTION CR-95-017 */
        l *= HEDGMUL;
    return l;
}

/* MARGIN REQUIREMENT.  WK=1 INCLUDES WORKING ORDERS, WORSE SIDE       */
static long mrgreq(int a, int wk)
{
    register int c;
    long net, e1, e2, tot;

    tot = 0;
    for (c = 0; c < ncon; c++) {
        net = p_bq[a][c] - p_sq[a][c];
        e1 = net;
        e2 = -net;
        if (wk) {
            e1 += p_ob[a][c];
            e2 += p_os[a][c];
        }
        if (e2 > e1)
            e1 = e2;
        tot += e1 * prdtab[c_prd[c]].im * mrgpct[acctab[a].tier - '1'] / 100;
    }
    return tot;
}

/* ===================================================================== */
/* FEES                                                                  */
/* ===================================================================== */

#ifdef OLDFEE
/* FLAT FEE AS CHARGED BEFORE CR-01-061.  KEPT FOR THE RERUN OF PRE    */
/* NOVEMBER 2001 SESSIONS IF AUDIT EVER ASK FOR ONE                    */
static long calcfee(int a, int pr, int q, int role)
{
    return (long)q * prdtab[pr].fee;
}
#else
/* ROLE 0 = TAKER, 1 = MAKER, 2 = AUCTION.  VOLUME BANDS ARE ON THE    */
/* ACCOUNT'S SESSION VOLUME BEFORE THIS FILL; A FILL THAT STRADDLES A  */
/* BAND IS SPLIT                                                       */
static long calcfee(int a, int pr, int q, int role)
{
    long v0, v1, q1, q2, q3, pct, f;

    v0 = a_vol[a];
    v1 = v0 + q;
    q1 = q2 = q3 = 0;
    if (v0 < VB1)
        q1 = (v1 < VB1 ? v1 : VB1) - v0;
    if (v1 > VB1 && v0 < VB2)
        q2 = (v1 < VB2 ? v1 : VB2) - (v0 > VB1 ? v0 : VB1);
    if (v1 > VB2)
        q3 = v1 - (v0 > VB2 ? v0 : VB2);
    pct = 100;
    if (role == 2)
        pct = AUCPCT;
    else if (role == 1 && acctab[a].typ == 'M')
        pct = MMPCT;
    f = ((q1 * VP1 + q2 * VP2 + q3 * VP3) * prdtab[pr].fee * pct + 5000L)
        / 10000L;
    if (f < FEEMIN)
        f = FEEMIN;
    return f;
}
#endif

/* ===================================================================== */
/* TRADE                                                                 */
/* ===================================================================== */

/* B = BUY ORDER SLOT, S = SELL ORDER SLOT.  AGG = B, S OR X (AUCTION) */
static void dotrd(int b, int s, int q, long px, char agg)
{
    int c, pr, ab, as, g, o;
    long fb, fs;
    char tt;

    c = otab[b].cn;
    pr = c_prd[c];
    ab = otab[b].ac;
    as = otab[s].ac;
    ntrd++;
    fb = calcfee(ab, pr, q, agg == 'X' ? 2 : (agg == 'B' ? 0 : 1));
    a_vol[ab] += q;
    fs = calcfee(as, pr, q, agg == 'X' ? 2 : (agg == 'S' ? 0 : 1));
    a_vol[as] += q;
    a_fee[ab] += fb;
    a_fee[as] += fs;
    a_nfl[ab]++;
    a_nfl[as]++;
    t_fee += fb + fs;
    t_vol += q;
    p_bq[ab][c] += q;
    p_bv[ab][c] += px * q;
    p_sq[as][c] += q;
    p_sv[as][c] += px * q;

    if (c_ntr[c] == 0) {
        c_opn[c] = c_hi[c] = c_lo[c] = px;
    } else {
        if (px > c_hi[c]) c_hi[c] = px;
        if (px < c_lo[c]) c_lo[c] = px;
    }
    c_lst[c] = px;
    c_lpx[c][c_ntr[c] % NSETL] = px;
    c_lq[c][c_ntr[c] % NSETL] = q;
    c_ntr[c]++;
    c_vol[c] += q;

    tt = 'C';
    if (agg == 'X') {
        tt = 'A';
        n_auc++;
        v_auc += q;
    } else if (trgflg)
        tt = 'S';
    fprintf(ffil, "%07ld%-6s%07ld%05d%08ld%-6s%08ld%-6s%c%c%07ld%07ld%06ld%07ld\n",
        ntrd, c_cd[c], px, q, otab[b].no, acctab[ab].id, otab[s].no,
        acctab[as].id, agg, tt, fb, fs, clk, nrec);

    g = (agg == 'S') ? s : b;
    o = (agg == 'S') ? b : s;
    sprintf(abuf, "T=%07ld PX=%07ld Q=%05d VS=%08ld", ntrd, px, q, otab[o].no);
    audit("TRD", otab[g].no, otab[g].ac, c, abuf);

    otab[b].rm -= q;
    otab[b].fl += q;
    otab[s].rm -= q;
    otab[s].fl += q;
}

/* FILL A RESTING ORDER R AGAINST INCOMING ORDER S */
static void fillrst(int r, int s, int q, long px)
{
    int c, a;

    c = otab[r].cn;
    a = otab[r].ac;
    if (otab[s].sd == 'B') {
        dotrd(s, r, q, px, 'B');
        p_os[a][c] -= q;
    } else {
        dotrd(r, s, q, px, 'S');
        p_ob[a][c] -= q;
    }
    if (otab[r].rm == 0) {
        unlnk(r);
        ordfree(r);
        n_ful++;
    }
}

/* SELF TRADE - TAKE THE RESTING ORDER OUT */
static void cxlrst(int r, int s)
{
    sprintf(abuf, "Q=%05d SELF TRADE BY=%08ld", otab[r].rm, otab[s].no);
    audit("STR", otab[r].no, otab[r].ac, otab[r].cn, abuf);
    unlnk(r);
    ordfree(r);
    n_str++;
}

/* ===================================================================== */
/* CIRCUIT BREAKER                                                       */
/* ===================================================================== */

static void chkcb(int c)
{
    long d;

    if (c_hlt[c] || c_ntr[c] == 0)
        return;
    d = c_lst[c] - c_ref[c];
    if (d < 0)
        d = -d;
    if (d < prdtab[c_prd[c]].cb)
        return;
    c_hlt[c] = 1;
    nhalt++;
    sprintf(abuf, "LAST=%07ld REF=%07ld", c_lst[c], c_ref[c]);
    audit("HLT", 0L, -1, c, abuf);
    if (nhalt >= XHALTN && !xhalt) {
        xhalt = 1;
        sprintf(abuf, "CONTRACTS HALTED=%d", nhalt);
        audit("XHL", 0L, -1, -1, abuf);
    }
}

#if 0
/* PIT SESSION LUNCH BREAK 1200-1300.  ORDERS TIMED IN THE BREAK WERE  */
/* HELD FOR THE AFTERNOON CALL.  ELECTRONIC SESSION HAS NO BREAK       */
/* (CR-00-012) BUT THE FLOOR COMMITTEE MAY WANT IT BACK - DO NOT       */
/* DELETE.  DSW                                                        */
static int inlunch(long tm)
{
    if (tm >= 120000L && tm < 130000L)
        return 1;
    return 0;
}

static void pitopen(int c)
{
    /* OPEN AT THE REFERENCE PRICE, NO UNCROSS */
    c_opn[c] = c_ref[c];
}
#endif

/* ===================================================================== */
/* CONTINUOUS MATCHING                                                   */
/* ===================================================================== */

/* PRO-RATA AT ONE PRICE LEVEL.  RETURNS 1 IF THE INCOMING ORDER WAS   */
/* KILLED BY SELF TRADE PREVENTION                                     */
static int prolvl(int s, long lpx, int stpm)
{
    static int lv[MAXORD], al[MAXORD], pa[MAXORD];
    int c, n, i, k, r, own, lot;
    long q, l, q0, x;

    c = otab[s].cn;
    lot = prdtab[c_prd[c]].lot;
    n = own = 0;
    r = (otab[s].sd == 'B') ? c_ahd[c] : c_bhd[c];
    while (r >= 0 && otab[r].px == lpx) {
        lv[n++] = r;
        if (otab[r].ac == otab[s].ac)
            own++;
        r = otab[r].nx;
    }
    if (own) {
        if (!stpm) {
            /* OWN ORDER SOMEWHERE AT THIS PRICE - NOTHING TRADES HERE */
            for (i = 0; otab[lv[i]].ac != otab[s].ac; i++)
                ;
            sprintf(abuf, "Q=%05d SELF TRADE VS=%08ld", otab[s].rm, otab[lv[i]].no);
            audit("STI", otab[s].no, otab[s].ac, c, abuf);
            return 1;
        }
        for (i = 0, k = 0; i < n; i++) {
            if (otab[lv[i]].ac == otab[s].ac)
                cxlrst(lv[i], s);
            else
                lv[k++] = lv[i];
        }
        n = k;
        if (n == 0)
            return 0;
    }

    q = otab[s].rm;
    for (i = 0; i < n; i++)
        al[i] = pa[i] = 0;

    /* 1. TOP ORDER FIRST, UP TO THE CAP */
    if (otab[lv[0]].tp) {
        x = otab[lv[0]].rm;
        if (x > prdtab[c_prd[c]].topcap) x = prdtab[c_prd[c]].topcap;
        if (x > q) x = q;
        al[0] = (int)x;
        q -= x;
    }
    l = 0;
    for (i = 0; i < n; i++)
        l += otab[lv[i]].rm - al[i];

    if (q >= l) {
        /* 2. ENOUGH TO CLEAR THE LEVEL */
        for (i = 0; i < n; i++)
            al[i] = otab[lv[i]].rm;
    } else if (q > 0) {
        /* 3. SHARE BY SIZE, WHOLE LOTS, SMALL SHARES DROPPED */
        q0 = q;
        for (i = 0; i < n; i++) {
            x = q0 * (otab[lv[i]].rm - al[i]) / l;
            x -= x % lot;
            if (x < MINALLOC)
                x = 0;
            pa[i] = (int)x;
            q -= x;
        }
        /* 4. WHAT IS LEFT GOES IN ARRIVAL ORDER */
        for (i = 0; i < n && q > 0; i++) {
            x = otab[lv[i]].rm - al[i] - pa[i];
            if (x > q) x = q;
            pa[i] += (int)x;
            q -= x;
        }
        for (i = 0; i < n; i++)
            al[i] += pa[i];
    }
    for (i = 0; i < n; i++)
        if (al[i] > 0)
            fillrst(lv[i], s, al[i], lpx);
    return 0;
}

/* QUANTITY AN INCOMING FOK COULD TAKE FROM OTHER ACCOUNTS */
static long fokavl(int c, int a, char sd, int mkt, long px)
{
    int r, pr;
    long pref, rpx, tot;

    pr = c_prd[c];
    pref = c_ntr[c] ? c_lst[c] : c_ref[c];
    tot = 0;
    r = (sd == 'B') ? c_ahd[c] : c_bhd[c];
    for (; r >= 0; r = otab[r].nx) {
        rpx = otab[r].px;
        if (mkt) {
            if (sd == 'B' ? rpx > pref + prdtab[pr].prot : rpx < pref - prdtab[pr].prot)
                break;
        } else {
            if (sd == 'B' ? rpx > px : rpx < px)
                break;
        }
        if (otab[r].ac != a)
            tot += otab[r].rm;
    }
    return tot;
}

/* RUN AN INCOMING ORDER AGAINST THE BOOK, THEN REST OR KILL WHAT IS   */
/* LEFT.  THE SLOT MAY BE FREE ON RETURN                               */
static void aggress(int s)
{
    int c, pr, r, mkt, stpm, kil, h, q;
    long pref, rpx;
    char sd;

    c = otab[s].cn;
    pr = c_prd[c];
    sd = otab[s].sd;
    mkt = (otab[s].ty == 'M' || otab[s].ty == 'S');
    pref = c_ntr[c] ? c_lst[c] : c_ref[c];
    /* 1 = CANCEL RESTING, 0 = CANCEL INCOMING.  CR-98-006, CR-00-030 */
    stpm = (otab[s].tf == 'F' || acctab[otab[s].ac].typ == 'M');
    kil = 0;
    otab[s].st = S_ACT;

mloop:
    if (otab[s].rm == 0)
        goto mdone;
    r = (sd == 'B') ? c_ahd[c] : c_bhd[c];
    if (r < 0)
        goto mdone;
    rpx = otab[r].px;
    if (mkt) {
        if (sd == 'B' ? rpx > pref + prdtab[pr].prot : rpx < pref - prdtab[pr].prot)
            goto mdone;
    } else {
        if (sd == 'B' ? rpx > otab[s].px : rpx < otab[s].px)
            goto mdone;
    }
    if (prdtab[pr].alg == 'P') {
        if (prolvl(s, rpx, stpm)) {
            kil = 1;
            goto mdone;
        }
        goto mloop;
    }
    /* FIFO */
    if (otab[r].ac == otab[s].ac) {
        if (stpm) {
            cxlrst(r, s);
            goto mloop;
        }
        sprintf(abuf, "Q=%05d SELF TRADE VS=%08ld", otab[s].rm, otab[r].no);
        audit("STI", otab[s].no, otab[s].ac, c, abuf);
        kil = 1;
        goto mdone;
    }
    q = otab[s].rm < otab[r].rm ? otab[s].rm : otab[r].rm;
    fillrst(r, s, q, rpx);
    goto mloop;

mdone:
    if (kil) {
        n_sti++;
        ordfree(s);
    } else if (otab[s].rm == 0) {
        n_ful++;
        ordfree(s);
    } else if (mkt || otab[s].tf == 'I') {
        sprintf(abuf, "Q=%05d UNFILLED REMAINDER", otab[s].rm);
        audit("KIL", otab[s].no, otab[s].ac, c, abuf);
        n_kil++;
        ordfree(s);
    } else {
        /* REST.  FIRST ORDER TO BETTER THE MARKET IS THE TOP ORDER */
        otab[s].tp = 0;
        if (prdtab[pr].alg == 'P') {
            h = (sd == 'B') ? c_bhd[c] : c_ahd[c];
            if (h < 0 || (sd == 'B' ? otab[s].px > otab[h].px : otab[s].px < otab[h].px)) {
                for (; h >= 0; h = otab[h].nx)
                    otab[h].tp = 0;
                otab[s].tp = 1;
            }
        }
        if (sd == 'B') insbid(s);
        else insask(s);
        sprintf(abuf, "PX=%07ld Q=%05d PRI=%09ld%s", otab[s].px, otab[s].rm,
            otab[s].pr, otab[s].tp ? " TOP" : "");
        audit("RST", otab[s].no, otab[s].ac, c, abuf);
    }
    chkcb(c);
}

/* TRIGGER STOPS.  OLDEST STOP WHOSE PRICE HAS BEEN REACHED GOES       */
/* FIRST; AFTER IT HAS RUN THE CHAIN IS SEARCHED AGAIN FROM THE TOP    */
static void runstp(int c)
{
    int s;
    long lp;

again:
    if (c_hlt[c] || c_ntr[c] == 0)
        return;
    lp = c_lst[c];
    for (s = c_shd[c]; s >= 0; s = otab[s].nx) {
        if ((otab[s].sd == 'B' && lp >= otab[s].sp)
         || (otab[s].sd == 'S' && lp <= otab[s].sp)) {
            unlnk(s);
            otab[s].pr = ++gseq;
            n_trg++;
            sprintf(abuf, "SP=%07ld LAST=%07ld", otab[s].sp, lp);
            audit("TRG", otab[s].no, otab[s].ac, c, abuf);
            trgflg = 1;
            aggress(s);
            trgflg = 0;
            goto again;
        }
    }
}

/* ===================================================================== */
/* OPENING AUCTION                                                       */
/* ===================================================================== */

static void uncross(int c)
{
    static long cand[MAXORD];
    int nc, i, j, b, s, q, nx;
    long p, bq, sq, m, im, d, bp, bm_, bi, bdist;

    if (c_bhd[c] < 0 && c_ahd[c] < 0)
        return;

    /* CANDIDATE PRICES ARE THE LIMITS ON THE BOOK */
    nc = 0;
    for (j = 0; j < 2; j++) {
        for (s = j ? c_ahd[c] : c_bhd[c]; s >= 0; s = otab[s].nx) {
            if (otab[s].ty == 'M')
                continue;
            for (i = 0; i < nc; i++)
                if (cand[i] == otab[s].px)
                    break;
            if (i == nc)
                cand[nc++] = otab[s].px;
        }
    }
    /* MARKET ORDERS ON BOTH SIDES CAN ALWAYS MEET AT THE REFERENCE, SO */
    /* IT IS A CANDIDATE AS WELL.  A STRAY LIMIT USED TO SET THE OPEN - */
    /* CR-00-019.  MARKET ORDERS LEAD THEIR CHAIN                       */
    if (c_bhd[c] >= 0 && c_ahd[c] >= 0 && otab[c_bhd[c]].ty == 'M'
        && otab[c_ahd[c]].ty == 'M') {
        for (i = 0; i < nc; i++)
            if (cand[i] == c_ref[c])
                break;
        if (i == nc)
            cand[nc++] = c_ref[c];
    }

    bp = 0;
    bm_ = 0;
    bi = 0;
    bdist = 0;
    for (i = 0; i < nc; i++) {
        p = cand[i];
        bq = sq = 0;
        for (s = c_bhd[c]; s >= 0; s = otab[s].nx)
            if (otab[s].ty == 'M' || otab[s].px >= p)
                bq += otab[s].rm;
        for (s = c_ahd[c]; s >= 0; s = otab[s].nx)
            if (otab[s].ty == 'M' || otab[s].px <= p)
                sq += otab[s].rm;
        m = bq < sq ? bq : sq;
        im = bq < sq ? sq - bq : bq - sq;
        d = p < c_ref[c] ? c_ref[c] - p : p - c_ref[c];
        if (m == 0)
            continue;
        /* MOST VOLUME, THEN LEAST IMBALANCE, THEN NEAREST REFERENCE, */
        /* THEN THE HIGHER PRICE                                      */
        if (m > bm_ || (m == bm_ && (im < bi || (im == bi && (d < bdist
            || (d == bdist && p > bp)))))) {
            bm_ = m;
            bi = im;
            bdist = d;
            bp = p;
        }
    }

    if (bm_ == 0) {
        audit("UNX", 0L, -1, c, "NO CROSS");
        goto left;
    }
    for (;;) {
        b = c_bhd[c];
        s = c_ahd[c];
        if (b < 0 || s < 0)
            break;
        if (otab[b].ty != 'M' && otab[b].px < bp)
            break;
        if (otab[s].ty != 'M' && otab[s].px > bp)
            break;
        q = otab[b].rm < otab[s].rm ? otab[b].rm : otab[s].rm;
        dotrd(b, s, q, bp, 'X');
        p_ob[otab[b].ac][c] -= q;
        p_os[otab[s].ac][c] -= q;
        if (otab[b].rm == 0) {
            unlnk(b);
            ordfree(b);
            n_ful++;
        }
        if (otab[s].rm == 0) {
            unlnk(s);
            ordfree(s);
            n_ful++;
        }
    }
    sprintf(abuf, "PX=%07ld VOL=%07ld IMB=%07ld", bp, bm_, bi);
    audit("UNX", 0L, -1, c, abuf);

left:
    /* AT-THE-OPEN ORDERS DO NOT CARRY INTO THE SESSION */
    for (j = 0; j < 2; j++) {
        for (s = j ? c_ahd[c] : c_bhd[c]; s >= 0; s = nx) {
            nx = otab[s].nx;
            if (otab[s].ty != 'M' && otab[s].tf != 'O')
                continue;
            sprintf(abuf, "Q=%05d UNFILLED AT OPEN", otab[s].rm);
            audit("EXP", otab[s].no, otab[s].ac, c, abuf);
            unlnk(s);
            ordfree(s);
            n_exp++;
        }
    }
    chkcb(c);
}

static void doopen(void)
{
    register int c;

    for (c = 0; c < ncon; c++)
        uncross(c);
    phase = 1;
    audit("OPN", 0L, -1, -1, "CONTINUOUS TRADING");
}

/* ===================================================================== */
/* NEW ORDER                                                             */
/* ===================================================================== */

static void donew(void)
{
    struct PRDREC *p;
    int rx, pr, mi, md, c, s, a, q, mq, spot;
    long yy, px, sp, v, lo, hi, lref;
    char sd, ty, tf, *mp;

    a = curacc;

    /* ---- CONTRACT ---- */
    pr = fndprd(rec + O_CON);
    if (pr < 0) { rx = X_PRD; goto bad; }
    p = &prdtab[pr];
    mp = strchr(mthcod, rec[O_CON + 3]);
    if (mp == NULL || numfld(rec + O_CON + 4, 2, &yy) < 0) { rx = X_MTH; goto bad; }
    mi = (int)(mp - mthcod);
    if (strchr(p->mths, rec[O_CON + 3]) == NULL) { rx = X_NLST; goto bad; }
    yy += (yy < Y2KPIV) ? 2000 : 1900;          /* CR-98-041 */
    md = (int)(yy * 12 + mi) - (by * 12 + bm - 1);
    if (md < 0) { rx = X_EXP; goto bad; }
    if (md == 0 && bd > p->ltd) { rx = X_LTD; goto bad; }
    if (md > MAXFWD) { rx = X_FWD; goto bad; }
    c = getcon(pr, (int)yy, mi, md);
    if (c_hlt[c]) { rx = X_CHLT; goto bad; }

    /* ---- SIDE, TYPE, TIME IN FORCE ---- */
    sd = rec[O_SID];
    ty = rec[O_OTY];
    tf = rec[O_TIF];
    if (sd != 'B' && sd != 'S') { rx = X_SIDE; goto bad; }
    if (ty != 'L' && ty != 'M' && ty != 'S' && ty != 'T') { rx = X_OTY; goto bad; }
    if (tf != 'D' && tf != 'I' && tf != 'F' && tf != 'O') { rx = X_TIF; goto bad; }
    if ((ty == 'S' || ty == 'T') && tf != 'D') { rx = X_COMB; goto bad; }

    /* ---- SESSION PHASE AND DELIVERY MONTH ---- */
    if (phase == 0) {
        if (!((ty == 'L' && (tf == 'D' || tf == 'O')) || (ty == 'M' && tf == 'O'))) {
            rx = X_PRE;
            goto bad;
        }
    } else if (tf == 'O') {
        rx = X_AUC;
        goto bad;
    }
    spot = (md == 0);
    if (spot && bd == p->ltd && ty != 'L') { rx = X_LAST; goto bad; }
    if (spot && acctab[a].tier == '3') { rx = X_SPOT; goto bad; }

    /* ---- QUANTITY ---- */
    if (numfld(rec + O_QTY, 5, &v) < 0 || v == 0) { rx = X_QTY; goto bad; }
    q = (int)v;
    if (q % p->lot != 0) { rx = X_LOT; goto bad; }
    mq = p->maxq;
    if (spot)
        mq = mq / 2;                            /* CR-03-006 */
    if (q > mq) { rx = X_MAXQ; goto bad; }

    /* ---- PRICE ---- */
    lo = c_ref[c] - p->lim;
    hi = c_ref[c] + p->lim;
    if (numfld(rec + O_PX, 7, &px) < 0) { rx = X_PX; goto bad; }
    if (ty == 'L' || ty == 'T') {
        if (px == 0) { rx = X_PX; goto bad; }
        if (px % p->tick != 0) { rx = X_TICK; goto bad; }
        if (px > hi) { rx = X_LUP; goto bad; }
        if (px < lo) { rx = X_LDN; goto bad; }
    } else if (px != 0) {
        rx = X_MPX;
        goto bad;
    }

    /* ---- STOP ---- */
    sp = 0;
    if (ty == 'S' || ty == 'T') {
        if (numfld(rec + O_STP, 7, &sp) < 0 || sp == 0 || sp % p->tick != 0
            || sp > hi || sp < lo) {
            rx = X_STP;
            goto bad;
        }
        if (ty == 'T' && (sd == 'B' ? px < sp : px > sp)) { rx = X_STPL; goto bad; }
        lref = c_ntr[c] ? c_lst[c] : c_ref[c];
        if (sd == 'B' ? sp <= lref : sp >= lref) { rx = X_STPT; goto bad; }
    }

    /* ---- BOOK ---- */
    if (fndord(curono) >= 0) { rx = X_DUP; goto bad; }
    if (nlive >= MAXORD) { rx = X_FULL; goto bad; }

    /* ---- RISK ---- */
    if (posuse(a, pr, sd) + q > poslim(a, pr)) { rx = X_POS; goto bad; }
    if (sd == 'B') p_ob[a][c] += q;
    else p_os[a][c] += q;
    v = mrgreq(a, 1);
    if (sd == 'B') p_ob[a][c] -= q;
    else p_os[a][c] -= q;
    if (v > acctab[a].coll) { rx = X_MRG; goto bad; }

    /* ---- LIQUIDITY ---- */
    if (phase == 1 && ty == 'M' && (sd == 'B' ? c_ahd[c] : c_bhd[c]) < 0) {
        rx = X_NOMK;
        goto bad;
    }
    if (tf == 'F' && fokavl(c, a, sd, ty == 'M', px) < q) { rx = X_FOK; goto bad; }

    /* ---- ACCEPTED ---- */
    s = ordget();
    otab[s].no = curono;
    otab[s].ac = a;
    otab[s].cn = c;
    otab[s].sd = sd;
    otab[s].ty = ty;
    otab[s].tf = tf;
    otab[s].st = S_ACT;
    otab[s].px = px;
    otab[s].sp = sp;
    otab[s].rm = q;
    otab[s].fl = 0;
    otab[s].pr = ++gseq;
    otab[s].tm = clk;
    otab[s].tp = 0;
    otab[s].nx = otab[s].pv = -1;
    otab[s].hn = hbkt[curono % HSZ];
    hbkt[curono % HSZ] = s;
    n_new++;
    a_crj[a] = 0;
    sprintf(abuf, "%c %c %c PX=%07ld SP=%07ld Q=%05d", sd, ty, tf, px, sp, q);
    audit("NEW", curono, a, c, abuf);

    if (ty == 'S' || ty == 'T') {
        insstp(s);
        sprintf(abuf, "SP=%07ld Q=%05d", sp, q);
        audit("PRK", curono, a, c, abuf);
        return;
    }
    if (phase == 0) {
        if (sd == 'B') insbid(s);
        else insask(s);
        sprintf(abuf, "PX=%07ld Q=%05d PRI=%09ld", px, q, otab[s].pr);
        audit("RST", curono, a, c, abuf);
        return;
    }
    aggress(s);
    runstp(c);
    return;

bad:
    reject(rx);
}

/* ===================================================================== */
/* AMEND                                                                 */
/* ===================================================================== */

static void doamd(void)
{
    struct PRDREC *p;
    int rx, s, c, a, nq, keep;
    long v, px, np, lo, hi, oq, opx;

    a = curacc;
    s = fndord(curono);
    if (s < 0) { rx = X_NFND; goto bad; }
    if (otab[s].ac != a) { rx = X_OWN; goto bad; }
    c = otab[s].cn;
    p = &prdtab[c_prd[c]];
    if (memcmp(rec + O_CON, c_cd[c], 6) != 0) { rx = X_CMIS; goto bad; }
    if (c_hlt[c]) { rx = X_CHLT; goto bad; }
    if (otab[s].st == S_PARK) { rx = X_SAMD; goto bad; }

    /* ZERO IN A FIELD MEANS LEAVE IT AS IT IS */
    if (numfld(rec + O_QTY, 5, &v) < 0) { rx = X_QTY; goto bad; }
    nq = v ? (int)v : otab[s].rm;
    if (numfld(rec + O_PX, 7, &px) < 0) { rx = X_PX; goto bad; }
    np = px ? px : otab[s].px;
    if (otab[s].ty == 'M' && px != 0) { rx = X_MPX; goto bad; }
    if (nq == otab[s].rm && np == otab[s].px) { rx = X_NOCH; goto bad; }

    if (nq % p->lot != 0) { rx = X_LOT; goto bad; }
    if (nq > p->maxq) { rx = X_MAXQ; goto bad; }
    if (np != otab[s].px) {
        lo = c_ref[c] - p->lim;
        hi = c_ref[c] + p->lim;
        if (np % p->tick != 0) { rx = X_TICK; goto bad; }
        if (np > hi) { rx = X_LUP; goto bad; }
        if (np < lo) { rx = X_LDN; goto bad; }
    }
    if (nq > otab[s].rm) {
        /* ONLY THE INCREASE IS NEW RISK */
        if (posuse(a, c_prd[c], otab[s].sd) + (nq - otab[s].rm) > poslim(a, c_prd[c])) {
            rx = X_POS;
            goto bad;
        }
        if (otab[s].sd == 'B') p_ob[a][c] += nq - otab[s].rm;
        else p_os[a][c] += nq - otab[s].rm;
        v = mrgreq(a, 1);
        if (otab[s].sd == 'B') p_ob[a][c] -= nq - otab[s].rm;
        else p_os[a][c] -= nq - otab[s].rm;
        if (v > acctab[a].coll) { rx = X_MRG; goto bad; }
    }

    /* ---- ACCEPTED ---- */
    oq = otab[s].rm;
    opx = otab[s].px;
    keep = (np == opx && nq < oq);              /* CR-97-008 */
    n_amd++;
    sprintf(abuf, "PX=%07ld>%07ld Q=%05ld>%05d %s", opx, np, oq, nq,
        keep ? "KEEP" : "LOSE");
    audit("AMD", curono, a, c, abuf);
    if (keep) {
        if (otab[s].sd == 'B') p_ob[a][c] -= oq - nq;
        else p_os[a][c] -= oq - nq;
        otab[s].rm = nq;
        return;
    }
    unlnk(s);
    otab[s].px = np;
    otab[s].rm = nq;
    otab[s].tp = 0;
    otab[s].pr = ++gseq;
    if (phase == 0) {
        if (otab[s].sd == 'B') insbid(s);
        else insask(s);
        sprintf(abuf, "PX=%07ld Q=%05d PRI=%09ld", np, nq, otab[s].pr);
        audit("RST", curono, a, c, abuf);
        return;
    }
    aggress(s);
    runstp(c);
    return;

bad:
    reject(rx);
}

/* ===================================================================== */
/* CANCEL                                                                */
/* ===================================================================== */

static void docxl(void)
{
    int s;

    s = fndord(curono);
    if (s < 0) {
        reject(X_NFND);
        return;
    }
    if (otab[s].ac != curacc) {
        reject(X_OWN);
        return;
    }
    sprintf(abuf, "Q=%05d", otab[s].rm);
    audit("CXL", curono, curacc, otab[s].cn, abuf);
    unlnk(s);
    ordfree(s);
    n_cxl++;
}

/* ===================================================================== */
/* TRAILER                                                               */
/* ===================================================================== */

static void dotrl(void)
{
    trlseen = 1;
    n_trl++;
    trlcnt = trlhsh = -1;
    if (numfld(rec + T_CNT, 7, &trlcnt) < 0)
        trlcnt = -1;
    if (numfld(rec + T_HSH, 10, &trlhsh) < 0)
        trlhsh = -1;
    if (trlcnt != nrec - 1)
        ctlerr |= 2;
    if (trlhsh != qhash)
        ctlerr |= 4;
    sprintf(abuf, "COUNT=%07ld HASH=%010ld", nrec - 1, qhash);
    audit("TRL", 0L, -1, -1, abuf);
}

/* ===================================================================== */
/* MAIN RECORD LOOP                                                      */
/* ===================================================================== */

static void runsess(void)
{
    long v, tm;
    int a;

nxtrec:
    if (getrec() < 0)
        goto eof;
    nrec++;
    curacc = -1;
    curono = 0;
    rcls = 0;
    if (trlseen) { reject(X_AFT); goto nxtrec; }
    if (rlen != RECLEN) { reject(X_LEN); goto nxtrec; }
    if (rec[O_TYP] == 'T') { dotrl(); goto nxtrec; }
    if (rec[O_TYP] == 'N') rcls = 1;
    else if (rec[O_TYP] == 'A') rcls = 2;
    else if (rec[O_TYP] == 'C') rcls = 3;
    else { reject(X_TYP); goto nxtrec; }

    if (numfld(rec + O_QTY, 5, &v) == 0)
        qhash = (qhash + v) % 10000000000L;

    if (numfld(rec + O_ONO, 8, &v) < 0 || v == 0) { reject(X_ONO); goto nxtrec; }
    curono = v;

    /* ---- SESSION CLOCK ---- */
    if (numfld(rec + O_TIM, 6, &tm) < 0 || tm / 10000 > 23 || tm / 100 % 100 > 59
        || tm % 100 > 59) {
        reject(X_TIM);
        goto nxtrec;
    }
    if (tm < T_PRE) { reject(X_EARLY); goto nxtrec; }
    if (tm >= T_CLS) { reject(X_LATE); goto nxtrec; }
    if (tm < clk) { reject(X_SEQ); goto nxtrec; }
    clk = tm;
    if (phase == 0 && tm >= T_OPN)
        doopen();

    if (rcls != 3 && xhalt) { reject(X_XHLT); goto nxtrec; }

    /* ---- ACCOUNT ---- */
    a = fndacc(rec + O_ACC);
    if (a < 0) { reject(X_ACC); goto nxtrec; }
    curacc = a;
    if (rcls != 3) {
        if (acctab[a].stat == 'S') { reject(X_SUSP); goto nxtrec; }
        if (a_lck[a]) { reject(X_LOCK); goto nxtrec; }
    }

    switch (rcls) {
    case 1: donew(); break;
    case 2: doamd(); break;
    case 3: docxl(); break;
    }
    goto nxtrec;

eof:
    /* NO ORDER TIMED AFTER THE OPEN - UNCROSS ANYWAY */
    if (phase == 0)
        doopen();
    if (!trlseen)
        ctlerr |= 1;
}

/* ===================================================================== */
/* END OF SESSION                                                        */
/* ===================================================================== */

/* SORT CONTRACTS PRODUCT TABLE ORDER THEN EXPIRY.  CR-99-027 */
static void srtcon(void)
{
    int i, j, t;

    for (i = 0; i < ncon; i++)
        cix[i] = i;
    for (i = 1; i < ncon; i++) {
        t = cix[i];
        for (j = i - 1; j >= 0; j--) {
            if (c_prd[cix[j]] < c_prd[t])
                break;
            if (c_prd[cix[j]] == c_prd[t] && c_md[cix[j]] < c_md[t])
                break;
            cix[j + 1] = cix[j];
        }
        cix[j + 1] = t;
    }
}

/* SETTLEMENT PRICE.  VOLUME WEIGHTED AVERAGE OF THE LAST TRADES TO    */
/* THE NEAREST TICK, HALF UP, THEN HELD INSIDE THE CLOSING BID/ASK.    */
/* A HALTED CONTRACT SETTLES AT ITS LAST TRADE.  NO TRADE = REFERENCE  */
static void dosettle(void)
{
    int c, i, n;
    long num, den, st, tk;
    char m;

    for (c = 0; c < ncon; c++) {
        tk = prdtab[c_prd[c]].tick;
        if (c_hlt[c]) {
            st = c_lst[c];
            m = 'L';
            goto setit;
        }
        if (c_ntr[c] == 0) {
            st = c_ref[c];
            m = 'R';
        } else {
            n = c_ntr[c] < NSETL ? (int)c_ntr[c] : NSETL;
            num = den = 0;
            for (i = 0; i < n; i++) {
                num += c_lpx[c][i] * c_lq[c][i];
                den += c_lq[c][i];
            }
            st = ((2 * num + den * tk) / (2 * den * tk)) * tk;
            m = 'V';
        }
        if (c_bhd[c] >= 0 && otab[c_bhd[c]].px > st) {
            st = otab[c_bhd[c]].px;
            m = 'B';
        }
        if (c_ahd[c] >= 0 && otab[c_ahd[c]].px < st) {
            st = otab[c_ahd[c]].px;
            m = 'A';
        }
setit:
        c_set[c] = st;
        c_mth[c] = m;
        sprintf(abuf, "SETTLE=%07ld %c", st, m);
        audit("STL", 0L, -1, c, abuf);
    }
}

static void wrbook(void)
{
    int i, j, c, s;

    for (i = 0; i < ncon; i++) {
        c = cix[i];
        for (j = 0; j < 3; j++) {
            s = j == 0 ? c_bhd[c] : (j == 1 ? c_ahd[c] : c_shd[c]);
            for (; s >= 0; s = otab[s].nx)
                fprintf(fbok, "%-6s%c%c%08ld%-6s%c%c%07ld%07ld%05d%05d%09ld%c%06ld\n",
                    c_cd[c], otab[s].sd, otab[s].st, otab[s].no,
                    acctab[otab[s].ac].id, otab[s].ty, otab[s].tf, otab[s].px,
                    otab[s].sp, otab[s].rm, otab[s].fl, otab[s].pr,
                    otab[s].tp ? 'T' : ' ', otab[s].tm);
        }
    }
}

/* VARIATION FOR ONE ACCOUNT IN ONE CONTRACT, CENTS */
static long varcalc(int a, int c)
{
    long pts;

    pts = c_set[c] * (p_bq[a][c] - p_sq[a][c]) - (p_bv[a][c] - p_sv[a][c]);
    return pts / prdtab[c_prd[c]].tick * prdtab[c_prd[c]].tval;
}

static long v_tot[NACC], m_req[NACC], m_call[NACC];

static void wrsettle(void)
{
    int i, a, c, np, nv;
    long net, var, tv, eq;

    np = nv = 0;
    for (i = 0; i < ncon; i++) {
        c = cix[i];
        fprintf(fstl, "P%-6s%07ld%07ld%07ld%07ld%07ld%07ld%09ld%07ld%c%c\n",
            c_cd[c], c_ref[c], c_opn[c], c_hi[c], c_lo[c], c_lst[c], c_set[c],
            c_vol[c], c_ntr[c], c_hlt[c] ? 'H' : 'O', c_mth[c]);
        np++;
    }
    for (a = 0; a < NACC; a++) {
        tv = 0;
        for (i = 0; i < ncon; i++) {
            c = cix[i];
            if (p_bq[a][c] == 0 && p_sq[a][c] == 0)
                continue;
            net = p_bq[a][c] - p_sq[a][c];
            var = varcalc(a, c);
            tv += var;
            fprintf(fstl, "V%-6s%-6s%07ld%07ld%c%07ld%c%013ld\n", acctab[a].id,
                c_cd[c], p_bq[a][c], p_sq[a][c], net < 0 ? '-' : '+',
                net < 0 ? -net : net, var < 0 ? '-' : '+', var < 0 ? -var : var);
            nv++;
        }
        v_tot[a] = tv;
    }
    /* MARGIN CALL WHERE COLLATERAL AFTER THE DAY'S VARIATION AND FEES */
    /* NO LONGER COVERS THE OVERNIGHT POSITION                         */
    for (a = 0; a < NACC; a++) {
        m_req[a] = mrgreq(a, 0);
        net = v_tot[a] - a_fee[a];
        eq = acctab[a].coll + net;
        m_call[a] = m_req[a] > eq ? m_req[a] - eq : 0;
        fprintf(fstl, "A%-6s%07ld%09ld%011ld%c%013ld%c%013ld%013ld%c%013ld\n",
            acctab[a].id, a_nfl[a], a_vol[a], a_fee[a],
            v_tot[a] < 0 ? '-' : '+', v_tot[a] < 0 ? -v_tot[a] : v_tot[a],
            net < 0 ? '-' : '+', net < 0 ? -net : net, m_req[a],
            m_call[a] > 0 ? 'Y' : 'N', m_call[a]);
    }
    fprintf(fstl, "T%05d%05d%09ld%013ld\n", np, nv, t_vol, t_fee);
}

/* ===================================================================== */
/* SESSION REPORT                                                        */
/* ===================================================================== */

static void wrrpt(void)
{
    int i, a, c;
    long nrst, nprk, tb, ts, nin, nout, npos;
    int s;

    nrst = nprk = 0;
    for (s = 0; s < MAXORD; s++) {
        if (otab[s].st == S_REST) nrst++;
        else if (otab[s].st == S_PARK) nprk++;
    }
    tb = ts = npos = 0;
    for (c = 0; c < ncon; c++) {
        nin = 0;
        for (a = 0; a < NACC; a++) {
            tb += p_bq[a][c];
            ts += p_sq[a][c];
            nin += p_bq[a][c] - p_sq[a][c];
        }
        if (nin != 0)
            npos++;
    }
    nin = n_new;
    nout = n_ful + n_cxl + n_kil + n_sti + n_str + n_exp + nrst + nprk;
    if (nin != nout)
        ctlerr |= 8;
    if (tb != ts || npos != 0)
        ctlerr |= 16;
    rc = n_rej > 0 ? 4 : 0;
    if (ctlerr)
        rc = 8;

    fprintf(frpt, "PORT HALDEN COMMODITY AND FUTURES EXCHANGE\n");
    fprintf(frpt, "FUTMATCH %-6s END OF SESSION REPORT          BUSINESS DATE %04d-%02d-%02d\n",
        VERS, by, bm, bd);
    fprintf(frpt, "========================================================================\n\n");

    fprintf(frpt, "INPUT\n");
    fprintf(frpt, "  RECORDS READ ................ %9ld\n", nrec);
    fprintf(frpt, "  NEW ORDERS ACCEPTED ......... %9ld\n", n_new);
    fprintf(frpt, "  NEW ORDERS REJECTED ......... %9ld\n", n_rjn);
    fprintf(frpt, "  AMENDS ACCEPTED ............. %9ld\n", n_amd);
    fprintf(frpt, "  AMENDS REJECTED ............. %9ld\n", n_rja);
    fprintf(frpt, "  CANCELS ACCEPTED ............ %9ld\n", n_cxl);
    fprintf(frpt, "  CANCELS REJECTED ............ %9ld\n", n_rjc);
    fprintf(frpt, "  OTHER RECORDS REJECTED ...... %9ld\n", n_rjx);
    fprintf(frpt, "  TRAILER RECORDS ............. %9ld\n\n", n_trl);

    fprintf(frpt, "ORDER DISPOSITION\n");
    fprintf(frpt, "  FULLY FILLED ................ %9ld\n", n_ful);
    fprintf(frpt, "  CANCELLED BY REQUEST ........ %9ld\n", n_cxl);
    fprintf(frpt, "  UNFILLED REMAINDER KILLED ... %9ld\n", n_kil);
    fprintf(frpt, "  SELF TRADE, INCOMING KILLED . %9ld\n", n_sti);
    fprintf(frpt, "  SELF TRADE, RESTING KILLED .. %9ld\n", n_str);
    fprintf(frpt, "  EXPIRED AT THE OPEN ......... %9ld\n", n_exp);
    fprintf(frpt, "  RESTING AT CLOSE ............ %9ld\n", nrst);
    fprintf(frpt, "  STOPS NOT TRIGGERED ......... %9ld\n\n", nprk);

    fprintf(frpt, "ACTIVITY\n");
    fprintf(frpt, "  TRADES ...................... %9ld\n", ntrd);
    fprintf(frpt, "  CONTRACTS TRADED ............ %9ld\n", t_vol);
    fprintf(frpt, "  OF WHICH AT THE OPEN ........ %9ld  IN %ld TRADES\n", v_auc, n_auc);
    fprintf(frpt, "  STOPS TRIGGERED ............. %9ld\n", n_trg);
    fprintf(frpt, "  CONTRACTS HALTED ............ %9d\n", nhalt);
    fprintf(frpt, "  EXCHANGE HALT ............... %9s\n", xhalt ? "YES" : "NO");
    fprintf(frpt, "  TOTAL FEES .................. %15s\n\n", mny(t_fee));

    fprintf(frpt, "REJECTS BY REASON\n");
    for (i = 0; i < NREJ; i++)
        if (rjc[i] > 0)
            fprintf(frpt, "  %s %-32s %9ld\n", rejtab[i].cd, rejtab[i].tx, rjc[i]);
    if (n_rej == 0)
        fprintf(frpt, "  NONE\n");
    fprintf(frpt, "  TOTAL                                 %9ld\n\n", n_rej);

    fprintf(frpt, "CONTRACT SUMMARY\n");
    fprintf(frpt, "CONTRACT     REF    OPEN    HIGH     LOW    LAST  SETTLE M    VOLUME  TRADES ST\n");
    for (i = 0; i < ncon; i++) {
        c = cix[i];
        fprintf(frpt, "%-6s   %7ld %7ld %7ld %7ld %7ld %7ld %c %9ld %7ld %s\n",
            c_cd[c], c_ref[c], c_opn[c], c_hi[c], c_lo[c], c_lst[c], c_set[c],
            c_mth[c], c_vol[c], c_ntr[c], c_hlt[c] ? "HALTED" : "OPEN");
    }
    if (ncon == 0)
        fprintf(frpt, "  NONE\n");
    fprintf(frpt, "\n");

    fprintf(frpt, "ACCOUNT SUMMARY\n");
    fprintf(frpt, "ACCOUNT NAME                     FILLS    VOLUME         FEES      VARIATION     MARGIN REQ    MARGIN CALL\n");
    for (a = 0; a < NACC; a++) {
        if (a_nfl[a] == 0 && a_rej[a] == 0)
            continue;
        fprintf(frpt, "%-6s  %-22s %7ld %9ld %12s %14s", acctab[a].id, acctab[a].nm,
            a_nfl[a], a_vol[a], mny(a_fee[a]), mny(v_tot[a]));
        fprintf(frpt, " %14s %14s%s\n", mny(m_req[a]), mny(m_call[a]),
            a_lck[a] ? "  LOCKED" : "");
    }
    fprintf(frpt, "\n");

    fprintf(frpt, "CONTROL TOTALS\n");
    if (!trlseen)
        fprintf(frpt, "  TRAILER ..................... MISSING\n");
    else {
        fprintf(frpt, "  TRAILER COUNT ............... FILE %10ld  READ %10ld  %s\n",
            trlcnt, nrec - n_trl - rjc[X_AFT], (ctlerr & 2) ? "MISMATCH" : "OK");
        fprintf(frpt, "  QUANTITY HASH ............... FILE %10ld  READ %10ld  %s\n",
            trlhsh, qhash, (ctlerr & 4) ? "MISMATCH" : "OK");
    }
    fprintf(frpt, "  ORDER BALANCE ............... IN   %10ld  OUT  %10ld  %s\n",
        nin, nout, (ctlerr & 8) ? "OUT OF BALANCE" : "OK");
    fprintf(frpt, "  BOUGHT / SOLD ............... BUY  %10ld  SELL %10ld  %s\n",
        tb, ts, (ctlerr & 16) ? "OUT OF BALANCE" : "OK");
    fprintf(frpt, "\nRETURN CODE %d\n", rc);
}

/* ===================================================================== */

static FILE *opnout(char *dir, char *nm)
{
    static char path[512];
    FILE *f;

    sprintf(path, "%s/%s", dir, nm);
    f = fopen(path, "w");
    if (f == NULL) {
        printf("FUTMATCH - CANNOT OPEN %s\n", path);
        fatal("OUTPUT FILE ERROR");
    }
    return f;
}

int main(int argc, char **argv)
{
    int i;

    if (argc != 3)
        fatal("USAGE: FUTMATCH ORDERS-FILE OUTPUT-DIR");
    if (strlen(argv[2]) > 400)
        fatal("OUTPUT DIRECTORY NAME TOO LONG");
    getbus();
    fin = fopen(argv[1], "r");
    if (fin == NULL)
        fatal("CANNOT OPEN ORDER FILE");
    ffil = opnout(argv[2], "FILLS.DAT");
    frej = opnout(argv[2], "REJECTS.DAT");
    fbok = opnout(argv[2], "BOOK.DAT");
    fstl = opnout(argv[2], "SETTLE.DAT");
    frpt = opnout(argv[2], "SESSION.RPT");
    faud = opnout(argv[2], "AUDIT.LOG");

    for (i = 0; i < MAXORD; i++)
        ofree[i] = MAXORD - 1 - i;
    nfree = MAXORD;
    for (i = 0; i < HSZ; i++)
        hbkt[i] = -1;
    curacc = -1;

    sprintf(abuf, "BUSDATE=%08ld %s", busdt, VERS);
    audit("SOS", 0L, -1, -1, abuf);

    runsess();
    srtcon();
    dosettle();
    wrbook();
    wrsettle();
    wrrpt();

    sprintf(abuf, "RC=%d", rc);
    audit("EOS", 0L, -1, -1, abuf);

    fclose(fin);
    if (fclose(ffil) || fclose(frej) || fclose(fbok) || fclose(fstl)
        || fclose(frpt) || fclose(faud))
        fatal("OUTPUT FILE ERROR ON CLOSE");
    printf("FUTMATCH - SESSION COMPLETE  RECORDS=%ld TRADES=%ld REJECTS=%ld  RC=%d\n",
        nrec, ntrd, n_rej, rc);
    return rc;
}
