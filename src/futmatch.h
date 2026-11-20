/*
 * FUTMATCH.H
 * PORT HALDEN COMMODITY AND FUTURES EXCHANGE
 * RECORD LAYOUTS, TABLE SIZES AND SESSION CONSTANTS FOR FUTMATCH
 *
 * 94-03-14 RJT  INITIAL
 * 96-02-12 RJT  STOP ORDERS (CR-96-002)
 * 98-11-30 DSW  Y2K - CENTURY WINDOW FOR CONTRACT YEAR (CR-98-041)
 * 00-05-15 DSW  OPENING AUCTION TIMES (CR-00-012)
 * 02-08-19 PLN  ORDER TABLE 2500 TO 6000 (CR-02-027)
 */
#ifndef FUTMATCH_H
#define FUTMATCH_H

/* ---- ORDERS.DAT, 80 BYTES FIXED, OFFSETS FROM ZERO ------------------ */
#define RECLEN  80
#define O_TYP   0       /* 1  N=NEW A=AMEND C=CANCEL T=TRAILER          */
#define O_ONO   1       /* 8  ORDER NUMBER                              */
#define O_ACC   9       /* 6  CLEARING ACCOUNT                          */
#define O_CON   15      /* 6  CONTRACT  PPPMYY                          */
#define O_SID   21      /* 1  B/S                                       */
#define O_OTY   22      /* 1  L=LIMIT M=MARKET S=STOP T=STOP LIMIT      */
#define O_TIF   23      /* 1  D=DAY I=IOC F=FOK O=AT THE OPEN           */
#define O_PX    24      /* 7  LIMIT PRICE, POINTS                       */
#define O_STP   31      /* 7  STOP PRICE, POINTS                        */
#define O_QTY   38      /* 5  QUANTITY, CONTRACTS                       */
#define O_TIM   43      /* 6  HHMMSS                                    */
                        /* 49-79 FILLER                                 */
/* TRAILER */
#define T_CNT   1       /* 7  DETAIL RECORD COUNT                       */
#define T_HSH   8       /* 10 QUANTITY HASH                             */

/* ---- TABLE SIZES ---------------------------------------------------- */
#define MAXORD  6000
#define MAXCON  96
#define HSZ     4099
#define LBUFSZ  256

/* ---- SESSION -------------------------------------------------------- */
#define T_PRE   83000L          /* PRE-OPEN STARTS                      */
#define T_OPN   90000L          /* OPENING UNCROSS                      */
#define T_CLS   143000L         /* CLOSE                                */
#define MAXFWD  18              /* MONTHS FORWARD LISTED                */
#define Y2KPIV  70              /* YY BELOW THIS IS 20YY                */
#define PXMAX   9999999L

/* ---- RISK ----------------------------------------------------------- */
#define REJLOCK 10              /* REJECTED NEW ORDERS IN A ROW = LOCK  */
#define HEDGMUL 2
#define XHALTN  3               /* CONTRACT HALTS THAT CLOSE THE FLOOR  */

/* ---- MATCHING ------------------------------------------------------- */
#define MINALLOC 2
#define NSETL   5               /* TRADES IN SETTLEMENT WINDOW          */

/* ---- FEES ----------------------------------------------------------- */
#define VB1     1000L           /* VOLUME BAND 1 TOP                    */
#define VB2     5000L           /* VOLUME BAND 2 TOP                    */
#define VP1     100
#define VP2     80
#define VP3     60
#define MMPCT   40
#define AUCPCT  50
#define FEEMIN  50L

/* ---- ORDER STATES --------------------------------------------------- */
#define S_FREE  0
#define S_ACT   'A'             /* INCOMING, BEING MATCHED              */
#define S_REST  'R'             /* ON BOOK                              */
#define S_PARK  'P'             /* STOP WAITING FOR TRIGGER             */

struct OREC {
    long no;            /* order number                                */
    int  ac;            /* account index                               */
    int  cn;            /* contract index                              */
    char sd;            /* side                                        */
    char ty;            /* type                                        */
    char tf;            /* time in force                               */
    char st;            /* state                                       */
    long px;            /* limit                                       */
    long sp;            /* stop                                        */
    int  rm;            /* remaining                                   */
    int  fl;            /* filled                                      */
    long pr;            /* priority sequence                           */
    long tm;            /* entry time                                  */
    int  tp;            /* TOP order flag                              */
    int  nx, pv;        /* book chain                                  */
    int  hn;            /* hash chain                                  */
};

