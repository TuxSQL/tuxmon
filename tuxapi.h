#ifndef _tuxapi_h_
#define _tuxapi_h_
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
 
#include <atmi.h>
#include <fml32.h>
#include <tpadm.h>

int Fchg32_string(FBFR32 **fbfr, FLDID32 fieldid, FLDOCC32 oc, const char *value);
int Fchg32_long(FBFR32 **fbfr, FLDID32 fieldid, FLDOCC32 oc, long value);
int Fget32_string(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *loc, FLDLEN32 maxlen);
int Fget32_long(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, long *loc);

int tpcall_fml32(const char *svc, FBFR32 *idata, FBFR32 **odata, long flags);
int tmibcall(const char *what, FBFR32 **resp, FLDID32 *filter);
int join_application(const char *name);

#endif
