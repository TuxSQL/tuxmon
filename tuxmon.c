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
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include <atmi.h>
#include <fml32.h>
#include <tpadm.h>

#include <ncurses.h>

/*
 * Currently just 2 samples to calculate TPS,
 * should be able to calculate TPM in future
 */
#define SAMPLE_COUNT 2

static FBFR32 *fml32_machine = NULL;
static FBFR32 *fml32_domain = NULL;
static FBFR32 *fml32_msg = NULL;
static FBFR32 *fml32_server = NULL;

static long max_queues;
static long max_servers;

static int aggregate_stats = 0;
static int show_system = 0;
enum server_sort { SORT_SERVICE, SORT_REQ, SORT_TRX };
static int server_sort_order = SORT_SERVICE;

static FLDID32 fld_msg[] = {
    TA_MSGID,
    TA_MSG_LRPID,
    TA_MSG_LSPID, 
    TA_MSG_QNUM,
    TA_MSG_CBYTES,
    TA_MSG_QBYTES,
    BADFLDID
};

static FLDID32 fld_server[] = {
    TA_SERVERNAME,
    TA_SRVID,
    TA_PID,
    TA_RPID,
    TA_RQID,
    TA_NUMREQ,
    TA_NUMTRAN,
    TA_NUMTRANABT,
    TA_NUMTRANCMT,
    TA_CURRSERVICE,
    TA_TOTREQC,
    BADFLDID
};

enum qtypes { UNKNOWN = 0, IN, INMANY, INOUT, OUT };
static const char *strqtype(int qtype)
{
    if (qtype == IN) {
        return "  -> ";
    } else if (qtype == INMANY ) {
        return "  => ";
    } else if (qtype == INOUT) {
        return " <-> ";
    } else if (qtype == OUT ) {
        return "  -# ";
    } else {
        return " ??? ";
    }
}

struct tux_server;
struct tux_queue {
    long msgid;

    long qnum;
    long cbytes;
    long qbytes;

    double used;

    long lrpid;
    long lspid;

    int qtype;
    struct tux_server *server;
    struct tux_server *sender;
    long sender_pid;
};

struct tux_machine {
    char lmid[30];
    char pmid[30];

    long n_tran;
    long n_tranabt;
    long n_trancmt;

    long n_req;
    long n_enqueue;
    long n_dequeue;

    long n_accessers;
    long n_clients;
    long n_gtt;

    long n_queues;
    long n_servers;
    long n_services;
};

struct tux_server {
    char name[20];

    long pid;
    long rqid;
    long rpid;
    long n_req;
    long n_tran;
    long n_tranabt;
    long n_trancmt;
    long reqc;
    int system;
};

struct tux_server_stat {
    double n_req;
    double n_tran;
    double n_tranabt;
    double n_trancmt;
    double reqc;
    long n;
    struct tux_server *server;
};

struct sample {
    struct timeval timestamp;

    struct tux_machine machine;
    struct tux_queue *queues;
    struct tux_server *servers;

    int n_queues;
    int n_servers;

    long n_act_msgs;
    long n_act_queues;
    long n_act_servers;
};

static struct sample samples[SAMPLE_COUNT];

static struct tux_server_stat *server_stats = NULL;
static struct tux_server_stat *tmp_server_stats = NULL;
static int n_server_stats = 0;

static int sample_next_index(int sample_index)
{
    return (sample_index + 1) % SAMPLE_COUNT;
}

static int init_stats()
{
    check(tmibcall("T_DOMAIN", &fml32_domain, NULL) != -1, "Failed to call MIB");

    check(Fget32_long(fml32_domain, TA_MAXSERVERS, 0, &max_servers) != -1, "");
    /* Does not count response queues and client queues
    check(Fget32_long(fml32_domain, TA_MAXQUEUES, 0, &max_queues) != -1, "");
     */

    max_queues = max_servers * 3;

    return 0;
error:
    return -1;
}

