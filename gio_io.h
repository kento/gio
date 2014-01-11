
int gio_open(const char* file, int flags, mode_t  mode);
int gio_close(const char* file, int fd);
ssize_t gio_write(const char* file, int fd, const void* buf, size_t size);
ssize_t gio_read(const char* file, int fd, void* buf, size_t size);
