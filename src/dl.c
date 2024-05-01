#include "dl.h"

static struct ArenaAllocator *userCredentialsAllocator_g = NULL;
static struct ArenaAllocator *groupCredentialsAllocator_g = NULL;
static struct ArenaAllocator *userCredentialsDataAllocator_g = NULL;
static struct ArenaAllocator *groupCredentialsDataAllocator_g = NULL;
static struct ArenaAllocator *entriesAllocator_g = NULL;
static struct ArenaAllocator *entriesDataAllocator_g = NULL;
static struct ArenaAllocator *temporaryDataAllocator_g = NULL;
static char *buffer_g = NULL;
int isOutTTY_g = 0;

static void allocateArenaAllocator(struct ArenaAllocator **allocator, const char *name, size_t unit, size_t capacity)
{
    if (*allocator)
    {
        return;
    }
    *allocator = allocateHeapMemory(sizeof(struct ArenaAllocator));
    (*allocator)->buffer = allocateHeapMemory(unit * capacity);
    (*allocator)->unit = unit;
    (*allocator)->capacity = capacity;
    (*allocator)->use = 0;
    size_t nameSize = strlen(name) + 1;
    (*allocator)->name = allocateHeapMemory(nameSize);
    memcpy((*allocator)->name, name, nameSize);
}

static void *allocateArenaMemory(struct ArenaAllocator *allocator, size_t use)
{
    if (allocator->use + use <= allocator->capacity)
    {
        void *allocation = allocator->buffer + allocator->use * allocator->unit;
        allocator->use += use;
        return allocation;
    }
    writeLog(LogType_Error, "can not allocate %zuB of memory on arena \"%s\".", allocator->name);
    return NULL;
}

