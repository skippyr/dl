#include "dl.h"

static struct ArenaAllocator* g_userCredentialsAllocator;
static struct ArenaAllocator* g_groupCredentialsAllocator;
static struct ArenaAllocator* g_credentialsDataAllocator;
static struct ArenaAllocator* g_entriesAllocator;
static struct ArenaAllocator* g_entriesDataAllocator;
static struct ArenaAllocator* g_temporaryAllocator;
static int g_exitCode = 0;
static int g_isOutputTTY;

static struct ArenaAllocator* allocateArenaAllocator(char* name, size_t size)
{
    struct ArenaAllocator* allocator = allocateHeapMemory(sizeof(struct ArenaAllocator));
    size_t nameLength = strlen(name);
    allocator->name = allocateHeapMemory(nameLength + 1);
    memcpy(allocator->name, name, nameLength + 1);
    *(size_t*)(allocator->buffer = allocateHeapMemory(size)) = 0;
    allocator->cursor = allocator->buffer;
    allocator->size = size;
    return allocator;
}

static void* allocateArenaMemory(struct ArenaAllocator* allocator, size_t size)
{
    if (allocator->cursor + size > allocator->buffer + allocator->size)
    {
        throwError("can not allocate %zuB of memory on arena allocator \"%s\".", size, allocator->name);
        return NULL;
    }
    void* allocation = allocator->cursor;
    allocator->cursor += size;
    return allocation;
}

