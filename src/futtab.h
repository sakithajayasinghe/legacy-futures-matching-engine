/*
 * FUTTAB.H
 * PORT HALDEN COMMODITY AND FUTURES EXCHANGE
 * PRODUCT MASTER AND CLEARING ACCOUNT MASTER
 *
 * THESE TABLES ARE COMPILED IN.  TO CHANGE A PRODUCT OR ADD AN ACCOUNT
 * EDIT THIS FILE AND REBUILD (SEE OPERATIONS BINDER SECTION 7).
 * OPCON.C CARRIES ITS OWN COPY OF THE PRODUCT TABLE - KEEP IN STEP.
 *
 * 94-03-14 RJT  GRAINS
 * 95-11-02 RJT  COPPER, GOLD ADDED (CR-95-017)
 * 96-06-20 MKO  RUBBER ADDED, 5-LOT CONTRACT (CR-96-009)
 * 97-09-08 MKO  CRUDE OIL ADDED (CR-97-031)
 * 99-01-25 DSW  ACCOUNT TIERS AND COLLATERAL FROM CLEARING (CR-99-004)
 * 01-10-01 DSW  SOYBEAN LIMIT WIDENED 5000 TO 7000 (CR-01-052)
 * 03-02-17 PLN  FEES REVISED PER BOARD CIRCULAR 03/02 (CR-03-006)
 */
#ifndef FUTTAB_H
#define FUTTAB_H

struct PRDREC {
    char cd[4];         /* product code                              */
    char nm[12];        /* short name                                */
    char alg;           /* F=FIFO  P=PRO-RATA                        */
    int  lot;           /* round lot                                 */
    long tick;          /* tick size in price points                 */
    long tval;          /* tick value, cents per contract            */
    long bset;          /* previous settlement, front reference      */
    long carry;         /* carry per month out, points               */
    long lim;           /* daily limit, points either side           */
    long cb;            /* circuit breaker, points from reference    */
    long prot;          /* market order protection, points           */
    int  maxq;          /* max order qty                             */
    int  plim;          /* base position limit                       */
    long im;            /* initial margin, cents per contract        */
    int  fee;           /* fee, cents per contract per side          */
    int  ltd;           /* last trading day of delivery month        */
    char mths[13];      /* listed delivery months                    */
    int  topcap;        /* TOP order priority cap                    */
};

#define NPRD 7

static struct PRDREC prdtab[NPRD] = {
/*   cd     name         alg lot tick  tval    bset   carry  lim   cb    prot maxq plim  im      fee ltd  months          top */
    {"CRN", "CORN",      'P', 1,  25L, 1250L,  45250L, 110L, 3000L, 2000L, 500L, 500, 300, 135000L, 120, 14, "HKNUZ",        20},
    {"WHT", "WHEAT",     'P', 1,  25L, 1250L,  61875L, 160L, 4500L, 3000L, 750L, 400, 250, 180000L, 120, 14, "HKNUZ",        20},
    {"SOY", "SOYBEANS",  'P', 1,  25L, 1250L, 118650L, -90L, 7000L, 4500L,1000L, 400, 250, 270000L, 130, 13, "FHKNQUX",      15},
    {"RBR", "RUBBER",    'P', 5,   5L,  500L,  16840L,  35L,  900L,  600L, 100L, 250, 200,  60000L,  85, 16, "FGHJKMNQUVXZ", 25},
    {"CPR", "COPPER",    'F', 2,   5L, 1250L,  38545L,  20L, 2000L, 1300L, 250L, 300, 200, 310000L, 150, 26, "HKNUZ",         0},
    {"GLD", "GOLD",      'F', 1,  10L, 1000L,  35240L,  45L, 2500L, 1600L, 300L, 300, 200, 250000L, 155, 26, "GJMQVZ",        0},
    {"CRD", "CRUDE OIL", 'F', 1,   1L, 1000L,   2145L,  -3L,  150L,  100L,  25L, 600, 400, 220000L, 145, 20, "FGHJKMNQUVXZ",  0}
};

struct ACCREC {
    char id[7];         /* clearing account                          */
    char nm[24];        /* name                                      */
    char tier;          /* 1,2,3                                     */
    char typ;           /* H=HEDGER S=SPECULATOR M=MARKET MAKER      */
    char stat;          /* A=ACTIVE S=SUSPENDED                      */
    long coll;          /* collateral on deposit, cents              */
};

#define NACC 20

static struct ACCREC acctab[NACC] = {
    {"HGC001", "HALDEN GRAIN CO-OP",     '1', 'H', 'A', 4000000000L},
    {"NRM002", "NORTHREACH MILLING",     '1', 'H', 'A', 3000000000L},
    {"PQF003", "PORT QUAY FEEDS",        '2', 'H', 'A', 1200000000L},
    {"TSR004", "TESSARA REFINING",       '1', 'H', 'A', 3500000000L},
    {"BLM005", "BELLMOOR METALS",        '2', 'H', 'A', 1500000000L},
    {"KVT006", "KAVETT TRADING",         '1', 'M', 'A', 6000000000L},
    {"ORL007", "ORLEIGH MARKETS",        '1', 'M', 'A', 6000000000L},
    {"DRX008", "DRAXWELL SECURITIES",    '1', 'M', 'A', 5000000000L},
    {"FNC009", "FENCASTLE CAPITAL",      '2', 'S', 'A', 1000000000L},
    {"WYB010", "WYBOURNE FUTURES",       '2', 'S', 'A',  900000000L},
    {"STK011", "STANNICK AND ROWE",      '2', 'S', 'A',  800000000L},
    {"MRL012", "MERLOW PARTNERS",        '2', 'S', 'A',  600000000L},
    {"QTN013", "QUINTON LOCALS",         '3', 'S', 'A',  150000000L},
    {"EBR014", "EBRIDGE TRADERS",        '3', 'S', 'A',  120000000L},
    {"LSK015", "LOSKER COMMODITIES",     '3', 'S', 'A',   80000000L},
    {"TVY016", "TAVEY BROTHERS",         '3', 'S', 'A',   50000000L},
    {"HLW017", "HOLLOWAY PIT TRADING",   '3', 'S', 'A',    2500000L},
    {"CDN018", "CADENHAM FUND",          '2', 'S', 'S',  700000000L},
    {"ARV019", "ARVELLE EXPORT",         '2', 'H', 'A', 1100000000L},
    {"JSP020", "JESPER LOCALS",          '3', 'S', 'S',    1000000L}
};

/* DELIVERY MONTH LETTERS, JAN..DEC */
static char mthcod[13] = "FGHJKMNQUVXZ";

#endif
