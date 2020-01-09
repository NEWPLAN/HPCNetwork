#ifndef __BASE_UTILS_H__

#define __BASE_UTILS_H__

#include <unistd.h>
#include <sys/syscall.h>
#define LOG_INFO(data)                                                                                          \
    do                                                                                                          \
    {                                                                                                           \
        printf("[tid:%ld %s:%d func:%s] %s\n", ::syscall(__NR_gettid), __FILE__, __LINE__, __FUNCTION__, data); \
    } while (0)

#endif