static int cmp_queue_msgid(const void *x, const void *y)
{
    return ((struct tux_queue *)x)->msgid - ((struct tux_queue *)y)->msgid;
}

/* Descending by % of queue space used, number of messages */
static int cmp_queue_used(const void *x, const void *y)
{
    double d = ((struct tux_queue *)y)->used - ((struct tux_queue *)x)->used;

    if (d < 0) {
        return -1;
    } else if (d > 0) {
        return 1;
    } else {
        return ((struct tux_queue *)y)->qnum - ((struct tux_queue *)x)->qnum;
    }
}

static int cmp_server_pid(const void *x, const void *y)
{
    return ((struct tux_server *)x)->pid - ((struct tux_server *)y)->pid;
}

static int cmp_server_stat_reqc(const void *x, const void *y)
{
    double d = ((struct tux_server_stat *)y)->reqc - ((struct tux_server_stat *)x)->reqc;
    if (d < 0) {
        return -1;
    } else if (d > 0) {
        return 1;
    } else {
        return 0;
    }
}

static int cmp_server_stat_n_req(const void *x, const void *y)
{
    double d = ((struct tux_server_stat *)y)->n_req - ((struct tux_server_stat *)x)->n_req;
    if (d < 0) {
        return -1;
    } else if (d > 0) {
        return 1;
    } else {
        return 0;
    }
}

static int cmp_server_stat_rqid(const void *x, const void *y)
{
    double d = ((struct tux_server_stat *)x)->server->reqc - ((struct tux_server_stat *)y)->server->reqc;
    if (d < 0) {
        return -1;
    } else if (d > 0) {
        return 1;
    } else {
        return 0;
    }
}

static int cmp_server_stat_n_tran(const void *x, const void *y)
{
    double d = ((struct tux_server_stat *)x)->server->n_tran - ((struct tux_server_stat *)y)->server->n_tran;
    if (d < 0) {
        return -1;
    } else if (d > 0) {
        return 1;
    } else {
        return 0;
    }
}


