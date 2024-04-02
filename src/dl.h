#define _GNU_SOURCE
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AVERAGE_CREDENTIAL_NAME_LENGTH 14
#define AVERAGE_ENTRY_NAME_LENGTH 16
#define AVERAGE_PATH_LENGTH 260
#define MAXIMUM_CREDENTIALS_EXPECTED 20
#define MAXIMUM_ENTRIES_EXPECTED 30000
#define PARSE_FILE_PERMISSION(permission_a, character_a, color_a) \
  setColor(entry.mode &permission_a ? color_a : Color_Default); \
  putchar(entry.mode &permission_a ? character_a : '-');
#define PARSE_OPTION(option_a, action_a) \
  if (!strcmp(arguments[index], "--" option_a)) { \
    action_a; \
    return 0; \
  }
#define SAVE_GREATER(buffer_a, value_a) \
  if (buffer_a < value_a) { \
    buffer_a = value_a; \
  }
#define VERSION "v1.0.0"

enum Color {
  Color_Black,
  Color_Red,
  Color_Green,
  Color_Yellow,
  Color_Blue,
  Color_Magenta,
  Color_Cyan,
  Color_White,
  Color_Default = 9
};

struct ArenaAllocator {
  char *name;
  char *buffer;
  char *cursor;
  size_t size;
};

struct Credential {
  char *name;
  size_t nameLength;
  uid_t id;
};

struct Entry {
  struct Credential *user;
  struct Credential *group;
  char *link;
  char *name;
  char *size;
  time_t modifiedEpoch;
  mode_t mode;
};

struct SIMultiplier {
  float value;
  char prefix;
};

static struct ArenaAllocator *allocateArenaAllocator(char *name, size_t size);
static void *allocateArenaMemory(struct ArenaAllocator *allocator, size_t size);
static void *allocateHeapMemory(size_t size);
static int countDigits(size_t number);
static void deallocateArenaAllocator(struct ArenaAllocator *allocator);
static void deallocateArenaMemory(struct ArenaAllocator *allocator,
                                  size_t size);
static struct Credential *findCredentialByID(int isUser, uid_t id);
static char *formatEntrySize(size_t *bufferLength, struct stat *status);
static void help(void);
static void readDirectory(char *directoryPath);
static void resetArenaAllocator(struct ArenaAllocator *allocator);
static void setColor(int color);
static int sortEntriesAlphabetically(const void *entry0, const void *entry1);
static void throwError(char *format, ...);
static void writeError(char *format, ...);
static void writeLine(size_t totalLines, ...);
