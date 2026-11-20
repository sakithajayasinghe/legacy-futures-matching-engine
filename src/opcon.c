/*
 * OPCON.C
 *
 * PORT HALDEN COMMODITY AND FUTURES EXCHANGE
 * FUTMATCH OPERATOR CONSOLE
 *
 * LINE MODE SO THAT IT RUNS ON THE MACHINE ROOM CONSOLE, A VT TERMINAL
 * OR A TELNET SESSION FROM THE FLOOR SUPERVISOR'S PC.  NO CURSES.
 * READS THE FILES OF THE SELECTED SESSION DIRECT - THERE IS NO DATABASE.
 *
 * CHANGE LOG
 * 96-03-04 RJT  V1.0 REPORT, FILLS, REJECTS
 * 96-09-16 MKO  V1.1 BOOK BY CONTRACT, PRICE LADDER
 * 97-10-13 MKO  V1.2 PRICE LIMIT INQUIRY FOR THE FLOOR SUPERVISOR.
 *               PRODUCT TABLE COPIED FROM FUTTAB.H (CR-97-040)
 * 99-02-08 DSW  V2.0 Y2K.  ACCOUNT POSITIONS FROM SETTLE FILE
 * 00-06-05 DSW  V2.1 RUN NEW SESSION FROM THE CONSOLE (CR-00-015)
 * 01-10-01 DSW  V2.1A SOYBEAN LIMIT 7000 (CR-01-052)
 * 03-02-17 PLN  V2.2 SPOT MONTH HALF MAX QTY SHOWN (CR-03-006)
 * 03-09-29 PLN  V2.3 LINUX.  SESSION HISTORY, NO PAGING WHEN NOT A TTY
 *               SO THE NIGHT SCRIPT CAN DRIVE IT (CR-03-118)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>

#define VERS    "V2.3"
#define DEFHOME "/opt/futmatch"
#define PAGELN  20
#define MAXSESS 30

/* COPY OF THE PRODUCT TABLE - SEE FUTTAB.H.  ONLY WHAT THE INQUIRY NEEDS */
struct LPRD {
    char cd[4];
    long tick;
    long bset;
    long carry;
    long lim;
    long cb;
    int  maxq;
    int  ltd;
    char mths[13];
};

#define NLPRD 7

static struct LPRD lprd[NLPRD] = {
    {"CRN", 25L,  45250L, 110L, 3000L, 2000L, 500, 14, "HKNUZ"},
    {"WHT", 25L,  61875L, 160L, 4500L, 3000L, 400, 14, "HKNUZ"},
    {"SOY", 25L, 118650L, -90L, 7000L, 4500L, 400, 13, "FHKNQUX"},
    {"RBR",  5L,  16840L,  35L,  900L,  600L, 250, 16, "FGHJKMNQUVXZ"},
    {"CPR",  5L,  38545L,  20L, 2000L, 1300L, 300, 26, "HKNUZ"},
    {"GLD", 10L,  35240L,  45L, 2500L, 1600L, 300, 26, "GJMQVZ"},
    {"CRD",  1L,   2145L,  -3L,  150L,  100L, 600, 20, "FGHJKMNQUVXZ"}
};

static char home[200];          /* installation directory               */
static char sess[40];           /* selected session                     */
static char lin[512];           /* file line                            */
static char ans[82];            /* operator input                       */
static int  tty;
static int  nln;                /* lines since last page break          */
static int  stop;               /* operator asked to stop the listing   */

/* READ ONE LINE FROM THE OPERATOR.  -1 AT END OF INPUT */
static int rdans(char *prompt)
{
    int n;

    printf("%s", prompt);
    fflush(stdout);
    if (fgets(ans, sizeof ans, stdin) == NULL) {
        ans[0] = 0;
        return -1;
    }
    n = strlen(ans);
    while (n > 0 && (ans[n - 1] == '\n' || ans[n - 1] == '\r' || ans[n - 1] == ' '))
        ans[--n] = 0;
    for (n = 0; ans[n]; n++)
        ans[n] = toupper((unsigned char)ans[n]);
    if (!tty)
        printf("%s\n", ans);    /* ECHO FOR THE LOG WHEN SCRIPTED */
    return 0;
}