static int poll_mib(int idx)
{
    struct sample *sample;
    FLDOCC32 oc;
    FLDOCC32 count;
    
    sample = &samples[idx];
    check(gettimeofday(&sample->timestamp, NULL) != -1, "gettimeofday failed");

    check(tmibcall("T_DOMAIN", &fml32_domain, NULL) != -1, "Failed to call MIB");
    check(tmibcall("T_MACHINE", &fml32_machine, NULL) != -1, "Failed to call MIB");
    check(tmibcall("T_MSG", &fml32_msg, fld_msg) != -1, "Failed to call MIB");
    check(tmibcall("T_SERVER", &fml32_server, fld_server) != -1, "Failed to call MIB");

    check(Fget32_string(fml32_machine, TA_LMID, 0, sample->machine.lmid, sizeof(sample->machine.lmid)) != -1, "");
    check(Fget32_string(fml32_machine, TA_PMID, 0, sample->machine.pmid, sizeof(sample->machine.pmid)) != -1, "");

    check(Fget32_long(fml32_machine, TA_NUMREQ, 0, &sample->machine.n_req) != -1, "");
    check(Fget32_long(fml32_machine, TA_NUMENQUEUE, 0, &sample->machine.n_enqueue) != -1, "");
    check(Fget32_long(fml32_machine, TA_NUMDEQUEUE, 0, &sample->machine.n_dequeue) != -1, "");

    check(Fget32_long(fml32_machine, TA_NUMTRAN, 0, &sample->machine.n_tran) != -1, "");
    check(Fget32_long(fml32_machine, TA_NUMTRANABT, 0, &sample->machine.n_tranabt) != -1, "");
    check(Fget32_long(fml32_machine, TA_NUMTRANCMT, 0, &sample->machine.n_trancmt) != -1, "");

    check(Fget32_long(fml32_machine, TA_CURACCESSERS, 0, &sample->machine.n_accessers) != -1, "");
    check(Fget32_long(fml32_machine, TA_CURCLIENTS, 0, &sample->machine.n_clients) != -1, "");
    check(Fget32_long(fml32_machine, TA_CURGTT, 0, &sample->machine.n_gtt) != -1, "");

    check(Fget32_long(fml32_domain, TA_CURQUEUES, 0, &sample->machine.n_queues) != -1, "");
    check(Fget32_long(fml32_domain, TA_CURSERVICES, 0, &sample->machine.n_services) != -1, "");
    check(Fget32_long(fml32_domain, TA_CURSERVERS, 0, &sample->machine.n_servers) != -1, "");

    if (sample->queues == NULL) {
        check((sample->queues = (struct tux_queue *)calloc(max_queues, sizeof(sample->queues[0]))) != NULL, "Failed to allocate queue statistics");
    }

    sample->n_queues = 0;
    sample->n_act_msgs = 0;
    sample->n_act_queues = 0;

    check((count = Foccur32(fml32_msg, TA_MSGID)) != -1, "Failed to count TA_MSGID");

    for (oc = 0; oc < count; oc++) {
        struct tux_queue *q;

        if (oc >= max_queues) {
            struct tux_queue *old = sample->queues;
            check((sample->queues = (struct tux_queue *)calloc(max_queues * 2, sizeof(sample->queues[0]))) != NULL, "Failed to allocate queue statistics");
            memcpy(sample->queues, old, max_queues * sizeof(sample->queues[0]));
            max_queues *= 2;
            free(old);
        }
        check(oc < max_queues, "More than max queues");

        q = &sample->queues[sample->n_queues];

        check(Fget32_long(fml32_msg, TA_MSGID, oc, &q->msgid) != -1, "");
        check(Fget32_long(fml32_msg, TA_MSG_LRPID, oc, &q->lrpid) != -1, "");
        check(Fget32_long(fml32_msg, TA_MSG_LSPID, oc, &q->lspid) != -1, "");

        check(Fget32_long(fml32_msg, TA_MSG_QNUM, oc, &q->qnum) != -1, "");
        check(Fget32_long(fml32_msg, TA_MSG_CBYTES, oc, &q->cbytes) != -1, "");
        check(Fget32_long(fml32_msg, TA_MSG_QBYTES, oc, &q->qbytes) != -1, "");

        q->used = (double)q->cbytes / (double)q->qbytes;
        q->server = NULL;
        q->sender = NULL;
        q->sender_pid = 0;
        q->qtype = UNKNOWN;

        if (q->qnum > 0) {
            sample->n_act_msgs += q->qnum;
            sample->n_act_queues += 1;
        }
        sample->n_queues++;
    }

    qsort(sample->queues, sample->n_queues, sizeof(sample->queues[0]), cmp_queue_msgid);

    if (sample->servers == NULL) {
        check((sample->servers = (struct tux_server *)calloc(max_servers, sizeof(sample->servers[0]))) != NULL, "Failed to allocate server statistics");
    }

    sample->n_servers = 0;
    check((count = Foccur32(fml32_server, TA_PID)) != -1, "Failed to count TA_PID");

    for (oc = 0; oc < count; oc++) {
        struct tux_server *s;
        char *slash, *name;
        long srvid;

        check(oc < max_servers, "More than max servers");

        s = &sample->servers[sample->n_servers];

        name = Ffind32(fml32_server, TA_SERVERNAME, oc, NULL);
        slash = strrchr(name, '/');
        if (slash != NULL) {
            name = slash + 1;
        }

        if (strlen(name) > sizeof(s->name) - 1) {
            strncpy(s->name, name, sizeof(s->name));
            s->name[sizeof(s->name) - 2] = '+';
            s->name[sizeof(s->name) - 1] = '\0';
        } else {
            strcpy(s->name, name);
        }

        check(Fget32_long(fml32_server, TA_SRVID, oc, &srvid) != -1, "");
        s->system = (srvid == 0 || srvid > 30000);

        check(Fget32_long(fml32_server, TA_PID, oc, &s->pid) != -1, "");
        check(Fget32_long(fml32_server, TA_RPID, oc, &s->rpid) != -1, "");
        check(Fget32_long(fml32_server, TA_RQID, oc, &s->rqid) != -1, "");
        check(Fget32_long(fml32_server, TA_NUMREQ, oc, &s->n_req) != -1, "");
        check(Fget32_long(fml32_server, TA_NUMTRAN, oc, &s->n_tran) != -1, "");
        check(Fget32_long(fml32_server, TA_NUMTRANABT, oc, &s->n_tranabt) != -1, "");
        check(Fget32_long(fml32_server, TA_NUMTRANCMT, oc, &s->n_trancmt) != -1, "");
        check(Fget32_long(fml32_server, TA_TOTREQC, oc, &s->reqc) != -1, "");

        sample->n_servers++;
    }
    qsort(sample->servers, sample->n_servers, sizeof(sample->servers[0]), cmp_server_pid);

    for (oc = 0; oc < sample->n_servers; oc++) {
        struct tux_queue qkey, *qfound;
        qkey.msgid = sample->servers[oc].rqid;
        qfound = (struct tux_queue *)bsearch(&qkey, sample->queues,
                                        sample->n_queues, sizeof(sample->queues[0]),
                                        cmp_queue_msgid);
        if (qfound != NULL) {
            qfound->server = &sample->servers[oc];
            if (qfound->server->rqid == qfound->server->rpid) {
                qfound->qtype = INOUT;
                continue;
            } else {
                if (qfound->qtype == UNKNOWN) {
                    qfound->qtype = IN;
                } else {
                    qfound->qtype = INMANY;
                }
            }
        }

        qkey.msgid = sample->servers[oc].rpid;
        qfound = (struct tux_queue *)bsearch(&qkey, sample->queues,
                                        sample->n_queues, sizeof(sample->queues[0]),
                                        cmp_queue_msgid);
        if (qfound != NULL) {
            qfound->qtype = OUT;
            qfound->server = &sample->servers[oc];
        }
    }

    for (oc = 0; oc < sample->n_queues; oc++) {
        struct tux_server skey, *sfound;

        if (sample->queues[oc].qtype == INOUT) {
            /* Fill in sender process only if it is not the server itreceiver */
            if (sample->queues[oc].server->pid != sample->queues[oc].lrpid) {
                skey.pid = sample->queues[oc].lrpid;
            } else if (sample->queues[oc].server->pid != sample->queues[oc].lspid) {
                skey.pid = sample->queues[oc].lspid;
            } else {
                continue;
            }
        } else if (sample->queues[oc].qtype == UNKNOWN) {
            skey.pid = sample->queues[oc].lspid;
            sample->queues[oc].qtype = IN;
        } else {
            skey.pid = sample->queues[oc].lspid;
        }

        sample->queues[oc].sender_pid = skey.pid;
        sfound = (struct tux_server *)bsearch(&skey, sample->servers,
                                        sample->n_servers, sizeof(sample->servers[0]),
                                        cmp_server_pid);
        if (sfound != NULL) {
            sample->queues[oc].sender = sfound;
        }
    }

    qsort(sample->queues, sample->n_queues, sizeof(sample->queues[0]), cmp_queue_used);

    return 0;
error:
    return -1;
}

