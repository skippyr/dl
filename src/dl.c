#include "dl.h"

static int g_exitCode = 0;
static struct ArenaAllocator* g_entriesAllocator;
static struct ArenaAllocator* g_entriesDataAllocator;
static struct ArenaAllocator* g_temporaryCharAllocator;
#ifdef _WIN32
static PSECURITY_DESCRIPTOR g_securityDescriptorBuffer;
static struct ArenaAllocator* g_credentialsAllocator;
static struct ArenaAllocator* g_credentialsDataAllocator;
static struct ArenaAllocator* g_temporaryWCharAllocator;
#else
static struct ArenaAllocator* g_userCredentialsAllocator;
static struct ArenaAllocator* g_userCredentialsDataAllocator;
static struct ArenaAllocator* g_groupCredentialsAllocator;
static struct ArenaAllocator* g_groupCredentialsDataAllocator;
#endif

static struct ArenaAllocator* allocateArenaAllocator(const char* name, size_t typeSize, size_t capacity)
{
	struct ArenaAllocator* allocator = allocateHeapMemory(__LINE__, sizeof(struct ArenaAllocator));
	allocator->buffer = allocateHeapMemory(__LINE__, typeSize * capacity);
	allocator->typeSize = typeSize;
	allocator->use = 0;
	allocator->capacity = capacity;
	size_t nameSize = strlen(name) + 1;
	allocator->name = allocateHeapMemory(__LINE__, nameSize);
	memcpy(allocator->name, name, nameSize);
	return allocator;
}

static void* allocateArenaMemory(size_t line, struct ArenaAllocator* allocator, size_t use)
{
	if (allocator->use + use > allocator->capacity)
	{
		throwLog(line, LogType_Error, "can not allocate %zu %s (%zuB) on arena \"%s\" (%zu %s [%zuB]/%zu %s [%zuB]).",
				 use, use == 1 ? "item" : "items", allocator->typeSize * use, allocator->name, allocator->use,
				 allocator->use == 1 ? "item" : "items", allocator->typeSize * allocator->use, allocator->capacity,
				 allocator->capacity == 1 ? "item" : "items", allocator->typeSize * allocator->capacity);
		return NULL;
	}
	void* allocation = allocator->buffer + allocator->typeSize * allocator->use;
	allocator->use += use;
	return allocation;
}

static void* allocateHeapMemory(size_t line, size_t size)
{
	void* allocation = malloc(size);
	if (allocation)
	{
		return allocation;
	}
	throwLog(line, LogType_Error, "can not allocate %zuB of memory on the heap.", size);
	return NULL;
}

static int countDigits(size_t number)
{
	int totalDigits;
	for (totalDigits = !number; number; number /= 10)
	{
		++totalDigits;
	}
	return totalDigits;
}

static void deallocateArenaAllocator(struct ArenaAllocator* allocator)
{
	free(allocator->name);
	free(allocator->buffer);
	free(allocator);
}

static void deallocateArenaMemory(size_t line, struct ArenaAllocator* allocator, size_t use)
{
	if (use > allocator->use)
	{
		throwLog(line, LogType_Error,
				 "can not deallocate %zu %s (%zuB) from arena \"%s\" (%zu %s [%zuB]/%zu %s [%zuB]).", use,
				 use == 1 ? "item" : "items", allocator->typeSize * use, allocator->name, allocator->use,
				 allocator->use == 1 ? "item" : "items", allocator->typeSize * allocator->use, allocator->capacity,
				 allocator->capacity ? "item" : "items", allocator->typeSize * allocator->capacity);
	}
	allocator->use -= use;
}

static char* formatEntrySize(size_t* bufferLength, unsigned long long entrySize, int isDirectory)
{
	if (isDirectory)
	{
		*bufferLength = 0;
		return NULL;
	}
	char formatBuffer[9];
	struct SIMultiplier multipliers[] = {{1099511627776, 'T'}, {1073741824, 'G'}, {1048576, 'M'}, {1024, 'k'}};
	for (size_t index = 0; index < 4; ++index)
	{
		if (entrySize >= multipliers[index].value)
		{
			float formatedSize = entrySize / multipliers[index].value;
			sprintf(formatBuffer, "%.1f%cB", formatedSize, multipliers[index].prefix);
			goto end;
		}
	}
	sprintf(formatBuffer, "%lldB", entrySize);
end:
	*bufferLength = strlen(formatBuffer);
	char* buffer = allocateArenaMemory(__LINE__, g_entriesDataAllocator, *bufferLength + 1);
	memcpy(buffer, formatBuffer, *bufferLength + 1);
	return buffer;
}

