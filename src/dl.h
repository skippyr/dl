#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <tdk.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define PROGRAM_NAME "dl"
#define PROGRAM_VERSION "v1.1.0"
#define TOTAL_CREDENTIALS_EXPECTED 20
#define TOTAL_TEMPORARY_ALLOCATIONS 500
#define AVERAGE_CREDENTIAL_NAME_LENGTH 16
#define SAVE_GREATER(a_buffer, a_value) \
	if (a_value > a_buffer) \
	{ \
		a_buffer = a_value; \
	}
#define PARSE_MODE(a_mode, a_character, a_color) \
	tdk_set256Color(entry.mode& a_mode ? a_color : tdk_Color_Default, tdk_Layer_Foreground); \
	tdk_write(entry.mode& a_mode ? a_character : "-");
#ifdef _WIN32
#define PROGRAM_PLATFORM "Windows"
#ifdef _WIN64
#define PROGRAM_ARCHITECTURE "x86_64"
#else
#define PROGRAM_ARCHITECTURE "x86"
#endif
#define SECURITY_DESCRIPTOR_BUFFER_SIZE 80
#define PARSE_OPTION(a_option, a_action) \
	if (!wcscmp(L"--" a_option, arguments[index])) \
	{ \
		a_action; \
		return 0; \
	}
#ifdef DEBUG
#define WRITE_WSTRING(a_string, a_size) \
	WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), a_string, a_size, NULL, NULL); \
	putchar('\n');
#endif
#else
#define PROGRAM_PLATFORM "Linux"
#ifdef __x86_64__
#define PROGRAM_ARCHITECTURE "x86_64"
#else
#define PROGRAM_ARCHITECTURE "x86"
#endif
#define PARSE_OPTION(a_option, a_action) \
	if (!strcmp("--" a_option, arguments[index])) \
	{ \
		a_action; \
		return 0; \
	}
#endif

enum LogType
{
	LogType_Warning,
	LogType_Error
};

struct ArenaAllocator
{
	char* name;
	char* buffer;
	size_t typeSize;
	size_t use;
	size_t capacity;
};

struct String
{
	char* buffer;
	size_t length;
};

struct SIMultiplier
{
	float value;
	char prefix;
};

#ifdef _WIN32
struct Entry
{
	char* name;
	char* size;
	struct Credential* credential;
	FILETIME modifiedTime;
	DWORD mode;
};

struct Credential
{
	struct String user;
	struct String domain;
	PSID sid;
};
#else
struct Entry
{
	char* name;
	char* size;
	char* link;
	struct Credential* user;
	struct Credential* group;
	time_t modifiedTime;
	mode_t mode;
};

struct Credential
{
	struct String name;
	uid_t id;
};
#endif

static struct ArenaAllocator* allocateArenaAllocator(const char* name, size_t typeSize, size_t capacity);
static void* allocateArenaMemory(size_t line, struct ArenaAllocator* allocator, size_t use);
static void* allocateHeapMemory(size_t line, size_t size);
static int countDigits(size_t number);
static void deallocateArenaAllocator(struct ArenaAllocator* allocator);
static void deallocateArenaMemory(size_t line, struct ArenaAllocator* allocator, size_t use);
static char* formatEntrySize(size_t* bufferLength, unsigned long long entrySize, int isDirectory);
static char* formatEntryModifiedDate(int month, int day, int year, size_t* bufferSize);
static void resetArenaAllocator(struct ArenaAllocator* allocator);
static void throwLog(size_t line, int type, const char* format, ...);
static void writeHelp(void);
static void writeLicense(void);
static void writeLines(size_t totalLines, ...);
static void writeVersion(void);
#ifdef DEBUG
static void dumpArenaAllocator(struct ArenaAllocator* allocator);
#endif
#ifdef _WIN32
static char* convertUTF16ToUTF8(size_t line, struct ArenaAllocator* allocator, const wchar_t* utf16String,
								size_t* utf8StringLength);
static struct Credential* findCredential(const wchar_t* utf16DirectoryPath, size_t globSize,
										 PWIN32_FIND_DATAW entryData);
static void readDirectory(const wchar_t* utf16DirectoryPath);
#else
static struct Credential* findCredential(uid_t id, int isUser);
static void readDirectory(const char* directoryPath);
static int sortEntriesAlphabetically(const void* entry0, const void* entry1);
#endif