static void *allocateHeapMemory(size_t size)
{
    void *allocation = malloc(size);
    if (allocation)
    {
        return allocation;
    }
    writeLog(LogType_Error, "can not allocate %zuB of memory on the heap.", size);
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

static struct Credential *findCredential(uid_t id, int isUser)
{
    struct ArenaAllocator *credentialsAllocator = isUser ? userCredentialsAllocator_g : groupCredentialsAllocator_g;
    struct ArenaAllocator *credentialsDataAllocator =
        isUser ? userCredentialsDataAllocator_g : groupCredentialsDataAllocator_g;
    for (size_t offset = 0; offset < credentialsAllocator->use; ++offset)
    {
        struct Credential *credential;
        if ((credential = (struct Credential *)credentialsAllocator->buffer + offset)->id == id)
        {
            return credential;
        }
    }
    const char *name;
    if (isUser)
    {
        struct passwd *user;
        if (!(user = getpwuid(id)))
        {
            return NULL;
        }
        name = user->pw_name;
    }
    else
    {
        struct group *group;
        if (!(group = getgrgid(id)))
        {
            return NULL;
        }
        name = group->gr_name;
    }
    struct Credential *credential = allocateArenaMemory(credentialsAllocator, 1);
    credential->id = id;
    credential->nameLength = strlen(name);
    credential->name = allocateArenaMemory(credentialsDataAllocator, credential->nameLength + 1);
    memcpy(credential->name, name, credential->nameLength + 1);
    return credential;
}

static char *formatSize(struct stat *status, size_t *length)
{
    float multiplierValues[] = {1099511627776, 1073741824, 1048576, 1024};
    char multiplierPrefixes[] = {'T', 'G', 'M', 'k'};
    char prefix = 0;
    char numberBuffer[7];
    int separatorOffset = 0;
    if (S_ISDIR(status->st_mode))
    {
        return NULL;
    }
    for (int offset = 0; offset < 4; ++offset)
    {
        if (status->st_size >= multiplierValues[offset])
        {
            float size = status->st_size / multiplierValues[offset];
            float temporary;
            prefix = multiplierPrefixes[offset];
            sprintf(numberBuffer,
                    ((temporary = size - (int)size) >= 0 && temporary < 0.1) || temporary >= 0.95 ? "%.0f" : "%.1f",
                    size);
            separatorOffset = -2;
            goto format_l;
        }
    }
    sprintf(numberBuffer, "%ld", status->st_size);
format_l:
    separatorOffset += strlen(numberBuffer) - 3;
    char formatBuffer[10];
    int formatOffset = 0;
    for (int numberOffset = 0; numberBuffer[numberOffset]; (void)(++numberOffset && ++formatOffset))
    {
        if (numberOffset == separatorOffset && separatorOffset)
        {
            formatBuffer[formatOffset++] = ',';
        }
        formatBuffer[formatOffset] = numberBuffer[numberOffset];
    }
    if (prefix)
    {
        formatBuffer[formatOffset++] = prefix;
    }
    *(short *)(formatBuffer + formatOffset) = *(short *)"B";
    char *buffer = allocateArenaMemory(entriesDataAllocator_g, (*length = formatOffset + 1) + 1);
    memcpy(buffer, formatBuffer, *length + 1);
    return buffer;
}

static void freeArenaAllocator(struct ArenaAllocator *allocator)
{
    if (allocator)
    {
        free(allocator->name);
        free(allocator->buffer);
        free(allocator);
    }
}

static void readDirectory(const char *directoryPath)
{
    struct stat status;
    int directoryStream;
    if (stat(directoryPath, &status))
    {
        writeLog(LogType_Warning, "can not find the entry \"%s\".", directoryPath);
        return;
    }
    else if (!S_ISDIR(status.st_mode))
    {
        writeLog(LogType_Warning, "the entry \"%s\" is not a directory.", directoryPath);
        return;
    }
    else if ((directoryStream = open(directoryPath, O_RDONLY)) < 0)
    {
        writeLog(LogType_Warning, "can not open the directory \"%s\".", directoryPath);
        return;
    }
    if (!buffer_g)
    {
        buffer_g = allocateHeapMemory(BUFFER_SIZE);
    }
    allocateArenaAllocator(&userCredentialsAllocator_g, "userCredentialsAllocator_g", sizeof(struct Credential), 20);
    allocateArenaAllocator(&groupCredentialsAllocator_g, "groupCredentialsAllocator_g", sizeof(struct Credential), 20);
    allocateArenaAllocator(&userCredentialsDataAllocator_g, "userCredentialsDataAllocator_g", sizeof(char), 320);
    allocateArenaAllocator(&groupCredentialsDataAllocator_g, "groupCredentialsDataAllocator_g", sizeof(char), 320);
    allocateArenaAllocator(&entriesAllocator_g, "entriesAllocator_g", sizeof(struct Entry), 30000);
    allocateArenaAllocator(&entriesDataAllocator_g, "entriesDataAllocator_g", sizeof(char), 2097152);
    allocateArenaAllocator(&temporaryDataAllocator_g, "temporaryDataAllocator_g", sizeof(char), 600);
    size_t directoryPathSize = strlen(directoryPath) + 1;
    int noColumnLength = 3;
    int userColumnLength = 4;
    int groupColumnLength = 5;
    int sizeColumnLength = 4;
    int nameColumnLength = 4;
    for (long totalEntries; (totalEntries = syscall(SYS_getdents64, directoryStream, buffer_g, BUFFER_SIZE)) > 0;)
    {
        struct linux_dirent64 *linuxEntry;
        for (long offset = 0; offset < totalEntries; offset += linuxEntry->d_reclen)
        {
            linuxEntry = (struct linux_dirent64 *)(buffer_g + offset);
            if (linuxEntry->d_name[0] == '.' &&
                (!linuxEntry->d_name[1] || (linuxEntry->d_name[1] == '.' && !linuxEntry->d_name[2])))
            {
                continue;
            }
            size_t entryNameLength = strlen(linuxEntry->d_name);
            size_t entryPathSize = directoryPathSize + entryNameLength + 1;
            char *entryPath = allocateArenaMemory(temporaryDataAllocator_g, entryPathSize);
            sprintf(entryPath, "%s/%s", directoryPath, linuxEntry->d_name);
            struct Entry *entry = allocateArenaMemory(entriesAllocator_g, 1);
            int temporaryNameColumnLength = entryNameLength + 2;
            lstat(entryPath, &status);
            entry->name = allocateArenaMemory(entriesDataAllocator_g, entryNameLength + 1);
            memcpy(entry->name, linuxEntry->d_name, entryNameLength + 1);
            entry->user = findCredential(status.st_uid, 1);
            entry->group = findCredential(status.st_gid, 0);
            entry->modifiedTime = status.st_mtim.tv_sec;
            entry->mode = status.st_mode;
            size_t entrySizeLength = 0;
            entry->size = formatSize(&status, &entrySizeLength);
            if (linuxEntry->d_type == DT_LNK)
            {
                char *link = allocateArenaMemory(temporaryDataAllocator_g, 300);
                link[readlink(entryPath, link, 300)] = 0;
                size_t linkLength = strlen(link);
                temporaryNameColumnLength += linkLength + 4;
                entry->link = allocateArenaMemory(entriesDataAllocator_g, linkLength + 1);
                memcpy(entry->link, link, linkLength + 1);
            }
            else
            {
                entry->link = NULL;
            }
            temporaryDataAllocator_g->use = 0;
            if (entry->user)
            {
                SAVE_GREATER(userColumnLength, entry->user->nameLength);
            }
            if (entry->group)
            {
                SAVE_GREATER(groupColumnLength, entry->group->nameLength);
            }
            if (entry->size)
            {
                SAVE_GREATER(sizeColumnLength, entrySizeLength);
            }
            SAVE_GREATER(nameColumnLength, temporaryNameColumnLength);
        }
    }
    close(directoryStream);
    qsort(entriesAllocator_g->buffer, entriesAllocator_g->use, sizeof(struct Entry), sortEntriesAlphabetically);
    int totalDigitsInMaximumNo = countDigits(entriesAllocator_g->use) + (entriesAllocator_g->use > 1000);
    SAVE_GREATER(noColumnLength, totalDigitsInMaximumNo);
    char *fullDirectoryPath = allocateArenaMemory(temporaryDataAllocator_g, 300);
    realpath(directoryPath, fullDirectoryPath);
    tdk_set256Color(tdk_Color_Yellow, tdk_Layer_Foreground);
    printf("󰝰 ");
    tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
    tdk_setWeight(tdk_Weight_Bold);
    printf("%s:\n", fullDirectoryPath);
    tdk_setWeight(tdk_Weight_Default);
    temporaryDataAllocator_g->use = 0;
    printf("%*s %-*s %-*s %-*s %*s %-*s Name\n", noColumnLength, "No.", groupColumnLength, "Group", userColumnLength,
           "User", 17, "Modified Date", sizeColumnLength, "Size", 13, "Permissions");
    writeLines(7, noColumnLength, groupColumnLength, userColumnLength, 17, sizeColumnLength, 13, nameColumnLength);
    if (!entriesAllocator_g->use)
    {
        tdk_set256Color(tdk_Color_LightBlack, tdk_Layer_Foreground);
        printf("%*s\n",
               17 + noColumnLength + groupColumnLength + userColumnLength + sizeColumnLength + nameColumnLength,
               "DIRECTORY IS EMPTY");
        tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
    }
    for (size_t offset = 0; offset < entriesAllocator_g->use; ++offset)
    {
        struct Entry *entry = (struct Entry *)entriesAllocator_g->buffer + offset;
        writeEntryNo(offset + 1, noColumnLength);
        putchar(' ');
        if (entry->group)
        {
            tdk_set256Color(tdk_Color_Red, tdk_Layer_Foreground);
            printf("%-*s ", groupColumnLength, entry->group->name);
        }
        else
        {
            printf("%-*c ", groupColumnLength, '-');
        }
        if (entry->user)
        {
            tdk_set256Color(tdk_Color_Green, tdk_Layer_Foreground);
            printf("%-*s ", userColumnLength, entry->user->name);
        }
        else
        {
            tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
            printf("%-*c ", userColumnLength, '-');
        }
        char modifiedDate[12];
        struct tm *localTime = localtime(&entry->modifiedTime);
        strftime(modifiedDate, sizeof(modifiedDate), "%b/%d/%Y", localTime);
        tdk_set256Color(tdk_Color_Yellow, tdk_Layer_Foreground);
        printf("%s ", modifiedDate);
        tdk_set256Color(tdk_Color_Magenta, tdk_Layer_Foreground);
        printf("%02d:%02d ", localTime->tm_hour, localTime->tm_min);
        if (entry->size)
        {
            tdk_set256Color(tdk_Color_Red, tdk_Layer_Foreground);
            printf("%*s ", sizeColumnLength, entry->size);
        }
        else
        {
            tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
            printf("%*c ", sizeColumnLength, '-');
        }
        PARSE_PERMISSION(S_IRUSR, 'r', tdk_Color_Red);
        PARSE_PERMISSION(S_IWUSR, 'w', tdk_Color_Green);
        PARSE_PERMISSION(S_IXUSR, 'x', tdk_Color_Yellow);
        PARSE_PERMISSION(S_IRGRP, 'r', tdk_Color_Red);
        PARSE_PERMISSION(S_IWGRP, 'w', tdk_Color_Green);
        PARSE_PERMISSION(S_IXGRP, 'x', tdk_Color_Yellow);
        PARSE_PERMISSION(S_IROTH, 'r', tdk_Color_Red);
        PARSE_PERMISSION(S_IWOTH, 'w', tdk_Color_Green);
        PARSE_PERMISSION(S_IXOTH, 'x', tdk_Color_Yellow);
        tdk_set256Color(tdk_Color_Magenta, tdk_Layer_Foreground);
        printf(" %03o ",
               entry->mode & (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH));
        switch (entry->mode & S_IFMT)
        {
        case S_IFDIR:
            tdk_set256Color(tdk_Color_Yellow, tdk_Layer_Foreground);
            printf(isOutTTY_g ? "󰝰 " : "d");
            break;
        case S_IFLNK:
            tdk_set256Color(tdk_Color_Blue, tdk_Layer_Foreground);
            printf(isOutTTY_g ? "󰌷 " : "l");
            break;
        case S_IFBLK:
            tdk_set256Color(tdk_Color_Magenta, tdk_Layer_Foreground);
            printf(isOutTTY_g ? "󰇖 " : "b");
            break;
        case S_IFCHR:
            tdk_set256Color(tdk_Color_Green, tdk_Layer_Foreground);
            printf(isOutTTY_g ? "󱣴 " : "c");
            break;
        case S_IFIFO:
            tdk_set256Color(tdk_Color_Blue, tdk_Layer_Foreground);
            printf(isOutTTY_g ? "󰟦 " : "f");
            break;
        case S_IFSOCK:
            tdk_set256Color(tdk_Color_Cyan, tdk_Layer_Foreground);
            printf(isOutTTY_g ? "󱄙 " : "s");
            break;
        case S_IFREG:
            tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
            printf(isOutTTY_g ? " " : "-");
            break;
        }
        tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
        printf("%s", entry->name);
        if (entry->link)
        {
            tdk_set256Color(tdk_Color_Blue, tdk_Layer_Foreground);
            printf(" -> ");
            tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
            printf("%s", entry->link);
        }
        putchar('\n');
    }
    entriesAllocator_g->use = 0;
    entriesDataAllocator_g->use = 0;
}

static int sortEntriesAlphabetically(const void *entry0, const void *entry1)
{
    return strcmp(((struct Entry *)entry0)->name, ((struct Entry *)entry1)->name);
}

static void writeEntryNo(int no, int align)
{
    char buffer[7];
    sprintf(buffer, "%d", no);
    int length = strlen(buffer);
    int leftPadding = align - length - (length >= 4);
    for (int offset = 0; offset < leftPadding; ++offset)
    {
        putchar(' ');
    }
    for (int separatorOffset = length - 3, offset = 0; offset < length; ++offset)
    {
        if (offset == separatorOffset && separatorOffset)
        {
            putchar(',');
        }
        putchar(buffer[offset]);
    }
}

static void writeHelp(void)
{
    tdk_setWeight(tdk_Weight_Bold);
    printf("Usage: ");
    tdk_setWeight(tdk_Weight_Default);
    printf("dl ");
    tdk_set256Color(tdk_Color_LightBlack, tdk_Layer_Foreground);
    printf("[OPTION | PATH]...\n");
    tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
    printf("Lists the contents of directories given its PATH(s).\n");
    printf("If no path is provided, the current directory is considered.\n\n");
    tdk_setWeight(tdk_Weight_Bold);
    printf("AVAILABLE OPTIONS\n");
    tdk_setWeight(tdk_Weight_Default);
    tdk_set256Color(tdk_Color_LightBlack, tdk_Layer_Foreground);
    printf("    --help     ");
    tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
    printf("show these help instructions.\n");
    tdk_set256Color(tdk_Color_LightBlack, tdk_Layer_Foreground);
    printf("    --version  ");
    tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
    printf("show its version and platform.\n\n");
    tdk_setWeight(tdk_Weight_Bold);
    printf("SOURCE CODE\n");
    tdk_setWeight(tdk_Weight_Default);
    printf("Its source code is available at: <");
    tdk_setEffect(tdk_Effect_Underline, 1);
    printf("https://github.com/skippyr/dl");
    tdk_setEffect(tdk_Effect_Underline, 0);
    printf(">.\n");
}

static void writeLines(int totalLines, ...)
{
    va_list arguments;
    va_start(arguments, totalLines);
    int lastOffsett = totalLines - 1;
    for (int offset = 0; offset < totalLines; ++offset)
    {
        int length;
        for (length = va_arg(arguments, int); length; --length)
        {
            putchar('-');
        }
        if (offset < lastOffsett)
        {
            putchar(' ');
        }
    }
    va_end(arguments);
    putchar('\n');
}

static void writeLog(int type, const char *format, ...)
{
    tdk_set256Color(type == LogType_Warning ? tdk_Color_Yellow : tdk_Color_Red, tdk_Layer_Foreground);
    fflush(stdout);
    fprintf(stderr, type == LogType_Warning ? "[WARNING] " : "[ERROR] ");
    if (type == LogType_Error)
    {
        tdk_set256Color(tdk_Color_LightBlack, tdk_Layer_Foreground);
        fflush(stdout);
        printf("(exit code 1) ");
    }
    tdk_set256Color(tdk_Color_Default, tdk_Layer_Foreground);
    tdk_setWeight(tdk_Weight_Bold);
    fflush(stdout);
    fprintf(stderr, "dl: ");
    tdk_setWeight(tdk_Weight_Default);
    fflush(stdout);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fprintf(stderr, "\n");
    if (type == LogType_Error)
    {
        exit(1);
    }
}

int main(int totalArguments, const char **arguments)
{
    isOutTTY_g = isatty(STDOUT_FILENO);
    if (totalArguments == 1)
    {
        readDirectory(".");
        goto end_l;
    }
    for (int offset = 1; offset < totalArguments; ++offset)
    {
        PARSE_OPTION("help", writeHelp());
        PARSE_OPTION("version", printf("dl %s (compiled for Linux %s)\n", VERSION, ARCHICTECTURE));
        if (arguments[offset][0] == '-' && (arguments[offset][1] == '-' || !arguments[offset][2]))
        {
            writeLog(LogType_Error, "the option \"%s\" is unrecognized.", arguments[offset]);
        }
    }
    for (int offset = 1; offset < totalArguments; ++offset)
    {
        readDirectory(arguments[offset]);
    }
end_l:
    if (buffer_g)
    {
        free(buffer_g);
    }
    freeArenaAllocator(userCredentialsAllocator_g);
    freeArenaAllocator(groupCredentialsAllocator_g);
    freeArenaAllocator(userCredentialsDataAllocator_g);
    freeArenaAllocator(groupCredentialsDataAllocator_g);
    freeArenaAllocator(entriesAllocator_g);
    freeArenaAllocator(entriesDataAllocator_g);
    freeArenaAllocator(temporaryDataAllocator_g);
    return 0;
}