static char* formatEntryModifiedDate(int month, int day, int year, size_t* bufferSize)
{
	char formatBuffer[12];
	sprintf(formatBuffer, "%s/%02d/%04d",
			!month        ? "Jan"
			: month == 1  ? "Feb"
			: month == 2  ? "Mar"
			: month == 3  ? "Apr"
			: month == 4  ? "May"
			: month == 5  ? "Jun"
			: month == 6  ? "Jul"
			: month == 7  ? "Aug"
			: month == 8  ? "Sep"
			: month == 9  ? "Oct"
			: month == 10 ? "Nov"
						  : "Dec",
			day, year);
	*bufferSize = strlen(formatBuffer) + 1;
	char* buffer = allocateArenaMemory(__LINE__, g_temporaryCharAllocator, *bufferSize);
	memcpy(buffer, formatBuffer, *bufferSize);
	return buffer;
}

static void resetArenaAllocator(struct ArenaAllocator* allocator)
{
	allocator->use = 0;
}

static void throwLog(size_t line, int type, const char* format, ...)
{
	tdk_set256Color(type == LogType_Warning ? tdk_Color_Yellow : tdk_Color_Red, tdk_Layer_Foreground);
	tdk_writeError("[%s] ", type == LogType_Warning ? "WARNING" : "ERROR");
	tdk_set256Color(tdk_Color_LightBlack, tdk_Layer_Foreground);
	tdk_write("(line %zu) ", line);
	tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	tdk_setWeight(tdk_Weight_Bold);
	tdk_writeError("%s: ", PROGRAM_NAME, line);
	tdk_setWeight(tdk_Weight_Default);
	fflush(stdout);
	va_list arguments;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	tdk_writeErrorLine("");
	if (type == LogType_Warning)
	{
		g_exitCode = 1;
	}
	else
	{
		tdk_writeErrorLine("Program exited with exit code 1.");
		exit(1);
	}
}

static void writeHelp(void)
{
	tdk_setWeight(tdk_Weight_Bold);
	tdk_writeLine("SYNOPSIS");
	tdk_setWeight(tdk_Weight_Default);
	tdk_writeLine("--------");
	tdk_setWeight(tdk_Weight_Bold);
	tdk_write("    dl ");
	tdk_setWeight(tdk_Weight_Default);
	tdk_set256Color(tdk_Color_LightBlack, tdk_Layer_Foreground);
	tdk_write("[OPTIONS]");
	tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	tdk_write("... ");
	tdk_set256Color(tdk_Color_Red, tdk_Layer_Foreground);
	tdk_write("[PATH]");
	tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	tdk_writeLine("...");
	tdk_write("    Write information about the entries inside of directories given their ");
	tdk_set256Color(tdk_Color_Red, tdk_Layer_Foreground);
	tdk_write("PATH");
	tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	tdk_writeLine("(s).");
	tdk_writeLine("    If no path is provided, the current directory is considered.");
	tdk_writeLine("");
	tdk_setWeight(tdk_Weight_Bold);
	tdk_writeLine("OPTIONS");
	tdk_setWeight(tdk_Weight_Default);
	tdk_writeLine("-------");
	tdk_set256Color(tdk_Color_Green, tdk_Layer_Foreground);
	tdk_write("    --help");
	tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	tdk_writeLine(": write these help instructions.");
	tdk_set256Color(tdk_Color_Green, tdk_Layer_Foreground);
	tdk_write("    --license");
	tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	tdk_writeLine(": write its license and copyright notice.");
	tdk_set256Color(tdk_Color_Green, tdk_Layer_Foreground);
	tdk_write("    --version");
	tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	tdk_writeLine(": write its version, platform and archictecture.");
	tdk_writeLine("");
	tdk_setWeight(tdk_Weight_Bold);
	tdk_writeLine("SOURCE CODE");
	tdk_setWeight(tdk_Weight_Default);
	tdk_writeLine("-----------");
	tdk_write("Its source code is available at: <");
	tdk_set256Color(tdk_Color_Red, tdk_Layer_Foreground);
	tdk_setEffect(tdk_Effect_Underline, 1);
	tdk_write("https://github.com/skippyr/dl");
	tdk_setEffect(tdk_Effect_Underline, 0);
	tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	tdk_writeLine(">.");
}

static void writeLicense(void)
{
	tdk_setWeight(tdk_Weight_Bold);
	tdk_writeLine("BSD 3-Clause License");
	tdk_setWeight(tdk_Weight_Default);
	tdk_write("Copyright (c) 2024, Sherman Rofeman <");
	tdk_set256Color(tdk_Color_Red, tdk_Layer_Foreground);
	tdk_setEffect(tdk_Effect_Underline, 1);
	tdk_write("skippyr.developer@gmail.com");
	tdk_setEffect(tdk_Effect_Underline, 0);
	tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	tdk_writeLine(">.");
	tdk_writeLine("");
	tdk_writeLine("This is free software licensed under the BSD-3-Clause License that comes WITH NO WARRANTY. Refer to "
				  "the LICENSE file");
	tdk_writeLine("that comes in its source code for license and copyright details.");
}

