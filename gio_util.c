#include <stdlib.h>
#include <sys/time.h>

#include "gio_err.h"

double gio_get_time(void)
{
  double t;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  t = ((double)(tv.tv_sec) + (double)(tv.tv_usec) * 0.001 * 0.001);
  //  gio_dbg(" -== > %f", t);
  return t;
}