static double xpp(long low, long high, double seconds)
{
    if (seconds == 0) {
        return 0;
    }

    if (low <= high) {
        return ((double)(high - low)) / seconds;
    }
    return ((double)(LONG_MAX - low + high)) / seconds;
}

static int calculate_diff(struct sample *low, struct sample *high, double seconds)
{
    n_server_stats = 0;
    if (tmp_server_stats == NULL) {
        check((tmp_server_stats = (struct tux_server_stat *)calloc(max_servers, sizeof(server_stats[0]))) != NULL, "Failed to allocate server statistics");
    }
    if (server_stats == NULL) {
        check((server_stats = (struct tux_server_stat *)calloc(max_servers, sizeof(server_stats[0]))) != NULL, "Failed to allocate server statistics");
    }
    high->n_act_servers = 0;


    int l = 0, h = 0;

    /* XXX This does not show server statistics
     * until it's present in both low and high although
     * several samples may exist between low and high.
     */
    while (h < high->n_servers) {
        int cmp = low->servers[l].pid - high->servers[h].pid;
        if (cmp < 0) {
            if (l < low->n_servers) {
                l++;
            } else {
                h++; /* new server PID, not enough statistics */
            }
        } else if (cmp > 0) {
            h++; /* new server PID, not enough statistics */
        } else {
            server_stats[n_server_stats].reqc = xpp(low->servers[l].reqc, high->servers[h].reqc, seconds);
            server_stats[n_server_stats].n_req = xpp(low->servers[l].n_req, high->servers[h].n_req, seconds);
            server_stats[n_server_stats].n_tran = xpp(low->servers[l].n_tran, high->servers[h].n_tran, seconds);
            server_stats[n_server_stats].server = &high->servers[h];
            server_stats[n_server_stats].n = 1;

            if (server_stats[n_server_stats].reqc > 0) {
                high->n_act_servers++;
            }

            n_server_stats++;
            l++;
            h++;
        }
    }

    if (n_server_stats > 1) {
        if (aggregate_stats) {
            int outex;
            int i;

            qsort(server_stats, n_server_stats, sizeof(server_stats[0]), cmp_server_stat_rqid);

            outex = 0;
            tmp_server_stats[outex] = server_stats[0];
            for (i = 1; i < n_server_stats; i++) {
                struct tux_server_stat *new = &server_stats[i];
                struct tux_server_stat *current = &tmp_server_stats[outex];

                if (new->server->rqid == current->server->rqid) {
                    current->n_req += new->n_req;
                    current->n_tran += new->n_tran;
                    current->n_tranabt += new->n_tranabt;
                    current->n_trancmt += new->n_trancmt;
                    current->n++;
                } else {
                    outex++;
                    tmp_server_stats[outex] = server_stats[i];
                }
            }

            n_server_stats = outex + 1;
            memcpy(server_stats, tmp_server_stats, n_server_stats * sizeof(server_stats[0]));
        }

        if (server_sort_order == SORT_SERVICE) {
            qsort(server_stats, n_server_stats, sizeof(server_stats[0]), cmp_server_stat_reqc);
        } else if (server_sort_order == SORT_REQ) {
            qsort(server_stats, n_server_stats, sizeof(server_stats[0]), cmp_server_stat_n_req);
        } else {
            qsort(server_stats, n_server_stats, sizeof(server_stats[0]), cmp_server_stat_n_tran);
        }
    }

    return 0;
error:
    return -1;
}