static void writeLines(size_t totalLines, ...)
{
	va_list arguments;
	va_start(arguments, totalLines);
	for (size_t index = 0; index < totalLines; ++index)
	{
		for (int length = va_arg(arguments, int); length; --length)
		{
			tdk_write("-");
		}
		if (index < totalLines - 1)
		{
			tdk_write(" ");
		}
	}
	va_end(arguments);
	tdk_writeLine("");
}

static void writeVersion(void)
{
	tdk_setWeight(tdk_Weight_Bold);
	tdk_write("%s ", PROGRAM_NAME);
	tdk_setWeight(tdk_Weight_Default);
	tdk_writeLine("%s (compiled for %s %s).", PROGRAM_VERSION, PROGRAM_PLATFORM, PROGRAM_ARCHITECTURE);
}

#ifdef DEBUG
static void dumpArenaAllocator(struct ArenaAllocator* allocator)
{
	tdk_writeLine("%s %zu %s (%zuB)/%zu %s (%zuB)", allocator->name, allocator->use,
				  allocator->use == 1 ? "item" : "items", allocator->typeSize * allocator->use, allocator->capacity,
				  allocator->capacity == 1 ? "item" : "items", allocator->typeSize * allocator->capacity);
}
#endif

#ifdef _WIN32
static char* convertUTF16ToUTF8(size_t line, struct ArenaAllocator* allocator, const wchar_t* utf16String,
								size_t* utf8StringLength)
{
	size_t utf8StringSize = WideCharToMultiByte(CP_UTF8, 0, utf16String, -1, NULL, 0, NULL, NULL);
	char* utf8String = allocateArenaMemory(line, allocator, utf8StringSize);
	WideCharToMultiByte(CP_UTF8, 0, utf16String, -1, utf8String, utf8StringSize, NULL, NULL);
	if (utf8StringLength)
	{
		*utf8StringLength = utf8StringSize - 1;
	}
	return utf8String;
}

static struct Credential* findCredential(const wchar_t* utf16DirectoryPath, size_t globSize,
										 PWIN32_FIND_DATAW entryData)
{
	size_t entryPathSize = wcslen(entryData->cFileName) + globSize - 1;
	wchar_t* entryPath = allocateArenaMemory(__LINE__, g_temporaryWCharAllocator, entryPathSize);
	memcpy(entryPath, utf16DirectoryPath, (globSize - 3) * sizeof(wchar_t));
	entryPath[globSize - 3] = '\\';
	memcpy(entryPath + globSize - 2, entryData->cFileName, (entryPathSize - globSize + 2) * sizeof(wchar_t));
	DWORD securityDescriptorSize;
	if (!GetFileSecurityW(entryPath, OWNER_SECURITY_INFORMATION, g_securityDescriptorBuffer,
						  SECURITY_DESCRIPTOR_BUFFER_SIZE, &securityDescriptorSize))
	{
		deallocateArenaMemory(__LINE__, g_temporaryWCharAllocator, entryPathSize);
		return NULL;
	}
	deallocateArenaMemory(__LINE__, g_temporaryWCharAllocator, entryPathSize);
	PSID sid;
	BOOL isOwnerDefaulted;
	GetSecurityDescriptorOwner(g_securityDescriptorBuffer, &sid, &isOwnerDefaulted);
	for (size_t index = 0; index < g_credentialsAllocator->use; ++index)
	{
		if (EqualSid(((struct Credential*)g_credentialsAllocator->buffer + index)->sid, sid))
		{
			return (struct Credential*)g_credentialsAllocator->buffer + index;
		}
	}
	DWORD utf16UserSize = 0;
	DWORD utf16DomainSize = 0;
	SID_NAME_USE use;
	LookupAccountSidW(NULL, sid, NULL, &utf16UserSize, NULL, &utf16DomainSize, &use);
	wchar_t* utf16User = allocateArenaMemory(__LINE__, g_temporaryWCharAllocator, utf16UserSize);
	wchar_t* utf16Domain = allocateArenaMemory(__LINE__, g_temporaryWCharAllocator, utf16DomainSize);
	LookupAccountSidW(NULL, sid, utf16User, &utf16UserSize, utf16Domain, &utf16DomainSize, &use);
	struct Credential* credential = allocateArenaMemory(__LINE__, g_credentialsAllocator, 1);
	DWORD sidLength = GetLengthSid(sid);
	credential->sid = allocateArenaMemory(__LINE__, g_credentialsDataAllocator, sidLength);
	CopySid(sidLength, credential->sid, sid);
	credential->user.buffer =
		convertUTF16ToUTF8(__LINE__, g_credentialsDataAllocator, utf16User, &credential->user.length);
	credential->domain.buffer =
		convertUTF16ToUTF8(__LINE__, g_credentialsDataAllocator, utf16Domain, &credential->domain.length);
	deallocateArenaMemory(__LINE__, g_temporaryWCharAllocator, utf16UserSize + utf16DomainSize + 2);
	return credential;
}