/* UNSIGNED NUMERIC FIELD OF N BYTES */
static long fnum(char *p, int n)
{
    long x;

    x = 0;
    while (n-- > 0 && *p >= '0' && *p <= '9')
        x = x * 10 + (*p++ - '0');
    return x;
}

/* PRINT WITH PAGE BREAKS ON A TERMINAL */
static void out(char *s)
{
    if (stop)
        return;
    printf("%s\n", s);
    if (!tty)
        return;
    if (++nln >= PAGELN) {
        nln = 0;
        if (rdans("-- MORE --  ENTER TO CONTINUE, Q TO STOP ") < 0 || ans[0] == 'Q')
            stop = 1;
    }
}

static FILE *opnses(char *nm)
{
    static char path[512];
    FILE *f;

    if (sess[0] == 0) {
        printf("NO SESSION ON FILE.  USE OPTION 8 TO RUN ONE.\n");
        return NULL;
    }
    sprintf(path, "%s/output/%s/%s", home, sess, nm);
    f = fopen(path, "r");
    if (f == NULL)
        printf("CANNOT OPEN %s\n", path);
    nln = 0;
    stop = 0;
    return f;
}

static int getlin(FILE *f)
{
    int n;

    if (fgets(lin, sizeof lin, f) == NULL)
        return -1;
    n = strlen(lin);
    if (n > 0 && lin[n - 1] == '\n')
        lin[n - 1] = 0;
    return 0;
}

/* SESSION DIRECTORIES ARE CCYYMMDD_HHMMSS.  NEWEST FIRST */
static char slist[MAXSESS][20];
static int  nsess;

static void ldsess(void)
{
    static char path[300];
    DIR *d;
    struct dirent *e;
    int i, j, k;
    char t[20];

    nsess = 0;
    sprintf(path, "%s/output", home);
    d = opendir(path);
    if (d == NULL)
        return;
    while ((e = readdir(d)) != NULL) {
        if (strlen(e->d_name) != 15 || e->d_name[8] != '_')
            continue;
        for (i = 0; i < 15; i++)
            if (i != 8 && !isdigit((unsigned char)e->d_name[i]))
                break;
        if (i < 15)
            continue;
        strcpy(t, e->d_name);
        /* KEEP THE LIST IN DESCENDING ORDER, DROP THE OLDEST */
        for (j = 0; j < nsess && strcmp(slist[j], t) > 0; j++)
            ;
        if (j >= MAXSESS)
            continue;
        k = nsess < MAXSESS ? nsess : MAXSESS - 1;
        for (; k > j; k--)
            strcpy(slist[k], slist[k - 1]);
        strcpy(slist[j], t);
        if (nsess < MAXSESS)
            nsess++;
    }
    closedir(d);
}

/* ---- 1. SESSION REPORT ---------------------------------------------- */
static void dorpt(void)
{
    FILE *f;

    if ((f = opnses("SESSION.RPT")) == NULL)
        return;
    while (getlin(f) == 0)
        out(lin);
    fclose(f);
}

/* ---- 2. FILLS ------------------------------------------------------- */
static void dofil(void)
{
    FILE *f;
    char o[120];
    long n, v;

    if (rdans("CONTRACT (ENTER = ALL) ==> ") < 0)
        return;
    if ((f = opnses("FILLS.DAT")) == NULL)
        return;
    out("TRADE   CONTR    PRICE   QTY BUY ORD  BUYER  SELL ORD SELLER A T  TIME");
    n = v = 0;
    while (getlin(f) == 0) {
        if (strlen(lin) < 82)
            continue;
        if (ans[0] && strncmp(lin + 7, ans, strlen(ans)) != 0)
            continue;
        sprintf(o, "%.7s %.6s %.7s %.5s %.8s %.6s %.8s %.6s %c %c %.6s",
            lin, lin + 7, lin + 13, lin + 20, lin + 25, lin + 33, lin + 39,
            lin + 47, lin[53], lin[54], lin + 69);
        out(o);
        n++;
        v += fnum(lin + 20, 5);
    }
    fclose(f);
    stop = 0;
    printf("%ld TRADES LISTED, %ld CONTRACTS\n", n, v);
}

