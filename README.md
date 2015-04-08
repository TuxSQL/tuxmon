# TuxMon

A simple top-like monitoring tool for Oracle Tuxedo

![tuxmon](http://tuxsql.com/img/portfolio/tuxmon.png#0)

## Information fields

* Logical and physical machine name. NB: Following numbers include Resource Managers and their queues.
    * Number of messages in IPC queues
    * Number of IPC queues with messages in them
    * Number of active servers with some service requests completed

* Domain statistics
    * Queues: Number of server queues
    * Server: Number of servers defined
    * Services: Number of services defined
    * Accessers: Number of clients and servers accessing the application
    * Clients: Number of clients accessing the application
    * GTT: Number of transaction table entries used
    * Req/s: Number of tpcall() and tapcall() calls per second
    * Enq/s: Number of tpenqueue() calls per second
    * Deq/s: Number of tpdequeue() calls per second
    * Trx/s: Number of transactions started per second
    * Cmt/s: Number of transactions committed per second
    * Abt/s: Number of transactions aborted per second

* A list of IPC queues sorted by "%FULL" column
    * MSQID: IPC queue identifier
    * MSGS: Number of messages in the queue
    * %FULL: % of queue space in use
    * FROM: Tuxedo server name (for reply queues) or PID of process that last sent message to queue
        * -> is a regular request or response queue
        * => is a MSSQ request queue
        * <-> is a single queue for requests and responses
        * -# is a reply queue
    * TO: Tuxedo server name (for reques queues) or PID of process that last received message from queue 

* A list of Tuxedo servers
    * PID: Server PID (Default mode)
    * COUNT: Number of servers (Aggregate mode)
    * RQID: Server request queue ID (MSQID)
    * SERVICE/S: Number of service requests completed per second
    * REQ/S: Number of tpcall() and tpacall() calls per second
    * TRX/S: Number of transactions started per second
    * SERVER: Server name

## Command-line options

* -a or --aggregate: Aggregate servers sharing the same request queue and shows per-request-queue statistics instead of per-server-process statistics.
* -s or --system: Show system servers and queues like BBL and Resource Managers. That's a great way to see how much IPC is *really* going on.

## Interactive commands

* s: Sorts servers by number of service requests completed
* r: Sorts servers by number of tpcall() or tpacall() calls made
* t: Sorts servers by number of transactions
* q: Quit

## Building

Set environment variable TUXDIR to root directory of Tuxedo installation. Now type `make` to build it.
You might need to change some compiler and linker flags in Makefile if it's different from GCC.