static void readDirectory(const wchar_t* utf16DirectoryPath)
{
	size_t globSize = wcslen(utf16DirectoryPath) + 3;
	wchar_t* glob = allocateArenaMemory(__LINE__, g_temporaryWCharAllocator, globSize);
	memcpy(glob, utf16DirectoryPath, (globSize - 3) * sizeof(wchar_t));
	glob[globSize - 3] = '\\';
	glob[globSize - 2] = '*';
	glob[globSize - 1] = 0;
	WIN32_FIND_DATAW entryData;
	HANDLE directory = FindFirstFileW(glob, &entryData);
	deallocateArenaMemory(__LINE__, g_temporaryWCharAllocator, globSize);
	if (directory == INVALID_HANDLE_VALUE)
	{
		size_t utf8DirectoryPathLength;
		char* utf8DirectoryPath =
			convertUTF16ToUTF8(__LINE__, g_temporaryCharAllocator, utf16DirectoryPath, &utf8DirectoryPathLength);
		DWORD directoryAttributes = GetFileAttributesW(utf16DirectoryPath);
		throwLog(__LINE__, LogType_Warning,
				 directoryAttributes == INVALID_FILE_ATTRIBUTES   ? "can not find the entry \"%s\"."
				 : directoryAttributes & FILE_ATTRIBUTE_DIRECTORY ? "can not open the directory \"%s\"."
																  : "the entry \"%s\" is not a directory.",
				 utf8DirectoryPath);
		deallocateArenaMemory(__LINE__, g_temporaryCharAllocator, utf8DirectoryPathLength + 1);
		return;
	}
	int indexColumnLength = 3;
	int userColumnLength = 4;
	int domainColumnLength = 6;
	int sizeColumnLength = 4;
	do
	{
		if (*entryData.cFileName == '.' &&
			(!entryData.cFileName[1] || (entryData.cFileName[1] == '.' && !entryData.cFileName[2])))
		{
			continue;
		}
		struct Entry* entry = allocateArenaMemory(__LINE__, g_entriesAllocator, 1);
		entry->credential = findCredential(utf16DirectoryPath, globSize, &entryData);
		entry->mode = entryData.dwFileAttributes;
		entry->modifiedTime = entryData.ftLastWriteTime;
		entry->name = convertUTF16ToUTF8(__LINE__, g_entriesDataAllocator, entryData.cFileName, NULL);
		size_t sizeLength;
		entry->size =
			formatEntrySize(&sizeLength, ((ULARGE_INTEGER){entryData.nFileSizeLow, entryData.nFileSizeHigh}).QuadPart,
							entryData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
		if (entry->credential)
		{
			SAVE_GREATER(userColumnLength, entry->credential->user.length);
			SAVE_GREATER(domainColumnLength, entry->credential->domain.length);
		}
		SAVE_GREATER(sizeColumnLength, sizeLength);
	} while (FindNextFileW(directory, &entryData));
	FindClose(directory);
	int totalDigitsInMaximumIndex = countDigits(g_entriesAllocator->use);
	SAVE_GREATER(indexColumnLength, totalDigitsInMaximumIndex);
	tdk_set256Color(tdk_Color_Yellow, tdk_Layer_Foreground);
	tdk_write(" ");
	tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	tdk_setWeight(tdk_Weight_Bold);
	if (((*utf16DirectoryPath > 'A' && *utf16DirectoryPath < 'Z') ||
		 (*utf16DirectoryPath > 'a' && *utf16DirectoryPath < 'z')) &&
		utf16DirectoryPath[1] == ':' && !utf16DirectoryPath[2])
	{
		tdk_writeLine("%c:\\:", *utf16DirectoryPath);
	}
	else
	{
		DWORD utf16DirectoryFullPathSize = GetFullPathNameW(utf16DirectoryPath, 0, NULL, NULL);
		wchar_t* utf16DirectoryFullPath =
			allocateArenaMemory(__LINE__, g_temporaryWCharAllocator, utf16DirectoryFullPathSize);
		GetFullPathNameW(utf16DirectoryPath, utf16DirectoryFullPathSize, utf16DirectoryFullPath, NULL);
		size_t utf8DirectoryFullPathLength;
		char* utf8DirectoryFullPath = convertUTF16ToUTF8(__LINE__, g_temporaryCharAllocator, utf16DirectoryFullPath,
														 &utf8DirectoryFullPathLength);
		if (utf8DirectoryFullPathLength > 3 && utf8DirectoryFullPath[utf8DirectoryFullPathLength - 1] == '\\')
		{
			utf8DirectoryFullPath[utf8DirectoryFullPathLength - 1] = 0;
		}
		tdk_writeLine("%s%s:", utf8DirectoryFullPath, utf8DirectoryFullPathLength < 3 ? "\\" : "");
		deallocateArenaMemory(__LINE__, g_temporaryCharAllocator, utf8DirectoryFullPathLength + 1);
		deallocateArenaMemory(__LINE__, g_temporaryWCharAllocator, utf16DirectoryFullPathSize);
	}
	tdk_writeLine("%*s %-*s %-*s %-*s %*s %-*s Name", indexColumnLength, "No.", domainColumnLength, "Domain",
				  userColumnLength, "User", 17, "Modified Date", sizeColumnLength, "Size", 5, "Mode");
	tdk_setWeight(tdk_Weight_Default);
	writeLines(7, indexColumnLength, domainColumnLength, userColumnLength, 17, sizeColumnLength, 5, 20);
	if (!g_entriesAllocator->use)
	{
		tdk_set256Color(tdk_Color_LightBlack, tdk_Layer_Foreground);
		tdk_writeLine("%*s", 23 + indexColumnLength + domainColumnLength + userColumnLength + sizeColumnLength,
					  "DIRECTORY IS EMPTY");
		tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	}
	for (size_t index = 0; index < g_entriesAllocator->use; ++index)
	{
		struct Entry entry = *((struct Entry*)g_entriesAllocator->buffer + index);
		tdk_write("%*zu ", indexColumnLength, index + 1);
		if (entry.credential)
		{
			tdk_set256Color(tdk_Color_Red, tdk_Layer_Foreground);
			tdk_write("%-*s ", domainColumnLength, entry.credential->domain.buffer);
			tdk_set256Color(tdk_Color_Green, tdk_Layer_Foreground);
			tdk_write("%-*s ", userColumnLength, entry.credential->user.buffer);
		}
		else
		{
			tdk_write("%-*c %-*c ", domainColumnLength, '-', userColumnLength, '-');
		}
		tdk_set256Color(tdk_Color_Yellow, tdk_Layer_Foreground);
		SYSTEMTIME systemModifiedTime;
		FileTimeToSystemTime(&entry.modifiedTime, &systemModifiedTime);
		size_t modifiedDateSize;
		char* modifiedDate = formatEntryModifiedDate(systemModifiedTime.wMonth - 1, systemModifiedTime.wDay,
													 systemModifiedTime.wYear, &modifiedDateSize);
		tdk_write("%s ", modifiedDate);
		deallocateArenaMemory(__LINE__, g_temporaryCharAllocator, modifiedDateSize);
		tdk_set256Color(tdk_Color_Magenta, tdk_Layer_Foreground);
		tdk_write("%02d:%02d ", systemModifiedTime.wHour, systemModifiedTime.wMinute);
		if (entry.size)
		{
			tdk_set256Color(tdk_Color_Red, tdk_Layer_Foreground);
			tdk_write("%*s ", sizeColumnLength, entry.size);
		}
		else
		{
			tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
			tdk_write("%*c ", sizeColumnLength, '-');
		}
		PARSE_MODE(FILE_ATTRIBUTE_HIDDEN, "h", tdk_Color_Red);
		PARSE_MODE(FILE_ATTRIBUTE_ARCHIVE, "a", tdk_Color_Green);
		PARSE_MODE(FILE_ATTRIBUTE_READONLY, "r", tdk_Color_Yellow);
		PARSE_MODE(FILE_ATTRIBUTE_TEMPORARY, "t", tdk_Color_Red);
		PARSE_MODE(FILE_ATTRIBUTE_REPARSE_POINT, "l", tdk_Color_Green);
		tdk_set256Color(entry.mode & FILE_ATTRIBUTE_DIRECTORY ? tdk_Color_Yellow : tdk_Color_Default,
						tdk_Layer_Foreground);
		tdk_write(entry.mode & FILE_ATTRIBUTE_DIRECTORY ? "  " : "  ");
		tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
		tdk_writeLine("%s", entry.name);
	}
	resetArenaAllocator(g_entriesAllocator);
	resetArenaAllocator(g_entriesDataAllocator);
}
#else
static struct Credential* findCredential(uid_t id, int isUser)
{
	struct ArenaAllocator* credentialsAllocator = isUser ? g_userCredentialsAllocator : g_groupCredentialsAllocator;
	struct ArenaAllocator* credentialDataAllocator =
		isUser ? g_userCredentialsDataAllocator : g_groupCredentialsDataAllocator;
	for (size_t index = 0; index < credentialsAllocator->use; ++index)
	{
		if (((struct Credential*)credentialsAllocator->buffer + index)->id == id)
		{
			return (struct Credential*)credentialsAllocator->buffer + index;
		}
	}
	char* name;
	if (isUser)
	{
		struct passwd* user = getpwuid(id);
		if (!user)
		{
			return NULL;
		}
		name = user->pw_name;
	}
	else
	{
		struct group* group = getgrgid(id);
		if (!group)
		{
			return NULL;
		}
		name = group->gr_name;
	}
	struct Credential* credential = allocateArenaMemory(__LINE__, credentialsAllocator, 1);
	credential->id = id;
	credential->name.length = strlen(name);
	credential->name.buffer = allocateArenaMemory(__LINE__, credentialDataAllocator, credential->name.length + 1);
	memcpy(credential->name.buffer, name, credential->name.length + 1);
	return credential;
}

static void readDirectory(const char* directoryPath)
{
	DIR* directoryStream = opendir(directoryPath);
	if (!directoryStream)
	{
		struct stat directoryStatus;
		throwLog(__LINE__, LogType_Warning,
				 stat(directoryPath, &directoryStatus) ? "can not find the entry \"%s\"."
				 : S_ISDIR(directoryStatus.st_mode)    ? "can not open the directory \"%s\"."
													   : "the entry \"%s\" is not a directory.",
				 directoryPath);
		return;
	}
	size_t directoryPathLength = strlen(directoryPath);
	int indexColumnLength = 3;
	int userColumnLength = 4;
	int groupColumnLength = 5;
	int sizeColumnLength = 4;
	for (struct dirent* entryData; (entryData = readdir(directoryStream));)
	{
		if (*entryData->d_name == '.' &&
			(!entryData->d_name[1] || (entryData->d_name[1] == '.' && !entryData->d_name[2])))
		{
			continue;
		}
		struct Entry* entry = allocateArenaMemory(__LINE__, g_entriesAllocator, 1);
		size_t entryNameSize = strlen(entryData->d_name) + 1;
		size_t entryPathSize = directoryPathLength + entryNameSize + 1;
		char* entryPath = allocateArenaMemory(__LINE__, g_temporaryCharAllocator, entryPathSize);
		memcpy(entryPath, directoryPath, directoryPathLength);
		entryPath[directoryPathLength] = '/';
		memcpy(entryPath + directoryPathLength + 1, entryData->d_name, entryNameSize);
		struct stat entryStatus;
		lstat(entryPath, &entryStatus);
		if (S_ISLNK(entryStatus.st_mode))
		{
			char link[256];
			link[readlink(entryPath, link, sizeof(link))] = 0;
			size_t linkSize = strlen(link) + 1;
			entry->link = allocateArenaMemory(__LINE__, g_entriesDataAllocator, linkSize);
			memcpy(entry->link, link, linkSize);
		}
		else
		{
			entry->link = NULL;
		}
		deallocateArenaMemory(__LINE__, g_temporaryCharAllocator, entryPathSize);
		entry->modifiedTime = entryStatus.st_mtim.tv_sec;
		entry->mode = entryStatus.st_mode;
		size_t sizeLength;
		entry->size = formatEntrySize(&sizeLength, entryStatus.st_size, S_ISDIR(entryStatus.st_mode));
		entry->user = findCredential(entryStatus.st_uid, 1);
		entry->group = findCredential(entryStatus.st_gid, 0);
		entry->name = allocateArenaMemory(__LINE__, g_entriesDataAllocator, entryNameSize);
		memcpy(entry->name, entryData->d_name, entryNameSize);
		if (entry->user)
		{
			SAVE_GREATER(userColumnLength, entry->user->name.length);
		}
		if (entry->group)
		{
			SAVE_GREATER(groupColumnLength, entry->group->name.length);
		}
		SAVE_GREATER(sizeColumnLength, sizeLength);
	}
	closedir(directoryStream);
	int totalDigitsInMaximumIndex = countDigits(g_entriesAllocator->use);
	SAVE_GREATER(indexColumnLength, totalDigitsInMaximumIndex);
	tdk_set256Color(tdk_Color_Yellow, tdk_Layer_Foreground);
	tdk_write(" ");
	tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	char* directoryFullPath = allocateArenaMemory(__LINE__, g_temporaryCharAllocator, 256);
	realpath(directoryPath, directoryFullPath);
	tdk_setWeight(tdk_Weight_Bold);
	tdk_writeLine("%s:", directoryFullPath);
	tdk_setWeight(tdk_Weight_Default);
	deallocateArenaMemory(__LINE__, g_temporaryCharAllocator, 256);
	qsort(g_entriesAllocator->buffer, g_entriesAllocator->use, sizeof(struct Entry), sortEntriesAlphabetically);
	tdk_setWeight(tdk_Weight_Bold);
	tdk_writeLine("%*s %-*s %-*s %-*s %*s %-*s Name", indexColumnLength, "No.", groupColumnLength, "Group",
				  userColumnLength, "User", 17, "Modified Date", sizeColumnLength, "Size", 13, "Mode");
	tdk_setWeight(tdk_Weight_Default);
	writeLines(7, indexColumnLength, groupColumnLength, userColumnLength, 17, sizeColumnLength, 13, 20);
	if (!g_entriesAllocator->use)
	{
		tdk_set256Color(tdk_Color_LightBlack, tdk_Layer_Foreground);
		tdk_writeLine("%*s", 29 + indexColumnLength + groupColumnLength + userColumnLength + sizeColumnLength,
					  "DIRECTORY IS EMPTY");
		tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
	}
	for (size_t index = 0; index < g_entriesAllocator->use; ++index)
	{
		struct Entry entry = *((struct Entry*)g_entriesAllocator->buffer + index);
		tdk_write("%*zu ", indexColumnLength, index + 1);
		if (entry.group)
		{
			tdk_set256Color(tdk_Color_Red, tdk_Layer_Foreground);
			tdk_write("%-*s ", groupColumnLength, entry.group->name.buffer);
		}
		else
		{
			tdk_write("%-*c ", groupColumnLength, '-');
		}
		if (entry.user)
		{
			tdk_set256Color(tdk_Color_Green, tdk_Layer_Foreground);
			tdk_write("%-*s ", userColumnLength, entry.user->name.buffer);
		}
		else
		{
			tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
			tdk_write("%-*c ", userColumnLength, '-');
		}
		struct tm* systemModifiedTime = localtime(&entry.modifiedTime);
		size_t modifiedDateSize;
		char* modifiedDate = formatEntryModifiedDate(systemModifiedTime->tm_mon, systemModifiedTime->tm_mday,
													 systemModifiedTime->tm_year + 1900, &modifiedDateSize);
		tdk_set256Color(tdk_Color_Yellow, tdk_Layer_Foreground);
		tdk_write("%s ", modifiedDate);
		deallocateArenaMemory(__LINE__, g_temporaryCharAllocator, modifiedDateSize);
		tdk_set256Color(tdk_Color_Magenta, tdk_Layer_Foreground);
		tdk_write("%02d:%02d ", systemModifiedTime->tm_hour, systemModifiedTime->tm_min);
		if (entry.size)
		{
			tdk_set256Color(tdk_Color_Red, tdk_Layer_Foreground);
			tdk_write("%*s ", sizeColumnLength, entry.size);
		}
		else
		{
			tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
			tdk_write("%*c ", sizeColumnLength, '-');
		}
		PARSE_MODE(S_IRUSR, "r", tdk_Color_Red);
		PARSE_MODE(S_IWUSR, "w", tdk_Color_Green);
		PARSE_MODE(S_IXUSR, "x", tdk_Color_Yellow);
		PARSE_MODE(S_IRGRP, "r", tdk_Color_Red);
		PARSE_MODE(S_IWGRP, "w", tdk_Color_Green);
		PARSE_MODE(S_IXGRP, "x", tdk_Color_Yellow);
		PARSE_MODE(S_IROTH, "r", tdk_Color_Red);
		PARSE_MODE(S_IWOTH, "w", tdk_Color_Green);
		PARSE_MODE(S_IXOTH, "x", tdk_Color_Yellow);
		tdk_set256Color(tdk_Color_Magenta, tdk_Layer_Foreground);
		tdk_write(" %03o ", entry.mode & (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH |
										  S_IWOTH | S_IXOTH));
		tdk_set256Color(S_ISDIR(entry.mode)    ? tdk_Color_Yellow
						: S_ISLNK(entry.mode)  ? tdk_Color_Blue
						: S_ISBLK(entry.mode)  ? tdk_Color_Magenta
						: S_ISCHR(entry.mode)  ? tdk_Color_Green
						: S_ISFIFO(entry.mode) ? tdk_Color_Blue
						: S_ISREG(entry.mode)  ? tdk_Color_Default
											   : tdk_Color_Cyan,
						tdk_Layer_Foreground);
		tdk_write(S_ISDIR(entry.mode)    ? " "
				  : S_ISLNK(entry.mode)  ? "󰌷 "
				  : S_ISBLK(entry.mode)  ? "󰇖 "
				  : S_ISCHR(entry.mode)  ? "󱣴 "
				  : S_ISFIFO(entry.mode) ? "󰟦 "
				  : S_ISREG(entry.mode)  ? " "
										 : "󱄙 ");
		tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
		tdk_write("%s", entry.name);
		if (entry.link)
		{
			tdk_set256Color(tdk_Color_Blue, tdk_Layer_Foreground);
			tdk_write(" -> ");
			tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
			tdk_writeLine(entry.link);
		}
		else
		{
			tdk_writeLine("");
		}
	}
	resetArenaAllocator(g_entriesAllocator);
	resetArenaAllocator(g_entriesDataAllocator);
}

static int sortEntriesAlphabetically(const void* entry0, const void* entry1)
{
	return strcmp(((struct Entry*)entry0)->name, ((struct Entry*)entry1)->name);
}
#endif

#ifdef _WIN32
int main(void)
{
	g_securityDescriptorBuffer = allocateHeapMemory(__LINE__, SECURITY_DESCRIPTOR_BUFFER_SIZE);
	g_entriesAllocator = allocateArenaAllocator("g_entriesAllocator", sizeof(struct Entry), 20000);
	g_entriesDataAllocator = allocateArenaAllocator("g_entriesDataAllocator", sizeof(char), 2097152);
	g_temporaryCharAllocator =
		allocateArenaAllocator("g_temporaryCharAllocator", sizeof(char), TOTAL_TEMPORARY_ALLOCATIONS);
	g_temporaryWCharAllocator =
		allocateArenaAllocator("g_temporaryWCharAllocator", sizeof(wchar_t), TOTAL_TEMPORARY_ALLOCATIONS);
	g_credentialsAllocator =
		allocateArenaAllocator("g_credentialsAllocator", sizeof(struct Credential), TOTAL_CREDENTIALS_EXPECTED);
	g_credentialsDataAllocator = allocateArenaAllocator(
		"g_credentialsDataAllocator", sizeof(char), AVERAGE_CREDENTIAL_NAME_LENGTH * 2 * TOTAL_CREDENTIALS_EXPECTED);
	int totalArguments;
	LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &totalArguments);
	if (totalArguments == 1)
	{
		readDirectory(L".");
		goto end;
	}
	for (int index = 1; index < totalArguments; ++index)
	{
		PARSE_OPTION("help", writeHelp());
		PARSE_OPTION("license", writeLicense());
		PARSE_OPTION("version", writeVersion());
	}
	for (int index = 1; index < totalArguments; ++index)
	{
		readDirectory(arguments[index]);
	}
end:
#ifdef DEBUG
	dumpArenaAllocator(g_entriesAllocator);
	dumpArenaAllocator(g_entriesDataAllocator);
	dumpArenaAllocator(g_temporaryCharAllocator);
	dumpArenaAllocator(g_temporaryWCharAllocator);
	dumpArenaAllocator(g_credentialsAllocator);
	dumpArenaAllocator(g_credentialsDataAllocator);
#endif
	LocalFree(arguments);
	free(g_securityDescriptorBuffer);
	deallocateArenaAllocator(g_entriesAllocator);
	deallocateArenaAllocator(g_entriesDataAllocator);
	deallocateArenaAllocator(g_temporaryCharAllocator);
	deallocateArenaAllocator(g_temporaryWCharAllocator);
	deallocateArenaAllocator(g_credentialsAllocator);
	deallocateArenaAllocator(g_credentialsDataAllocator);
	return g_exitCode;
}
#else
int main(int totalArguments, const char** arguments)
{
	g_entriesAllocator = allocateArenaAllocator("g_entriesAllocator", sizeof(struct Entry), 20000);
	g_entriesDataAllocator = allocateArenaAllocator("g_entriesDataAllocator", sizeof(char), 2097152);
	g_temporaryCharAllocator =
		allocateArenaAllocator("g_temporaryCharAllocator", sizeof(char), TOTAL_TEMPORARY_ALLOCATIONS);
	g_userCredentialsAllocator =
		allocateArenaAllocator("g_userCredentialsAllocator", sizeof(struct Credential), TOTAL_CREDENTIALS_EXPECTED);
	g_userCredentialsDataAllocator = allocateArenaAllocator(
		"g_userCredentialsDataAllocator", sizeof(char), AVERAGE_CREDENTIAL_NAME_LENGTH * TOTAL_CREDENTIALS_EXPECTED);
	g_groupCredentialsAllocator =
		allocateArenaAllocator("g_groupCredentialsAllocator", sizeof(struct Credential), TOTAL_CREDENTIALS_EXPECTED);
	g_groupCredentialsDataAllocator = allocateArenaAllocator(
		"g_groupCredentialsDataAllocator", sizeof(char), AVERAGE_CREDENTIAL_NAME_LENGTH * TOTAL_CREDENTIALS_EXPECTED);
	if (totalArguments == 1)
	{
		readDirectory(".");
		goto exit;
	}
	for (int index = 1; index < totalArguments; ++index)
	{
		PARSE_OPTION("help", writeHelp());
		PARSE_OPTION("license", writeLicense());
		PARSE_OPTION("version", writeVersion());
	}
	for (int index = 1; index < totalArguments; ++index)
	{
		readDirectory(arguments[index]);
	}
exit:
#ifdef DEBUG
	dumpArenaAllocator(g_entriesAllocator);
	dumpArenaAllocator(g_entriesDataAllocator);
	dumpArenaAllocator(g_temporaryCharAllocator);
	dumpArenaAllocator(g_userCredentialsAllocator);
	dumpArenaAllocator(g_userCredentialsDataAllocator);
	dumpArenaAllocator(g_groupCredentialsAllocator);
	dumpArenaAllocator(g_groupCredentialsDataAllocator);
#endif
	deallocateArenaAllocator(g_entriesAllocator);
	deallocateArenaAllocator(g_entriesDataAllocator);
	deallocateArenaAllocator(g_temporaryCharAllocator);
	deallocateArenaAllocator(g_userCredentialsAllocator);
	deallocateArenaAllocator(g_userCredentialsDataAllocator);
	deallocateArenaAllocator(g_groupCredentialsAllocator);
	deallocateArenaAllocator(g_groupCredentialsDataAllocator);
	return g_exitCode;
}
#endif