/* ---- 3. REJECTS ----------------------------------------------------- */
static void dorej(void)
{
    FILE *f;
    char o[140];
    long n;

    if (rdans("REASON CODE (ENTER = ALL) ==> ") < 0)
        return;
    if ((f = opnses("REJECTS.DAT")) == NULL)
        return;
    out("RECORD  CODE REASON                           RECORD IMAGE");
    n = 0;
    while (getlin(f) == 0) {
        if (strlen(lin) < 46)
            continue;
        if (ans[0] && strncmp(lin + 8, ans, strlen(ans)) != 0)
            continue;
        sprintf(o, "%.45s %.49s", lin, lin + 46);
        out(o);
        n++;
    }
    fclose(f);
    stop = 0;
    printf("%ld REJECTS LISTED\n", n);
}

/* ---- 4. BOOK BY CONTRACT -------------------------------------------- */
/* ONE LINE PER PRICE LEVEL.  BOOK.DAT IS ALREADY IN PRICE ORDER         */
static void dobok(void)
{
    FILE *f;
    char o[100], key[9], cur[9], hd;
    long q, n, tot;
    int pass;

    if (rdans("CONTRACT ==> ") < 0 || ans[0] == 0)
        return;
    for (pass = 0; pass < 3; pass++) {
        if ((f = opnses("BOOK.DAT")) == NULL)
            return;
        hd = pass == 0 ? 'B' : 'S';
        out(pass == 0 ? "BIDS      PRICE     QTY ORDERS" :
            pass == 1 ? "OFFERS    PRICE     QTY ORDERS" :
                        "STOPS     ORDER    ACCOUNT S    STOP   LIMIT   QTY");
        cur[0] = 0;
        q = n = tot = 0;
        while (getlin(f) == 0) {
            if (strlen(lin) < 64 || strncmp(lin, ans, 6) != 0)
                continue;
            if (pass == 2) {
                if (lin[7] != 'P')
                    continue;
                sprintf(o, "          %.8s %.6s  %c %.7s %.7s %.5s", lin + 8, lin + 16,
                    lin[6], lin + 31, lin + 24, lin + 38);
                out(o);
                tot++;
                continue;
            }
            if (lin[7] != 'R' || lin[6] != hd)
                continue;
            memcpy(key, lin + 24, 7);
            key[7] = 0;
            if (strcmp(key, cur) != 0) {
                if (cur[0]) {
                    sprintf(o, "        %s %7ld %6ld", cur, q, n);
                    out(o);
                }
                strcpy(cur, key);
                q = n = 0;
            }
            q += fnum(lin + 38, 5);
            n++;
            tot++;
        }
        if (cur[0]) {
            sprintf(o, "        %s %7ld %6ld", cur, q, n);
            out(o);
        }
        if (tot == 0)
            out("          NONE");
        fclose(f);
    }
    stop = 0;
}

/* ---- 5. ACCOUNT POSITIONS ------------------------------------------- */
static void dopos(void)
{
    FILE *f;
    char o[120];
    int hit;

    if (rdans("ACCOUNT ==> ") < 0 || ans[0] == 0)
        return;
    if ((f = opnses("SETTLE.DAT")) == NULL)
        return;
    out("CONTRACT  BOUGHT    SOLD      NET       VARIATION (CENTS)");
    hit = 0;
    while (getlin(f) == 0) {
        if (strncmp(lin + 1, ans, 6) != 0)
            continue;
        if (lin[0] == 'V' && strlen(lin) >= 49) {
            sprintf(o, "%.6s   %.7s %.7s %.8s  %.14s", lin + 7, lin + 13, lin + 20,
                lin + 27, lin + 35);
            out(o);
            hit = 1;
        } else if (lin[0] == 'A' && strlen(lin) >= 89) {
            stop = 0;
            printf("FILLS %.7s  VOLUME %.9s  FEES (CENTS) %.11s\n", lin + 7, lin + 14, lin + 23);
            printf("VARIATION %.14s  NET %.14s\n", lin + 34, lin + 48);
            printf("MARGIN REQUIRED %.13s  CALL %c %.13s\n", lin + 62, lin[75], lin + 76);
            hit = 1;
        }
    }
    fclose(f);
    stop = 0;
    if (!hit)
        printf("NO SUCH ACCOUNT\n");
}

