#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <execinfo.h>
#include <unistd.h>

#include "gio_err.h"

#define DEBUG_STDOUT stderr

int rank;
char hostname[256];

void gio_err_init(int r) 
{
  rank = r;
  if (gethostname(hostname, sizeof(hostname)) != 0) {
    gio_err("gethostname fialed (%s:%s:%d)", __FILE__, __func__, __LINE__);
  }
  return;
}

char* gio_gethostname() {
  return hostname;
}

void gio_err(const char* fmt, ...)
{
  va_list argp;
  fprintf(stderr, "GIO:ERROR:%s:%d: ", hostname, rank);
  va_start(argp, fmt);
  vfprintf(stderr, fmt, argp);
  va_end(argp);
  fprintf(stderr, "\n");
  exit(1);
  return;
}

void gio_alert(const char* fmt, ...)
{
  va_list argp;
  fprintf(stderr, "GIO:ALERT:%s:%d: ", hostname, rank);
  va_start(argp, fmt);
  vfprintf(stderr, fmt, argp);
  va_end(argp);
  fprintf(stderr, " (%s:%s:%d)\n", __FILE__, __func__, __LINE__);
  exit(1);
  return;
}

void gio_dbg(const char* fmt, ...) {
  va_list argp;
  fprintf(DEBUG_STDOUT, "GIO:DEBUG:%s:%d: ", hostname, rank);
  va_start(argp, fmt);
  vfprintf(DEBUG_STDOUT, fmt, argp);
  va_end(argp);
  fprintf(DEBUG_STDOUT, "\n");
  return;
}

void gio_print(const char* fmt, ...) {
  va_list argp;
  fprintf(stdout, "GIO:%s:%d: ", hostname, rank);
  va_start(argp, fmt);
  vfprintf(stdout, fmt, argp);
  va_end(argp);
  fprintf(stdout, "\n");
  return;
}

void gio_dbgi(int r, const char* fmt, ...) {
  if (rank != r) return;
  va_list argp;
  fprintf(DEBUG_STDOUT, "GIO:DEBUG:%s:%d: ", hostname, rank);
  va_start(argp, fmt);
  vfprintf(DEBUG_STDOUT, fmt, argp);
  va_end(argp);
  fprintf(DEBUG_STDOUT, "\n");
}

void gio_debug(const char* fmt, ...)
{
  va_list argp;
  fprintf(DEBUG_STDOUT, "GIO:DEBUG:%s:%d: ", hostname, rank);
  va_start(argp, fmt);
  vfprintf(DEBUG_STDOUT, fmt, argp);
  va_end(argp);
  fprintf(DEBUG_STDOUT, "\n");
}


void gio_exit(int no) {
  fprintf(stderr, "GIO:DEBUG:%s:%d: == EXIT == sleep 1sec ...\n", hostname, rank);
  sleep(1);
  exit(no);
  return;
}

/* void gio_test(void* ptr, char* file, char* func, int line) */
/* { */
/*   if (ptr == NULL) { */
/*     gio_err("NULL POINTER EXCEPTION (%s:%s:%d)", file, func, line); */
/*   } */
/* } */


void gio_btrace() 
{
  int j, nptrs;
  void *buffer[100];
  char **strings;

  nptrs = backtrace(buffer, 100);

  /* backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO)*/
  strings = backtrace_symbols(buffer, nptrs);
  if (strings == NULL) {
    perror("backtrace_symbols");
    exit(EXIT_FAILURE);
  }   

  /*
    You can translate the address to function name by
    addr2line -f -e ./a.out <address>
  */
  for (j = 0; j < nptrs; j++)
    gio_dbg("%s", strings[j]);
  free(strings);
  return;
}

void gio_btracei(int r) 
{
  if (rank != r) return;
  gio_btrace();
  return;
}
