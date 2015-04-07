/* Copyright (C) 2014-2015 TuxSQL.com (support@tuxsql.com)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "misc.h"
#include "tuxapi.h"

#include <unistd.h>

int tpcall_fml32(const char *svc, FBFR32 *idata, FBFR32 **odata, long flags)
{
    long olen = 0;

    check(tpcall((char *)svc, (char *)idata, 0, (char **)odata, &olen, flags) != -1,
            "Failed to call %s, %s", svc, tpstrerror(tperrno));
    return 0;
error:
    return -1;
}

int Fchg32_string(FBFR32 **fbfr, FLDID32 fieldid, FLDOCC32 oc, const char *value)
{
    for (;;) {
        if (Fchg32(*fbfr, fieldid, oc, (char *)value, 0) != -1) {
            break;
        }

        check(Ferror32 == FNOSPACE, "fml32 error: %s", Fstrerror32(Ferror32));
        check((*fbfr = (FBFR32 *)tprealloc((char *)*fbfr, Fsizeof32(*fbfr) * 2)) != NULL, "tprealloc failed");
    }
    return 0;

error:
    return -1;
}

int Fchg32_long(FBFR32 **fbfr, FLDID32 fieldid, FLDOCC32 oc, long value)
{
    for (;;) {
        if (Fchg32(*fbfr, fieldid, oc, (char *)&value, 0) != -1) {
            break;
        }

        check(Ferror32 == FNOSPACE, "fml32 error: %s", Fstrerror32(Ferror32));
        check((*fbfr = (FBFR32 *)tprealloc((char *)*fbfr, Fsizeof32(*fbfr) * 2)) != NULL, "tprealloc failed");
    }
    return 0;
error:
    return -1;
}

/* NOTE: this one does string truncation unlike the original Fget32 that fails with FNOSPACE */
int Fget32_string(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *loc, FLDLEN32 maxlen)
{
    char *val;
    check((val = Ffind32(fbfr, fieldid, oc, NULL)) != NULL, "fml32 error on field %ld: %s", (long)fieldid, Fstrerror32(Ferror32));
    
    strncpy(loc, val, maxlen);
    loc[maxlen - 1] = '\0';
    return 0;
error:
    return -1;
}

int Fget32_long(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, long *loc)
{
    check(Fget32(fbfr, fieldid, oc, (char *)loc, NULL) != -1, "fml32 error on field %ld: %s", (long)fieldid, Fstrerror32(Ferror32));
    return 0;
error:
    return -1;
}