static double timestamp_diff(struct timeval *start, struct timeval *end)
{
    return ((double)(end->tv_sec - start->tv_sec)) + 0.000001 * (end->tv_usec - start->tv_usec);
}

/* Sleep for "seconds" since "since", continue after interrupts
 * like window size change.
 */
static int force_sleep(struct timeval *since, double seconds)
{
    struct timespec req, rem;
    struct timeval now;

    check(gettimeofday(&now, NULL) != -1, "gettimeofday failed");

    seconds -= timestamp_diff(since, &now);
    if (seconds < 0) {
        return 0;
    }

    rem.tv_sec = seconds;
    rem.tv_nsec = (seconds - (long)seconds) * 1000000000.0;

    while (rem.tv_sec > 0 || rem.tv_nsec > 0) {
        req = rem;
        if (nanosleep(&req, &rem) == 0) {
            break;
        }
        check(errno == EINTR, "nanosleep() failed");
    }
    return 0;
error:
    return -1;
}

static void cleanup()
{
    endwin();
    tpterm();
}

static void usage(const char *progname)
{
    fprintf(stderr,
            "Usage: %s [-a][-s]\n"
            "-a or --aggregate      Aggregate server statistics by request queue\n"
            "-s or --system         Include system servers and queues (BBL and RM)\n"
            "", progname);
}

