#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <tdk.h>

#ifdef __x86_64__
#define ARCHICTECTURE "x86_64"
#else
#define ARCHICTECTURE "x86"
#endif
#define BUFFER_SIZE 1024
#define PARSE_OPTION(option_a, action_a) \
    if (!strcmp("--" option_a, arguments[offset])) \
    { \
        action_a; \
        return 0; \
    }
#define PARSE_PERMISSION(permission_a, character_a, color_a) \
    if (entry->mode & permission_a) \
    { \
        tdk_set256Color(color_a, tdk_Layer_Foreground); \
        putchar(character_a); \
    } \
    else \
    { \
        tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground); \
        putchar('-'); \
    }
#define SAVE_GREATER(buffer_a, value_a) \
    if ((int)value_a > buffer_a) \
    { \
        buffer_a = value_a; \
    }

struct ArenaAllocator
{
    char *name;
    char *buffer;
    size_t unit;
    size_t use;
    size_t capacity;
};

struct Credential
{
    char *name;
    size_t nameLength;
    uid_t id;
};

struct Entry
{
    char *name;
    char *link;
    char *size;
    struct Credential *user;
    struct Credential *group;
    time_t modifiedTime;
    mode_t mode;
};

struct linux_dirent64
{
    ino64_t d_ino;
    off64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

enum LogType
{
    LogType_Warning,
    LogType_Error
};

static void allocateArenaAllocator(struct ArenaAllocator **allocator, const char *name, size_t unit, size_t capacity);
static void *allocateArenaMemory(struct ArenaAllocator *allocator, size_t use);
static void *allocateHeapMemory(size_t size);
static int countDigits(size_t number);
static struct Credential *findCredential(uid_t id, int isUser);
static char *formatSize(struct stat *status, size_t *length);
static void freeArenaAllocator(struct ArenaAllocator *allocator);
static void readDirectory(const char *directoryPath);
static int sortEntriesAlphabetically(const void *entry0, const void *entry1);
static void writeEntryNo(int no, int align);
static void writeHelp(void);
static void writeLines(int totalLines, ...);
static void writeLog(int type, const char *format, ...);
