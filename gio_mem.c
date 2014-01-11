#include <stdlib.h>
#include <stdio.h>

#include "gio_err.h"

unsigned long total_alloc_size = 0;
unsigned long total_alloc_count = 0;

void* gio_malloc(size_t size) 
{
  void* addr;
  if ((addr = malloc(size)) == NULL) {
    gio_err("Memory allocation returned (%s:%s:%d)",  __FILE__, __func__, __LINE__);
  }
  total_alloc_count++;

  //TODO: Manage memory consumption
  //  total_alloc_size += size;
  //  gio_dbg("malloc: done %d", total_alloc_size);
  return addr;
}

void gio_free(void* addr) 
{
  free(addr);
  total_alloc_count--;

  //TODO: Manage memory consumption
  //  total_alloc_size -= size;
  return;
}