static int parse_options(int argc, char *argv[])
{
    int optind;

    for (optind = 1; optind < argc && argv[optind][0] == '-'; optind++) {
        if (strcmp(argv[optind], "-a") == 0 || strcmp(argv[optind], "--aggregate") == 0) {
            aggregate_stats = 1;
        } else if (strcmp(argv[optind], "-s") == 0 || strcmp(argv[optind], "--system") == 0) {
            show_system = 1;
        } else if (strcmp(argv[optind], "-h") == 0 || strcmp(argv[optind], "--help") == 0) {
            usage(argv[0]);
            exit(0);
        } else {
            log_err("Unknown option [%s]", argv[optind]);
            usage(argv[0]);
            exit(-1);
        }
    }

    return 0;
}


int main(int argc, char *argv[])
{
    int low, high;

    (void)parse_options(argc, argv);

    low = 0;
    high = 0;

    check((fml32_machine = (FBFR32 *)tpalloc("FML32", NULL, 4 * 1024)) != NULL, "Failed to allocate request buffer");
    check((fml32_domain = (FBFR32 *)tpalloc("FML32", NULL, 4 * 1024)) != NULL, "Failed to allocate request buffer");
    check((fml32_msg = (FBFR32 *)tpalloc("FML32", NULL, 4 * 1024)) != NULL, "Failed to allocate request buffer");
    check((fml32_server = (FBFR32 *)tpalloc("FML32", NULL, 4 * 1024)) != NULL, "Failed to allocate request buffer");

    check(join_application(argv[0]) != -1, "Failed to join application");

    init_stats();

    initscr();
    atexit(cleanup);

    cbreak();
    noecho();
    curs_set(0);

    nodelay(stdscr, TRUE);

    while (1) {
        struct sample *l, *h;
        double period;
        int idx;
        int row;
        int out_rows;
        int stat_rows;
        int ch;
        int queue_row, server_row;
        int rows, cols;
        char header[64];

        while ((ch = getch()) != ERR) {
            if (ch == 'q' || ch == 'Q') {
                goto done;
            } else if (ch == 's' || ch == 'S') {
                server_sort_order = SORT_SERVICE;
            } else if (ch == 'r' || ch == 'R') {
                server_sort_order = SORT_REQ;
            } else if (ch == 't' || ch == 'T') {
                server_sort_order = SORT_TRX;
            }
        }

        check(poll_mib(high) != -1, "");

        l = &samples[low];
        h = &samples[high];

        period = timestamp_diff(&l->timestamp, &h->timestamp);
        check(calculate_diff(l, h, period) != -1, "Failed to calculate server statistics");

        erase();

        getmaxyx(stdscr, rows, cols);
        cols = cols;

        stat_rows = (rows - 6) / 2;
        queue_row = 4;
        server_row = queue_row + 1 + stat_rows;

        row = 0;

        attron(A_REVERSE);
        move(row, 0); hline(' ', 80);
        snprintf(header, sizeof(header), "%s : %s", h->machine.pmid, h->machine.lmid);
        mvprintw(row++, 0,
                "%-40s ACTIVE M: %-3ld  Q: %-3ld  S: %-3ld",
                header, h->n_act_msgs, h->n_act_queues, h->n_act_servers);
        attroff(A_REVERSE);

        mvprintw(row++, 0, "Queues:   % 5ld   Accessers: % 5ld   Req/s: % 9.2f   Trx/s: % 9.2f",
                    h->machine.n_queues, h->machine.n_accessers,
                    xpp(l->machine.n_req, h->machine.n_req, period),
                    xpp(l->machine.n_tran, h->machine.n_tran, period)
                );
        mvprintw(row++, 0, "Servers:  % 5ld   Clients:   % 5ld   Enq/s: % 9.2f   Cmt/s: % 9.2f",
                    h->machine.n_servers, h->machine.n_clients,
                    xpp(l->machine.n_enqueue, h->machine.n_enqueue, period),
                    xpp(l->machine.n_trancmt, h->machine.n_trancmt, period)
                );
        mvprintw(row++, 0, "Services: % 5ld   GTT used:  % 5ld   Deq/s: % 9.2f   Abt/s: % 9.2f",
                    h->machine.n_services, h->machine.n_gtt,
                    xpp(l->machine.n_dequeue, h->machine.n_dequeue, period),
                    xpp(l->machine.n_tranabt, h->machine.n_tranabt, period)
                );

        row = queue_row;
        attron(A_REVERSE);
        move(row, 0); hline(' ', 80);
        mvprintw(row++, 0, "MSQID     MSGS    %%FULL                  FROM       TO");
        attroff(A_REVERSE);

        for (idx = 0, out_rows = 0; idx < h->n_queues; idx++) {
            char receiver[32], sender[32];

            if (h->queues[idx].server != NULL) {
                if (!show_system && h->queues[idx].server->system) {
                    continue;
                }

                strcpy(receiver, h->queues[idx].server->name);
            } else if (h->queues[idx].lrpid != 0)  {
                snprintf(receiver, sizeof(receiver), "(%ld)",  h->queues[idx].lrpid);
            } else {
                receiver[0] = '\0';
            }

            if (h->queues[idx].sender != NULL) {
                strcpy(sender, h->queues[idx].sender->name);
            } else if (h->queues[idx].sender_pid != 0) {
                snprintf(sender, sizeof(sender), "(%ld)",  h->queues[idx].sender_pid);
            } else {
                sender[0] = '\0';
            }

            mvprintw(row, 0, "%-9ld  % 3ld  % 6.2f%%  %+20s %s %-20s",
                    h->queues[idx].msgid, 
                    h->queues[idx].qnum, 
                    h->queues[idx].used * 100.0,
                    sender,
                    strqtype(h->queues[idx].qtype),
                    receiver
                    );
            row++;
            if (++out_rows >= stat_rows) {
                break;
            }
        }

        row = server_row;
        attron(A_REVERSE);
        move(row, 0); hline(' ', 80);
        if (aggregate_stats) {
            mvprintw(row++, 0, "COUNT      RQID       SERVICE/S      REQ/S      TRX/S  SERVER");
        } else {
            mvprintw(row++, 0, "PID        RQID       SERVICE/S      REQ/S      TRX/S  SERVER");
        }
        attroff(A_REVERSE);

        for (idx = 0, out_rows = 0; idx < n_server_stats; idx++) {
            char first_col[32];

            if (!show_system && server_stats[idx].server->system) {
                continue;
            }

            if (aggregate_stats) {
                snprintf(first_col, sizeof(first_col), "% 3ld", server_stats[idx].n);
            } else {
                snprintf(first_col, sizeof(first_col), "(%ld)",  server_stats[idx].server->pid);
            }

            mvprintw(row, 0, "%-9s  %-9ld  % 9.2f  % 9.2f  % 9.2f  %-28s",
                    first_col,
                    server_stats[idx].server->rqid,
                    server_stats[idx].reqc,
                    server_stats[idx].n_req,
                    server_stats[idx].n_tran,
                    server_stats[idx].server->name
                    );
            row++;
            if (++out_rows >= stat_rows) {
                break;
            }
        }

        refresh();

        force_sleep(&h->timestamp, 1);

        low = high;
        high = sample_next_index(high);
    }

done:
    return 0;

error:
    return -1;
}
