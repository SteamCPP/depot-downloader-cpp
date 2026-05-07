// Abstracts the difference between WIN32 & UNIX-like systems
#pragma once

#ifdef _WIN32
    #include <io.h>
    #include <windows.h>
    #define pwrite(fd, buf, count, offset) \
        (_lseeki64(fd, offset, SEEK_SET), _write(fd, buf, count))
    typedef int ssize_t;
#else
    #include <unistd.h>
#endif