static void* allocateHeapMemory(size_t size)
{
    void* allocation = malloc(size);
    if (allocation)
    {
        return allocation;
    }
    throwError("can not allocate %zuB of memory on the heap.", size);
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

static void deallocateArenaMemory(struct ArenaAllocator* allocator, size_t size)
{
    allocator->cursor -= size;
}

static struct Credential* findCredentialByID(int isUser, uid_t id)
{
    struct ArenaAllocator* credentialsAllocator = isUser ? g_userCredentialsAllocator : g_groupCredentialsAllocator;
    size_t totalCredentials = *(size_t*)credentialsAllocator->buffer;
    struct Credential* credentials = (struct Credential*)(credentialsAllocator->buffer + sizeof(size_t));
    for (size_t index = 0; index < totalCredentials; ++index)
    {
        if (credentials[index].id == id)
        {
            return credentials + index;
        }
    }
    char* name = isUser ? getpwuid(id)->pw_name : getgrgid(id)->gr_name;
    allocateArenaMemory(credentialsAllocator, sizeof(struct Credential));
    credentials[totalCredentials].id = id;
    credentials[totalCredentials].nameLength = strlen(name);
    credentials[totalCredentials].name =
        allocateArenaMemory(g_credentialsDataAllocator, credentials[totalCredentials].nameLength + 1);
    memcpy(credentials[totalCredentials].name, name, credentials[totalCredentials].nameLength + 1);
    ++*(size_t*)credentialsAllocator->buffer;
    return credentials + totalCredentials;
}

static char* formatEntrySize(size_t* bufferLength, struct stat* status)
{
    if (S_ISDIR(status->st_mode))
    {
        *bufferLength = 1;
        char* buffer = allocateArenaMemory(g_entriesDataAllocator, 2);
        *buffer = '-';
        buffer[1] = 0;
        return buffer;
    }
    struct SIMultiplier multipliers[] = {{1099511627776, 'T'}, {1073741824, 'G'}, {1048576, 'M'}, {1024, 'k'}};
    char format[9];
    for (size_t index = 0; index < 4; ++index)
    {
        if (status->st_size >= multipliers[index].value)
        {
            float size = status->st_size / multipliers[index].value;
            sprintf(format, "%.1f%cB", size, multipliers[index].prefix);
            goto exit;
        }
    }
    sprintf(format, "%ldB", status->st_size);
exit:
    *bufferLength = strlen(format);
    char* buffer = allocateArenaMemory(g_entriesDataAllocator, *bufferLength + 1);
    memcpy(buffer, format, *bufferLength + 1);
    return buffer;
}

static void help(void)
{
    puts("Usage: dl [OPTION]... [PATH]...");
    puts("Lists the entries inside of directories given their PATH(s).");
    puts("If no path is provided, the current directory is considered.\n");
    puts("OPTIONS");
    puts("    --help    print these instructions.");
    puts("    --version print its version.");
}

static void readDirectory(char* directoryPath)
{
    DIR* stream = opendir(directoryPath);
    if (!stream)
    {
        struct stat status;
        writeError(stat(directoryPath, &status) ? "can not find the entry \"%s\"."
                   : S_ISDIR(status.st_mode)    ? "can not open the directory \"%s\"."
                                                : "the entry \"%s\" is not a directory.",
                   directoryPath);
        return;
    }
    size_t directoryPathLength = strlen(directoryPath);
    size_t totalEntries = 0;
    int indexColumnLength = 3;
    int userColumnLength = 4;
    int groupColumnLength = 5;
    int sizeColumnLength = 4;
    for (struct dirent* entryRegister; (entryRegister = readdir(stream));)
    {
        if (*entryRegister->d_name == '.' &&
            (!entryRegister->d_name[1] || (entryRegister->d_name[1] == '.' && !entryRegister->d_name[2])))
        {
            continue;
        }
        size_t nameLength = strlen(entryRegister->d_name);
        size_t entryPathLength = directoryPathLength + nameLength + 1;
        char* entryPath = allocateArenaMemory(g_temporaryAllocator, entryPathLength + 1);
        sprintf(entryPath, "%s/%s", directoryPath, entryRegister->d_name);
        struct stat status;
        lstat(entryPath, &status);
        struct Entry* entry = allocateArenaMemory(g_entriesAllocator, sizeof(struct Entry));
        if (S_ISLNK(status.st_mode))
        {
            char link[AVERAGE_PATH_LENGTH + 1];
            link[readlink(entryPath, link, sizeof(link))] = 0;
            size_t linkLength = strlen(link);
            entry->link = allocateArenaMemory(g_entriesDataAllocator, linkLength + 1);
            memcpy(entry->link, link, linkLength + 1);
        }
        else
        {
            entry->link = NULL;
        }
        deallocateArenaMemory(g_temporaryAllocator, entryPathLength + 1);
        entry->user = findCredentialByID(1, status.st_uid);
        entry->group = findCredentialByID(0, status.st_gid);
        entry->modifiedEpoch = status.st_mtim.tv_sec;
        entry->mode = status.st_mode;
        entry->name = allocateArenaMemory(g_entriesDataAllocator, nameLength + 1);
        memcpy(entry->name, entryRegister->d_name, nameLength + 1);
        size_t sizeLength;
        entry->size = formatEntrySize(&sizeLength, &status);
        ++totalEntries;
        SAVE_GREATER(userColumnLength, (int)entry->user->nameLength);
        SAVE_GREATER(groupColumnLength, (int)entry->group->nameLength);
        SAVE_GREATER(sizeColumnLength, (int)sizeLength);
    }
    closedir(stream);
    qsort(g_entriesAllocator->buffer, totalEntries, sizeof(struct Entry), sortEntriesAlphabetically);
    int totalDigitsInMaximumIndex = countDigits(totalEntries);
    SAVE_GREATER(indexColumnLength, totalDigitsInMaximumIndex);
    printf("%-*s %-*s %-*s %-*s %*s %-*s %s\n", indexColumnLength, "No.", userColumnLength, "User", groupColumnLength,
           "Group", 17, "Modified Date", sizeColumnLength, "Size", 14, "Mode", "Name");
    writeLine(7, indexColumnLength, userColumnLength, groupColumnLength, 17, sizeColumnLength, 14, 15);
    if (!totalEntries)
    {
        printf("%*s\n", 26 + indexColumnLength + userColumnLength + groupColumnLength + sizeColumnLength,
               "DIRECTORY IS EMPTY");
    }
    for (size_t index = 0; index < totalEntries; ++index)
    {
        struct Entry entry = *((struct Entry*)g_entriesAllocator->buffer + index);
        struct tm* modifiedTime = localtime(&entry.modifiedEpoch);
        char date[12];
        strftime(date, sizeof(date), "%b %d %Y", modifiedTime);
        printf("%*zu ", indexColumnLength, index + 1);
        setColor(Color_Red);
        printf("%-*s ", userColumnLength, entry.user->name);
        setColor(Color_Green);
        printf("%-*s ", groupColumnLength, entry.group->name);
        setColor(Color_Yellow);
        printf("%s ", date);
        setColor(Color_Magenta);
        printf("%02d:%02d ", modifiedTime->tm_hour, modifiedTime->tm_min);
        setColor(!entry.size[1] ? Color_Default : Color_Red);
        printf("%*s ", sizeColumnLength, entry.size);
        setColor(S_ISREG(entry.mode) ? Color_Default : Color_Blue);
        putchar(S_ISDIR(entry.mode)    ? 'd'
                : S_ISLNK(entry.mode)  ? 'l'
                : S_ISBLK(entry.mode)  ? 'b'
                : S_ISCHR(entry.mode)  ? 'c'
                : S_ISFIFO(entry.mode) ? 'f'
                : S_ISREG(entry.mode)  ? '-'
                                       : 's');
        PARSE_FILE_PERMISSION(S_IRUSR, 'r', Color_Red);
        PARSE_FILE_PERMISSION(S_IWUSR, 'w', Color_Green);
        PARSE_FILE_PERMISSION(S_IXUSR, 'x', Color_Yellow);
        PARSE_FILE_PERMISSION(S_IRGRP, 'r', Color_Red);
        PARSE_FILE_PERMISSION(S_IWGRP, 'w', Color_Green);
        PARSE_FILE_PERMISSION(S_IXGRP, 'x', Color_Yellow);
        PARSE_FILE_PERMISSION(S_IROTH, 'r', Color_Red);
        PARSE_FILE_PERMISSION(S_IWOTH, 'w', Color_Green);
        PARSE_FILE_PERMISSION(S_IXOTH, 'x', Color_Yellow);
        setColor(Color_Magenta);
        printf(" %03o ",
               entry.mode & (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH));
        setColor(S_ISDIR(entry.mode)    ? Color_Yellow
                 : S_ISLNK(entry.mode)  ? Color_Blue
                 : S_ISBLK(entry.mode)  ? Color_Magenta
                 : S_ISCHR(entry.mode)  ? Color_Green
                 : S_ISFIFO(entry.mode) ? Color_Blue
                 : S_ISREG(entry.mode)  ? Color_Default
                                        : Color_Cyan);
        printf(S_ISDIR(entry.mode)    ? " "
               : S_ISLNK(entry.mode)  ? "󰌷 "
               : S_ISBLK(entry.mode)  ? "󰇖 "
               : S_ISCHR(entry.mode)  ? "󱣴 "
               : S_ISFIFO(entry.mode) ? "󰟦 "
               : S_ISREG(entry.mode)  ? " "
                                      : "󱄙 ");
        setColor(Color_Default);
        printf("%s", entry.name);
        if (entry.link)
        {
            setColor(Color_Blue);
            printf(" -> ");
            setColor(Color_Default);
            printf("%s", entry.link);
        }
        putchar('\n');
    }
    resetArenaAllocator(g_entriesAllocator);
    resetArenaAllocator(g_entriesDataAllocator);
    writeLine(1, 52 + indexColumnLength + userColumnLength + groupColumnLength + sizeColumnLength);
    setColor(Color_Red);
    printf(":: ");
    setColor(Color_Default);
    printf("Path: ");
    setColor(Color_Green);
    char* fullPath = realpath(directoryPath, allocateArenaMemory(g_temporaryAllocator, AVERAGE_PATH_LENGTH + 1));
    printf("%s", fullPath);
    resetArenaAllocator(g_temporaryAllocator);
    setColor(Color_Default);
    printf(".\n");
    setColor(Color_Red);
    printf(":: ");
    setColor(Color_Default);
    printf("Total: ");
    setColor(Color_Yellow);
    printf("%zu", totalEntries);
    setColor(Color_Default);
    printf(" %s.\n", totalEntries == 1 ? "entry" : "entries");
}

static void resetArenaAllocator(struct ArenaAllocator* allocator)
{
    allocator->cursor = allocator->buffer;
}

static void setColor(int color)
{
    if (g_isOutputTTY)
    {
        printf("\033[3%dm", color);
    }
}

static int sortEntriesAlphabetically(const void* entry0, const void* entry1)
{
    return strcmp(((struct Entry*)entry0)->name, ((struct Entry*)entry1)->name);
}

static void throwError(char* format, ...)
{
    fprintf(stderr, "dl: ");
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    exit(1);
}

static void writeError(char* format, ...)
{
    fprintf(stderr, "dl: ");
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    g_exitCode = 1;
}

static void writeLine(size_t totalLines, ...)
{
    va_list arguments;
    va_start(arguments, totalLines);
    for (size_t index = 0; index < totalLines; ++index)
    {
        int length = va_arg(arguments, int);
        for (int column = 0; column < length; ++column)
        {
            setColor(column % 2 ? Color_Red : Color_Yellow);
            printf(column % 2 ? "v" : "≥");
        }
        if (index < totalLines - 1)
        {
            putchar(' ');
        }
    }
    va_end(arguments);
    setColor(Color_Default);
    putchar('\n');
}

int main(int totalArguments, char** arguments)
{
    g_userCredentialsAllocator = allocateArenaAllocator(
        "g_userCredentialsAllocator", sizeof(struct Credential) * MAXIMUM_CREDENTIALS_EXPECTED + sizeof(size_t));
    g_groupCredentialsAllocator =
        allocateArenaAllocator("g_groupCredentialAllocator", g_userCredentialsAllocator->size);
    g_credentialsDataAllocator = allocateArenaAllocator(
        "g_credentialsDataAllocator", (AVERAGE_CREDENTIAL_NAME_LENGTH + 1) * MAXIMUM_CREDENTIALS_EXPECTED);
    g_entriesAllocator = allocateArenaAllocator("g_entriesAllocator", sizeof(struct Entry) * MAXIMUM_ENTRIES_EXPECTED);
    g_entriesDataAllocator = allocateArenaAllocator("g_entriesDataAllocator",
                                                    (AVERAGE_ENTRY_NAME_LENGTH * 2 + 9) * MAXIMUM_ENTRIES_EXPECTED);
    g_temporaryAllocator = allocateArenaAllocator("g_temporaryAllocator", AVERAGE_PATH_LENGTH + 1);
    g_isOutputTTY = isatty(STDOUT_FILENO);
    if (totalArguments == 1)
    {
        readDirectory(".");
        goto exit;
    }
    for (int index = 1; index < totalArguments; ++index)
    {
        PARSE_OPTION("version", puts(VERSION));
        PARSE_OPTION("help", help());
    }
    for (int index = 1; index < totalArguments; ++index)
    {
        readDirectory(arguments[index]);
    }
exit:
    deallocateArenaAllocator(g_userCredentialsAllocator);
    deallocateArenaAllocator(g_groupCredentialsAllocator);
    deallocateArenaAllocator(g_credentialsDataAllocator);
    deallocateArenaAllocator(g_entriesAllocator);
    deallocateArenaAllocator(g_entriesDataAllocator);
    deallocateArenaAllocator(g_temporaryAllocator);
    return g_exitCode;
}
