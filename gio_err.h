#ifndef GIO_ERR_H
#define GIO_ERR_H

void gio_err_init(int r);
void gio_err(const char* fmt, ...);
void gio_alert(const char* fmt, ...);
void gio_dbg(const char* fmt, ...);
void gio_print(const char* fmt, ...);
void gio_debug(const char* fmt, ...);
void gio_btrace();

void gio_exit(int no);

#endif
