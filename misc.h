#ifndef _misc_h_
#define _misc_h_

#include <stdio.h>
#include <string.h>
#include <ncurses.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

#ifdef NDEBUG
#define debug(fmt, ...)
#else
#define debug(fmt, ...) endwin(); fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); refresh()
#endif

#define log_err(fmt, ...) endwin(); fprintf(stderr, "[ERROR] (%s in %s:%d) " fmt "\n", __PRETTY_FUNCTION__, __FILE__, __LINE__,  ##__VA_ARGS__); refresh()
#define check(expr, fmt, ...) if (!(expr)) { log_err(fmt, ##__VA_ARGS__); goto error; }
#define not_reached(fmt, ...) { log_err(fmt, ##__VA_ARGS__); goto error; }

#endif