/* ---- 6. SETTLEMENT PRICES ------------------------------------------- */
static void doset(void)
{
    FILE *f;
    char o[120];

    if ((f = opnses("SETTLE.DAT")) == NULL)
        return;
    out("CONTRACT     REF    OPEN    HIGH     LOW    LAST  SETTLE    VOLUME  TRADES S M");
    while (getlin(f) == 0) {
        if (lin[0] != 'P' || strlen(lin) < 67)
            continue;
        sprintf(o, "%.6s   %.7s %.7s %.7s %.7s %.7s %.7s %.9s %.7s %c %c", lin + 1,
            lin + 7, lin + 14, lin + 21, lin + 28, lin + 35, lin + 42, lin + 49,
            lin + 58, lin[65], lin[66]);
        out(o);
    }
    fclose(f);
    stop = 0;
}

/* ---- 7. PRICE LIMIT INQUIRY ----------------------------------------- */
/* WORKS THE DAY'S LIMITS OUT FOR A CONTRACT THE SAME WAY THE BATCH      */
/* DOES.  BUSINESS DATE COMES FROM THE HEADING OF THE SESSION REPORT     */
static void dolim(void)
{
    FILE *f;
    struct LPRD *p;
    char *mp, *dp;
    static char mcod[13] = "FGHJKMNQUVXZ";
    int i, by, bm, bd, yy, mi, md, mq;
    long ref;

    if (rdans("CONTRACT ==> ") < 0 || ans[0] == 0)
        return;
    by = bm = bd = 0;
    if ((f = opnses("SESSION.RPT")) == NULL)
        return;
    while (getlin(f) == 0) {
        dp = strstr(lin, "BUSINESS DATE ");
        if (dp != NULL && sscanf(dp + 14, "%4d-%2d-%2d", &by, &bm, &bd) == 3)
            break;
    }
    fclose(f);
    if (by == 0) {
        printf("BUSINESS DATE NOT FOUND ON SESSION REPORT\n");
        return;
    }
    if (strlen(ans) != 6) {
        printf("CONTRACT IS PPPMYY, E.G. CRNH27\n");
        return;
    }
    p = NULL;
    for (i = 0; i < NLPRD; i++)
        if (strncmp(ans, lprd[i].cd, 3) == 0)
            p = &lprd[i];
    if (p == NULL) {
        printf("UNKNOWN PRODUCT\n");
        return;
    }
    mp = strchr(mcod, ans[3]);
    if (mp == NULL || !isdigit((unsigned char)ans[4]) || !isdigit((unsigned char)ans[5])) {
        printf("INVALID CONTRACT MONTH\n");
        return;
    }
    if (strchr(p->mths, ans[3]) == NULL) {
        printf("MONTH NOT LISTED\n");
        return;
    }
    mi = (int)(mp - mcod);
    yy = (ans[4] - '0') * 10 + (ans[5] - '0');
    yy += yy < 70 ? 2000 : 1900;
    md = (yy * 12 + mi) - (by * 12 + bm - 1);
    if (md < 0 || (md == 0 && bd > p->ltd)) {
        printf("CONTRACT EXPIRED\n");
        return;
    }
    if (md > 18) {
        printf("CONTRACT NOT YET LISTED\n");
        return;
    }
    /* REFERENCE = LAST SETTLEMENT PLUS CARRY, TO THE NEAREST TICK */
    ref = ((p->bset + p->carry * md + p->tick / 2) / p->tick) * p->tick;
    mq = p->maxq;
    if (md == 0)
        mq = mq / 2;
    printf("CONTRACT %s   BUSINESS DATE %04d-%02d-%02d   MONTHS OUT %d\n", ans, by, bm, bd, md);
    printf("REFERENCE PRICE ....... %7ld\n", ref);
    printf("LIMIT UP .............. %7ld\n", ref + p->lim);
    printf("LIMIT DOWN ............ %7ld\n", ref - p->lim);
    printf("HALT ABOVE ............ %7ld\n", ref + p->cb);
    printf("HALT BELOW ............ %7ld\n", ref - p->cb);
    printf("TICK .................. %7ld\n", p->tick);
    printf("MAX ORDER QTY ......... %7d\n", mq);
    printf("LAST TRADING DAY ...... %7d%s\n", p->ltd, md == 0 ? "  (DELIVERY MONTH)" : "");
}