/* ---- REJECT REASONS ------------------------------------------------- */
struct REJREC {
    char cd[5];
    char tx[33];
};

#define X_LEN   0
#define X_TYP   1
#define X_AFT   2
#define X_ONO   3
#define X_TIM   4
#define X_SEQ   5
#define X_EARLY 6
#define X_LATE  7
#define X_HOL   8
#define X_ACC   9
#define X_SUSP  10
#define X_LOCK  11
#define X_PRD   12
#define X_MTH   13
#define X_NLST  14
#define X_EXP   15
#define X_LTD   16
#define X_FWD   17
#define X_CHLT  18
#define X_XHLT  19
#define X_SIDE  20
#define X_OTY   21
#define X_TIF   22
#define X_COMB  23
#define X_QTY   24
#define X_LOT   25
#define X_MAXQ  26
#define X_PX    27
#define X_MPX   28
#define X_TICK  29
#define X_LUP   30
#define X_LDN   31
#define X_STP   32
#define X_STPL  33
#define X_STPT  34
#define X_PRE   35
#define X_AUC   36
#define X_LAST  37
#define X_SPOT  38
#define X_DUP   39
#define X_FULL  40
#define X_NOMK  41
#define X_FOK   42
#define X_POS   43
#define X_MRG   44
#define X_NFND  45
#define X_OWN   46
#define X_CMIS  47
#define X_SAMD  48
#define X_NOCH  49
#define NREJ    50

static struct REJREC rejtab[NREJ] = {
    {"E001", "BAD RECORD LENGTH"},
    {"E002", "UNKNOWN RECORD TYPE"},
    {"E003", "RECORD AFTER TRAILER"},
    {"E004", "INVALID ORDER NUMBER"},
    {"E005", "INVALID TIME"},
    {"E006", "TIME SEQUENCE ERROR"},
    {"E007", "BEFORE SESSION START"},
    {"E008", "AFTER SESSION CLOSE"},
    {"E009", "HOLIDAY - NO SESSION"},
    {"E010", "UNKNOWN ACCOUNT"},
    {"E011", "ACCOUNT SUSPENDED"},
    {"E012", "ACCOUNT LOCKED - EXCESS REJECTS"},
    {"E020", "UNKNOWN PRODUCT"},
    {"E021", "INVALID CONTRACT MONTH"},
    {"E022", "MONTH NOT LISTED"},
    {"E023", "CONTRACT EXPIRED"},
    {"E024", "PAST LAST TRADING DAY"},
    {"E025", "CONTRACT NOT YET LISTED"},
    {"E026", "CONTRACT HALTED"},
    {"E027", "EXCHANGE HALTED"},
    {"E030", "INVALID SIDE"},
    {"E031", "INVALID ORDER TYPE"},
    {"E032", "INVALID TIME IN FORCE"},
    {"E033", "INVALID TYPE/TIF COMBINATION"},
    {"E034", "INVALID QUANTITY"},
    {"E035", "NOT A LOT MULTIPLE"},
    {"E036", "EXCEEDS MAX ORDER QTY"},
    {"E037", "INVALID PRICE"},
    {"E038", "PRICE NOT ALLOWED ON MARKET"},
    {"E039", "PRICE NOT ON TICK"},
    {"E040", "ABOVE LIMIT UP"},
    {"E041", "BELOW LIMIT DOWN"},
    {"E042", "INVALID STOP PRICE"},
    {"E043", "STOP LIMIT PRICE INCONSISTENT"},
    {"E044", "STOP ALREADY TRIGGERED"},
    {"E050", "NOT ALLOWED IN PRE-OPEN"},
    {"E051", "AUCTION CLOSED"},
    {"E052", "LIMIT ORDERS ONLY ON LAST DAY"},
    {"E053", "SPOT MONTH NOT PERMITTED"},
    {"E060", "DUPLICATE ORDER NUMBER"},
    {"E061", "ORDER TABLE FULL"},
    {"E062", "NO MARKET"},
    {"E063", "FOK NOT FILLABLE"},
    {"E070", "POSITION LIMIT EXCEEDED"},
    {"E071", "INSUFFICIENT MARGIN"},
    {"E080", "ORDER NOT FOUND"},
    {"E081", "NOT ORDER OWNER"},
    {"E082", "CONTRACT MISMATCH"},
    {"E083", "STOP ORDER CANNOT BE AMENDED"},
    {"E084", "NO CHANGE"}
};

#endif