int tmibcall(const char *what, FBFR32 **resp, FLDID32 *filter)
{
    static long tpadmcall_works = 1;
    static FBFR32 *fml32_req = NULL;
    long more;
    char *cursor;

    if (fml32_req == NULL) {
        check((fml32_req = (FBFR32 *)tpalloc("FML32", NULL, 4 * 1024)) != NULL, "Failed to allocate request buffer");
    }

    Finit32(fml32_req, Fsizeof32(fml32_req));
    Finit32(*resp, Fsizeof32(*resp));

    check(Fchg32_string(&fml32_req, TA_OPERATION, 0, "GET") != -1, "Failed to set TA_OPERATION");
    check(Fchg32_string(&fml32_req, TA_CLASS, 0, what) != -1, "Failed to set TA_CLASS");
    check(Fchg32_string(&fml32_req, TA_STATE, 0, "ACT") != -1, "Failed to set TA_STATE");
    check(Fchg32_long(&fml32_req, TA_FLAGS, 0, 65536) != -1, "Failed to set TA_FLAGS");

    /* By filtering results we have space for more records, TA_MORE>0 does not happen
     * and we may use tpadmcall() and avoid tpcall()s to BBL.
     * win-win!
     */
    if (filter) {
        FLDOCC32 oc = 0;
        FLDID32 *fieldid = filter;

        while (*fieldid != BADFLDID) {
            check(Fchg32_long(&fml32_req, TA_FILTER, oc++, *fieldid++) != -1, "Failed to set TA_FILTER");
        }
    }

    if (tpadmcall_works) {
        if (tpadmcall(fml32_req, resp, TPNOFLAGS) == -1) {
            check(tperrno == TPEPROTO, "tpadmcall failed %s", tpstrerror(tperrno));
            tpadmcall_works = 0;
        }
    }

    if (!tpadmcall_works) {
        check(tpcall_fml32(".TMIB", fml32_req, resp, TPNOTRAN | TPSIGRSTRT) != -1, "Failed to call .TMIB");
    }

    check(Fget32_long(*resp, TA_MORE, 0, &more) != -1, "Failed to get TA_MORE");
    if (more > 0) {
        /*
         * tpadmcall() does not support GETNEXT
         * so for large responses switch to using tpcall
         */
        if (tpadmcall_works) {
            Finit32(*resp, Fsizeof32(*resp));
            check(tpcall_fml32(".TMIB", fml32_req, resp, TPNOTRAN | TPSIGRSTRT) != -1, "Failed to call .TMIB");
        }


        cursor = Ffind32(*resp, TA_CURSOR, 0, NULL);
        check(cursor != NULL, "TA_MORE>0 but TA_CURSOR missing");
        check(Fchg32_string(&fml32_req, TA_OPERATION, 0, "GETNEXT") != -1, "Failed to set TA_OPERATION");
        check(Fchg32_string(&fml32_req, TA_CURSOR, 0, cursor) != -1, "Failed to set TA_CURSOR");
    }

    while (more > 0) {
        check(tpcall_fml32(".TMIB", fml32_req, &fml32_req, TPNOTRAN | TPSIGRSTRT) != -1, "Failed to call .TMIB");
        for (;;) {
            if (Fconcat32(*resp, fml32_req) != -1) {
                break;
            }

            check(Ferror32 == FNOSPACE, "fml32 error: %s", Fstrerror32(Ferror32));
            check((*resp = (FBFR32 *)tprealloc((char *)*resp, Fsizeof32(*resp) * 2)) != NULL, "tprealloc failed");
        }
 

        check(Fget32_long(fml32_req, TA_MORE, 0, &more) != -1, "Failed to get TA_MORE");
    }

    return 0;

error:
    return -1;
}

int join_application(const char *name)
{
    TPINIT *tpinitbuf = NULL;
    int auth;
    char *passwd;

    tpinitbuf = (TPINIT *)tpalloc("TPINIT", NULL, TPINITNEED(16));
    check(tpinitbuf != NULL, "Failed to allocate TPINIT");

    switch (auth = tpchkauth()) {
    case TPNOAUTH :
        strcpy(tpinitbuf->usrname, name) ;
        strcpy(tpinitbuf->passwd, "") ;
        strcpy(tpinitbuf->cltname, "tpsysadm") ;
        break ;

    case TPSYSAUTH :
    case TPAPPAUTH :
        check((passwd = getpass("Application Password:")) != NULL, "Unable to get Application Password");

        strcpy(tpinitbuf->passwd, passwd) ;
        strcpy(tpinitbuf->cltname, "tpsysadm") ;

        if (auth == TPSYSAUTH)
            break ;

        fprintf(stdout, "User Name:") ;
        check(fgets(tpinitbuf->usrname, sizeof(tpinitbuf->usrname), stdin) != NULL, "fgets failed");
        strcpy(tpinitbuf->grpname, "") ;

        check((passwd = getpass("User Password:")) != NULL, "Unable to get User Password");
        strcpy ((char *) &tpinitbuf->data, passwd) ;

        tpinitbuf->datalen = strlen((char *)&tpinitbuf->data) + 1L;
        break ;
    default:
        not_reached("Unsupported auth mode %d", auth);
    }

    check(tpinit(tpinitbuf) != -1, "Failed to join application, %s", tpstrerror(tperrno));
    tpfree((char *)tpinitbuf) ;

    return 0;

error:
    return -1;
}