/* ---- 8. RUN NEW SESSION --------------------------------------------- */
static void dorun(void)
{
    static char cmd[300], id[64];
    FILE *pp;
    int rc, n;

    if (rdans("RUN A NEW SESSION NOW?  Y/N ==> ") < 0 || ans[0] != 'Y')
        return;
    printf("NEWDAY SUBMITTED - PLEASE WAIT\n");
    fflush(stdout);
    sprintf(cmd, "%s/run/NEWDAY.sh", home);
    pp = popen(cmd, "r");
    if (pp == NULL) {
        printf("CANNOT START %s\n", cmd);
        return;
    }
    id[0] = 0;
    if (fgets(id, sizeof id, pp) == NULL)
        id[0] = 0;
    rc = pclose(pp);
    n = strlen(id);
    if (n > 0 && id[n - 1] == '\n')
        id[--n] = 0;
    if (n != 15) {
        printf("NEWDAY FAILED - CALL SUPPORT\n");
        return;
    }
    strcpy(sess, id);
    printf("SESSION %s COMPLETE  %s\n", sess,
        rc == 0 ? "" : "*** JOB ENDED WITH ERROR - SEE JOB LOG ***");
    {
        FILE *f;

        if ((f = opnses("JOBLOG.TXT")) != NULL) {
            while (getlin(f) == 0)
                out(lin);
            fclose(f);
        }
    }
    stop = 0;
}

/* ---- 9. SELECT SESSION ---------------------------------------------- */
static void dosel(void)
{
    int i, k;
    char o[60];

    ldsess();
    if (nsess == 0) {
        printf("NO SESSIONS ON FILE\n");
        return;
    }
    nln = 0;
    stop = 0;
    for (i = 0; i < nsess; i++) {
        sprintf(o, " %2d  %.15s%s", i + 1, slist[i], strcmp(slist[i], sess) == 0 ? "  <== CURRENT" : "");
        out(o);
    }
    stop = 0;
    if (rdans("SESSION NUMBER ==> ") < 0 || ans[0] == 0)
        return;
    k = atoi(ans);
    if (k < 1 || k > nsess) {
        printf("NOT ON THE LIST\n");
        return;
    }
    strcpy(sess, slist[k - 1]);
}

static void menu(void)
{
    printf("\n");
    printf(" PORT HALDEN COMMODITY AND FUTURES EXCHANGE\n");
    printf(" FUTMATCH OPERATOR CONSOLE %-6s      SESSION %s\n", VERS, sess[0] ? sess : "(NONE)");
    printf(" ------------------------------------------------------------\n");
    printf("  1  SESSION REPORT             6  SETTLEMENT PRICES\n");
    printf("  2  FILLS                      7  PRICE LIMIT INQUIRY\n");
    printf("  3  REJECTS                    8  RUN NEW SESSION\n");
    printf("  4  ORDER BOOK BY CONTRACT     9  SELECT SESSION\n");
    printf("  5  ACCOUNT POSITIONS          X  EXIT\n");
}

int main(void)
{
    char *e;

    e = getenv("FUTHOME");
    if (e == NULL || *e == 0)
        e = DEFHOME;
    if (strlen(e) >= sizeof home - 1) {
        printf("OPCON - FUTHOME TOO LONG\n");
        return 16;
    }
    strcpy(home, e);
    tty = isatty(0);
    ldsess();
    if (nsess > 0)
        strcpy(sess, slist[0]);

    for (;;) {
        menu();
        if (rdans(" SELECT ==> ") < 0) {
            printf("\nEND OF INPUT - CONSOLE CLOSED\n");
            return 0;
        }
        switch (ans[0]) {
        case '1': dorpt(); break;
        case '2': dofil(); break;
        case '3': dorej(); break;
        case '4': dobok(); break;
        case '5': dopos(); break;
        case '6': doset(); break;
        case '7': dolim(); break;
        case '8': dorun(); break;
        case '9': dosel(); break;
        case 'X':
        case 'Q':
            printf("CONSOLE CLOSED\n");
            return 0;
        case 0:
            break;
        default:
            printf("INVALID SELECTION\n");
        }
    }
}
