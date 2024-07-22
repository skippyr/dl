#if defined(_WIN32)
#include <Windows.h>
#else
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <tmk.h>

#if defined(_WIN32)
#define PROGRAM_PLATFORM "Windows"
#if defined(_M_ARM)
#define PROGRAM_ARCHITECTURE "ARM32"
#elif defined(_M_ARM64)
#define PROGRAM_ARCHITECTURE "ARM64"
#elif defined(_WIN64)
#define PROGRAM_ARCHITECTURE "x86_64"
#else
#define PROGRAM_ARCHITECTURE "x86"
#endif
#else
#if defined(__linux__)
#define PROGRAM_PLATFORM "Linux"
#elif defined(__APPLE__)
#define PROGRAM_PLATFORM "MacOS"
#else
#define PROGRAM_PLATFORM "Unknown Platform"
#define PROGRAM_ARCHITECTURE "Unknown Architecture"
#endif
#if defined(__linux__) || defined(__APPLE__)
#if defined(__arm__)
#define PROGRAM_ARCHITECTURE "ARM32"
#elif defined(__aarch64__)
#if defined(__APPLE__)
#define PROGRAM_ARCHITECTURE "ARM64 (Apple Sillicon)"
#else
#define PROGRAM_ARCHITECTURE "ARM64"
#endif
#elif defined(__x86_64__)
#define PROGRAM_ARCHITECTURE "x86_64"
#elif defined(__i386__)
#define PROGRAM_ARCHITECTURE "x86"
#endif
#endif
#endif
#define PARSE_MODE(mode_a, character_a, color_a)                               \
  if (entry.mode & mode_a) {                                                   \
    tmk_setFontANSIColor(color_a, tmk_FontLayer_Foreground);                   \
  } else {                                                                     \
    tmk_resetFontColors();                                                     \
  }                                                                            \
  tmk_write("%c", entry.mode &mode_a ? character_a : '-');
#define PARSE_OPTION(option_a, action_a)                                       \
  if (!strcmp(cmdArguments.utf8Arguments[offset], "--" option_a)) {            \
    action_a;                                                                  \
    return 0;                                                                  \
  }
#define PROGRAM_NAME "dl"
#define PROGRAM_VERSION "v3.0.0"
#define DEBUG 0
#define SAVE_GREATER(buffer_a, value_a)                                        \
  if (value_a > buffer_a) {                                                    \
    buffer_a = value_a;                                                        \
  }

struct String {
  char *buffer;
  size_t length;
};

struct SIMultiplier {
  float value;
  char prefix;
};

#if defined(_WIN32)
struct Credential {
  struct String user;
  struct String domain;
  PSID sid;
};

struct Entry {
  char *name;
  char *size;
  struct Credential *credential;
  FILETIME modifiedTime;
  DWORD mode;
};
#else
struct Credential {
  struct String name;
  uid_t id;
};

struct Entry {
  char *name;
  char *link;
  char *size;
  struct Credential *user;
  struct Credential *group;
  time_t modifiedTime;
  mode_t mode;
};
#endif

struct ArenaAllocator {
  char *name;
  char *buffer;
  size_t use;
  size_t capacity;
  size_t unit;
};

#if defined(DEBUG)
static void debugArenaAllocator(struct ArenaAllocator *allocator);
#endif
#if defined(_WIN32)
static char *convertArenaUTF16ToUTF8(struct ArenaAllocator *allocator,
                                     const wchar_t *utf16String,
                                     size_t *utf8StringLength);
static struct Credential *findCredential(const wchar_t *utf16DirectoryPath,
                                         size_t globSize,
                                         PWIN32_FIND_DATAW entryData);
static void readDirectory(const char *utf8DirectoryPath,
                          const wchar_t *utf16DirectoryPath);
#else
static struct Credential *findCredential(bool isUser, unsigned int id);
static void readDirectory(const char *directoryPath);
#endif
static int sortEntriesAlphabetically(const void *entryI, const void *entryII);
static void writeLines(size_t totalLines, ...);
static char *formatModifiedDate(int month, int day, int year,
                                size_t *bufferSize);
static char *formatSize(size_t *bufferLength, unsigned long long entrySize,
                        bool isDirectory);
static int countDigits(size_t number);
static void writeErrorArguments(const char *format, va_list arguments);
static void writeError(const char *format, ...);
static void throwError(const char *format, ...);
static void writeHelp(void);
static void writeVersion(void);
static void *allocateHeapMemory(size_t totalBytes);
static void createArenaAllocator(const char *name, size_t unit, size_t capacity,
                                 struct ArenaAllocator **allocator);
static void *allocateArenaMemory(struct ArenaAllocator *allocator,
                                 size_t totalAllocations);
static void resetArenaAllocator(struct ArenaAllocator *allocator);
static void freeArenaMemory(struct ArenaAllocator *allocator,
                            size_t totalAllocations);
static void freeArenaAllocator(struct ArenaAllocator *allocator);
