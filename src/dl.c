#include "dl.h"

#if defined(_WIN32)
static char *securityDescriptorBuffer_g = NULL;
static struct ArenaAllocator *temporaryWideDataAllocator_g = NULL;
static struct ArenaAllocator *credentialsAllocator_g = NULL;
static struct ArenaAllocator *credentialsDataAllocator_g = NULL;
#else
static struct ArenaAllocator *userCredentialsAllocator_g = NULL;
static struct ArenaAllocator *userCredentialsDataAllocator_g = NULL;
static struct ArenaAllocator *groupCredentialsAllocator_g = NULL;
static struct ArenaAllocator *groupCredentialsDataAllocator_g = NULL;
#endif
static struct ArenaAllocator *entriesAllocator_g = NULL;
static struct ArenaAllocator *entriesDataAllocator_g = NULL;
static struct ArenaAllocator *temporaryDataAllocator_g = NULL;
static int exitCode_g = 0;

#if defined(DEBUG)
static void debugArenaAllocator(struct ArenaAllocator *allocator) {
  if (!allocator) {
    return;
  }
  tmk_writeLine("::     Name: %s.", allocator->name);
  tmk_writeLine("::   Buffer: %p.", allocator->buffer);
  tmk_writeLine("::      Use: %zu.", allocator->use);
  tmk_writeLine(":: Capacity: %zu.", allocator->capacity);
  tmk_writeLine("::     Unit: %zu.", allocator->unit);
}
#endif

#if defined(_WIN32)
static char *convertArenaUTF16ToUTF8(struct ArenaAllocator *allocator,
                                     const wchar_t *utf16String,
                                     size_t *utf8StringLength) {
  size_t utf8StringSize =
      WideCharToMultiByte(CP_UTF8, 0, utf16String, -1, NULL, 0, NULL, NULL);
  char *utf8String = allocateArenaMemory(allocator, utf8StringSize);
  WideCharToMultiByte(CP_UTF8, 0, utf16String, -1, utf8String, utf8StringSize,
                      NULL, NULL);
  if (utf8StringLength) {
    *utf8StringLength = utf8StringSize - 1;
  }
  return utf8String;
}

static struct Credential *findCredential(const wchar_t *utf16DirectoryPath,
                                         size_t globSize,
                                         PWIN32_FIND_DATAW entryData) {
  size_t entryPathSize = wcslen(entryData->cFileName) + globSize - 1;
  wchar_t *entryPath =
      allocateArenaMemory(temporaryWideDataAllocator_g, entryPathSize);
  memcpy(entryPath, utf16DirectoryPath, (globSize - 3) * sizeof(wchar_t));
  entryPath[globSize - 3] = '\\';
  memcpy(entryPath + globSize - 2, entryData->cFileName,
         (entryPathSize - globSize + 2) * sizeof(wchar_t));
  DWORD securityDescriptorSize;
  if (!GetFileSecurityW(entryPath, OWNER_SECURITY_INFORMATION,
                        securityDescriptorBuffer_g, 80,
                        &securityDescriptorSize)) {
    freeArenaMemory(temporaryWideDataAllocator_g, entryPathSize);
    return NULL;
  }
  freeArenaMemory(temporaryWideDataAllocator_g, entryPathSize);
  PSID sid;
  BOOL isOwnerDefaulted;
  GetSecurityDescriptorOwner(securityDescriptorBuffer_g, &sid,
                             &isOwnerDefaulted);
  for (size_t index = 0; index < credentialsAllocator_g->use; ++index) {
    if (EqualSid(
            ((struct Credential *)credentialsAllocator_g->buffer + index)->sid,
            sid)) {
      return (struct Credential *)credentialsAllocator_g->buffer + index;
    }
  }
  DWORD utf16UserSize = 0;
  DWORD utf16DomainSize = 0;
  SID_NAME_USE use;
  LookupAccountSidW(NULL, sid, NULL, &utf16UserSize, NULL, &utf16DomainSize,
                    &use);
  wchar_t *utf16User =
      allocateArenaMemory(temporaryWideDataAllocator_g, utf16UserSize);
  wchar_t *utf16Domain =
      allocateArenaMemory(temporaryWideDataAllocator_g, utf16DomainSize);
  LookupAccountSidW(NULL, sid, utf16User, &utf16UserSize, utf16Domain,
                    &utf16DomainSize, &use);
  struct Credential *credential =
      allocateArenaMemory(credentialsAllocator_g, 1);
  DWORD sidLength = GetLengthSid(sid);
  credential->sid = allocateArenaMemory(credentialsDataAllocator_g, sidLength);
  CopySid(sidLength, credential->sid, sid);
  credential->user.buffer = convertArenaUTF16ToUTF8(
      credentialsDataAllocator_g, utf16User, &credential->user.length);
  credential->domain.buffer = convertArenaUTF16ToUTF8(
      credentialsDataAllocator_g, utf16Domain, &credential->domain.length);
  freeArenaMemory(temporaryWideDataAllocator_g,
                  utf16UserSize + utf16DomainSize + 2);
  return credential;
}

static void readDirectory(const char *utf8DirectoryPath,
                          const wchar_t *utf16DirectoryPath) {
  size_t globSize = wcslen(utf16DirectoryPath) + 3;
  wchar_t *glob = allocateHeapMemory(globSize * sizeof(wchar_t));
  memcpy(glob, utf16DirectoryPath, (globSize - 3) * sizeof(wchar_t));
  glob[globSize - 3] = L'\\';
  glob[globSize - 2] = L'*';
  glob[globSize - 1] = 0;
  WIN32_FIND_DATAW entryData;
  HANDLE directoryStream = FindFirstFileW(glob, &entryData);
  free(glob);
  if (directoryStream == INVALID_HANDLE_VALUE) {
    DWORD directoryAttributes = GetFileAttributesW(utf16DirectoryPath);
    writeError(directoryAttributes == INVALID_FILE_ATTRIBUTES
                   ? "can not find the entry \"%s\"."
               : directoryAttributes & FILE_ATTRIBUTE_DIRECTORY
                   ? "can not open the directory \"%s\"."
                   : "the entry \"%s\" is not a directory.",
               utf8DirectoryPath);
    return;
  }
  securityDescriptorBuffer_g = allocateHeapMemory(80);
  createArenaAllocator("entriesAllocator_g", sizeof(struct Entry), 30000,
                       &entriesAllocator_g);
  createArenaAllocator("entriesDataAllocator_g", sizeof(char), 2097152,
                       &entriesDataAllocator_g);
  createArenaAllocator("temporaryDataAllocator_g", sizeof(char), 500,
                       &temporaryDataAllocator_g);
  createArenaAllocator("temporaryWideDataAllocator_g", sizeof(wchar_t), 500,
                       &temporaryWideDataAllocator_g);
  createArenaAllocator("credentialsAllocator_g", sizeof(struct Credential), 20,
                       &credentialsAllocator_g);
  createArenaAllocator("credentialsDataAllocator_g", sizeof(char), 640,
                       &credentialsDataAllocator_g);
  int indexColumnLength = 3;
  int userColumnLength = 4;
  int domainColumnLength = 6;
  int sizeColumnLength = 4;
  do {
    if (entryData.cFileName[0] == '.' &&
        (!entryData.cFileName[1] ||
         (entryData.cFileName[1] == '.' && !entryData.cFileName[2]))) {
      continue;
    }
    struct Entry *entry = allocateArenaMemory(entriesAllocator_g, 1);
    entry->credential =
        findCredential(utf16DirectoryPath, globSize, &entryData);
    entry->mode = entryData.dwFileAttributes;
    entry->modifiedTime = entryData.ftLastWriteTime;
    entry->name = convertArenaUTF16ToUTF8(entriesDataAllocator_g,
                                          entryData.cFileName, NULL);
    size_t sizeLength;
    entry->size = formatSize(
        &sizeLength,
        ((ULARGE_INTEGER){entryData.nFileSizeLow, entryData.nFileSizeHigh})
            .QuadPart,
        entryData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    if (entry->credential) {
      SAVE_GREATER(userColumnLength, entry->credential->user.length);
      SAVE_GREATER(domainColumnLength, entry->credential->domain.length);
    }
    SAVE_GREATER(sizeColumnLength, sizeLength);
  } while (FindNextFileW(directoryStream, &entryData));
  FindClose(directoryStream);
  int totalDigitsForIndex = countDigits(entriesAllocator_g->use);
  SAVE_GREATER(indexColumnLength, totalDigitsForIndex);
  qsort(entriesAllocator_g->buffer, entriesAllocator_g->use,
        sizeof(struct Entry), sortEntriesAlphabetically);
  tmk_setFontANSIColor(tmk_ANSIColor_DarkYellow, tmk_FontLayer_Foreground);
  if (!tmk_isStreamRedirected(tmk_Stream_Output)) {
    tmk_write(" ");
  }
  tmk_resetFontColors();
  tmk_setFontWeight(tmk_FontWeight_Bold);
  if (((utf16DirectoryPath[0] > 'A' && utf16DirectoryPath[0] < 'Z') ||
       (utf16DirectoryPath[0] > 'a' && utf16DirectoryPath[0] < 'z')) &&
      utf16DirectoryPath[1] == ':' && !utf16DirectoryPath[2]) {
    tmk_writeLine("%c:\\:", utf16DirectoryPath[0]);
  } else {
    DWORD utf16DirectoryFullPathSize =
        GetFullPathNameW(utf16DirectoryPath, 0, NULL, NULL);
    wchar_t *utf16DirectoryFullPath = allocateArenaMemory(
        temporaryWideDataAllocator_g, utf16DirectoryFullPathSize);
    GetFullPathNameW(utf16DirectoryPath, utf16DirectoryFullPathSize,
                     utf16DirectoryFullPath, NULL);
    size_t utf8DirectoryFullPathLength;
    char *utf8DirectoryFullPath = convertArenaUTF16ToUTF8(
        temporaryDataAllocator_g, utf16DirectoryFullPath,
        &utf8DirectoryFullPathLength);
    if (utf8DirectoryFullPathLength > 3 &&
        utf8DirectoryFullPath[utf8DirectoryFullPathLength - 1] == '\\') {
      utf8DirectoryFullPath[utf8DirectoryFullPathLength - 1] = 0;
    }
    tmk_writeLine("%s%s:", utf8DirectoryFullPath,
                  utf8DirectoryFullPathLength < 3 ? "\\" : "");
    freeArenaMemory(temporaryDataAllocator_g, utf8DirectoryFullPathLength + 1);
    freeArenaMemory(temporaryWideDataAllocator_g, utf16DirectoryFullPathSize);
  }
  tmk_writeLine("%*s %-*s %-*s %-*s %*s %-*s Name", indexColumnLength, "No.",
                domainColumnLength, "Domain", userColumnLength, "User", 17,
                "Modified Date", sizeColumnLength, "Size", 5, "Mode");
  tmk_resetFontWeight();
  writeLines(7, indexColumnLength, domainColumnLength, userColumnLength, 17,
             sizeColumnLength, 5, 20);
  if (!entriesAllocator_g->use) {
    tmk_setFontANSIColor(tmk_ANSIColor_LightBlack, tmk_FontLayer_Foreground);
    tmk_writeLine("%*s",
                  23 + indexColumnLength + domainColumnLength +
                      userColumnLength + sizeColumnLength,
                  "DIRECTORY IS EMPTY");
    tmk_resetFontColors();
  }
  for (size_t index = 0; index < entriesAllocator_g->use; ++index) {
    struct Entry entry = *((struct Entry *)entriesAllocator_g->buffer + index);
    tmk_write("%*zu ", indexColumnLength, index + 1);
    if (entry.credential) {
      tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
      tmk_write("%-*s ", domainColumnLength, entry.credential->domain.buffer);
      tmk_setFontANSIColor(tmk_ANSIColor_DarkGreen, tmk_FontLayer_Foreground);
      tmk_write("%-*s ", userColumnLength, entry.credential->user.buffer);
    } else {
      tmk_write("%-*c %-*c ", domainColumnLength, '-', userColumnLength, '-');
    }
    tmk_setFontANSIColor(tmk_ANSIColor_DarkYellow, tmk_FontLayer_Foreground);
    SYSTEMTIME localModifiedTime;
    FileTimeToSystemTime(&entry.modifiedTime, &localModifiedTime);
    size_t modifiedDateSize;
    char *modifiedDate =
        formatModifiedDate(localModifiedTime.wMonth - 1, localModifiedTime.wDay,
                           localModifiedTime.wYear, &modifiedDateSize);
    tmk_write("%s ", modifiedDate);
    freeArenaMemory(temporaryDataAllocator_g, modifiedDateSize);
    tmk_setFontANSIColor(tmk_ANSIColor_DarkMagenta, tmk_FontLayer_Foreground);
    tmk_write("%02d:%02d ", localModifiedTime.wHour, localModifiedTime.wMinute);
    if (entry.size) {
      tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
      tmk_write("%*s ", sizeColumnLength, entry.size);
    } else {
      tmk_resetFontColors();
      tmk_write("%*c ", sizeColumnLength, '-');
    }
    PARSE_MODE(FILE_ATTRIBUTE_HIDDEN, 'h', tmk_ANSIColor_DarkRed);
    PARSE_MODE(FILE_ATTRIBUTE_ARCHIVE, 'a', tmk_ANSIColor_DarkGreen);
    PARSE_MODE(FILE_ATTRIBUTE_READONLY, 'r', tmk_ANSIColor_DarkYellow);
    PARSE_MODE(FILE_ATTRIBUTE_TEMPORARY, 't', tmk_ANSIColor_DarkRed);
    PARSE_MODE(FILE_ATTRIBUTE_REPARSE_POINT, 'l', tmk_ANSIColor_DarkGreen);
    if (entry.mode & FILE_ATTRIBUTE_DIRECTORY) {
      tmk_setFontANSIColor(tmk_ANSIColor_DarkYellow, tmk_FontLayer_Foreground);
    } else {
      tmk_resetFontColors();
    }
    if (tmk_isStreamRedirected(tmk_Stream_Output)) {
      tmk_write(entry.mode & FILE_ATTRIBUTE_DIRECTORY ? " d " : " - ");
    } else {
      tmk_write(entry.mode & FILE_ATTRIBUTE_DIRECTORY ? "  " : "  ");
    }
    tmk_resetFontColors();
    tmk_writeLine("%s", entry.name);
  }
  resetArenaAllocator(entriesAllocator_g);
  resetArenaAllocator(entriesDataAllocator_g);
}
#else
static struct Credential *findCredential(bool isUser, unsigned int id) {
  if (!userCredentialsAllocator_g || !groupCredentialsAllocator_g) {
    return NULL;
  }
  struct ArenaAllocator *credentials =
      isUser ? userCredentialsAllocator_g : groupCredentialsAllocator_g;
  struct ArenaAllocator *buffer =
      isUser ? userCredentialsDataAllocator_g : groupCredentialsDataAllocator_g;
  for (size_t offset = 0; offset < credentials->use; ++offset) {
    if (((struct Credential *)credentials->buffer + offset)->id == id) {
      return (struct Credential *)credentials->buffer + offset;
    }
  }
  struct Credential *credential = allocateArenaMemory(credentials, 1);
  const char *name;
  if (isUser) {
    struct passwd *user = getpwuid(id);
    if (!user) {
      freeArenaMemory(credentials, 1);
      return NULL;
    }
    name = user->pw_name;
  } else {
    struct group *group = getgrgid(id);
    if (!group) {
      freeArenaMemory(credentials, 1);
      return NULL;
    }
    name = group->gr_name;
  }
  credential->id = id;
  credential->name.length = strlen(name);
  credential->name.buffer =
      allocateArenaMemory(buffer, credential->name.length + 1);
  memcpy(credential->name.buffer, name, credential->name.length + 1);
  return credential;
}

static void readDirectory(const char *directoryPath) {
  DIR *directoryStream = opendir(directoryPath);
  if (!directoryStream) {
    struct stat directoryStat;
    writeError(stat(directoryPath, &directoryStat)
                   ? "can not find the entry \"%s\"."
               : S_ISDIR(directoryStat.st_mode)
                   ? "can not open the directory \"%s\"."
                   : "the entry \"%s\" is not a directory.",
               directoryPath);
    return;
  }
  createArenaAllocator("entriesAllocator_g", sizeof(struct Entry), 30000,
                       &entriesAllocator_g);
  createArenaAllocator("entriesDataAllocator_g", sizeof(char), 2097152,
                       &entriesDataAllocator_g);
  createArenaAllocator("temporaryDataAllocator_g", sizeof(char), 500,
                       &temporaryDataAllocator_g);
  createArenaAllocator("userCredentialsAllocator_g", sizeof(struct Credential),
                       20, &userCredentialsAllocator_g);
  createArenaAllocator("userCredentialsDataAllocator_g", sizeof(char), 320,
                       &userCredentialsDataAllocator_g);
  createArenaAllocator("groupCredentialsAllocator_g", sizeof(struct Credential),
                       20, &groupCredentialsAllocator_g);
  createArenaAllocator("groupCredentialsDataAllocator_g", sizeof(char), 320,
                       &groupCredentialsDataAllocator_g);
  size_t directoryPathLength = strlen(directoryPath);
  int indexColumnLength = 3;
  int userColumnLength = 4;
  int groupColumnLength = 5;
  int sizeColumnLength = 4;
  for (struct dirent *entryData; (entryData = readdir(directoryStream));) {
    if (entryData->d_name[0] == '.' &&
        (!entryData->d_name[1] ||
         (entryData->d_name[1] == '.' && !entryData->d_name[2]))) {
      continue;
    }
    struct Entry *entry = allocateArenaMemory(entriesAllocator_g, 1);
    size_t entryNameSize = strlen(entryData->d_name) + 1;
    size_t entryPathSize = directoryPathLength + entryNameSize + 1;
    char *entryPath =
        allocateArenaMemory(temporaryDataAllocator_g, entryPathSize);
    memcpy(entryPath, directoryPath, directoryPathLength);
    entryPath[directoryPathLength] = '/';
    memcpy(entryPath + directoryPathLength + 1, entryData->d_name,
           entryNameSize);
    struct stat entryStat;
    lstat(entryPath, &entryStat);
    if (S_ISLNK(entryStat.st_mode)) {
      char link[256];
      link[readlink(entryPath, link, sizeof(link))] = 0;
      size_t linkSize = strlen(link) + 1;
      entry->link = allocateArenaMemory(entriesDataAllocator_g, linkSize);
      memcpy(entry->link, link, linkSize);
    } else {
      entry->link = NULL;
    }
    size_t sizeLength;
    entry->size =
        formatSize(&sizeLength, entryStat.st_size, S_ISDIR(entryStat.st_mode));
    entry->modifiedTime = entryStat.st_mtime;
    entry->mode = entryStat.st_mode;
    entry->user = findCredential(true, entryStat.st_uid);
    entry->group = findCredential(false, entryStat.st_gid);
    entry->name = allocateArenaMemory(entriesDataAllocator_g, entryNameSize);
    memcpy(entry->name, entryData->d_name, entryNameSize);
    if (entry->user) {
      SAVE_GREATER(userColumnLength, entry->user->name.length);
    }
    if (entry->group) {
      SAVE_GREATER(groupColumnLength, entry->group->name.length);
    }
    SAVE_GREATER(sizeColumnLength, sizeLength);
    resetArenaAllocator(temporaryDataAllocator_g);
  }
  closedir(directoryStream);
  int totalDigitsForIndex = countDigits(entriesAllocator_g->use);
  SAVE_GREATER(indexColumnLength, totalDigitsForIndex);
  tmk_setFontANSIColor(tmk_ANSIColor_DarkYellow, tmk_FontLayer_Foreground);
  if (!tmk_isStreamRedirected(tmk_Stream_Output)) {
    tmk_write(" ");
  }
  tmk_resetFontColors();
  char *directoryFullPath = allocateArenaMemory(temporaryDataAllocator_g, 256);
  realpath(directoryPath, directoryFullPath);
  tmk_setFontWeight(tmk_FontWeight_Bold);
  tmk_writeLine("%s:", directoryFullPath);
  tmk_resetFontWeight();
  resetArenaAllocator(temporaryDataAllocator_g);
  qsort(entriesAllocator_g->buffer, entriesAllocator_g->use,
        sizeof(struct Entry), sortEntriesAlphabetically);
  tmk_setFontWeight(tmk_FontWeight_Bold);
  tmk_writeLine("%*s %-*s %-*s %-*s %*s %-*s Name", indexColumnLength, "No.",
                groupColumnLength, "Group", userColumnLength, "User", 17,
                "Modified Date", sizeColumnLength, "Size", 13, "Mode");
  tmk_resetFontWeight();
  writeLines(7, indexColumnLength, groupColumnLength, userColumnLength, 17,
             sizeColumnLength, 13, 20);
  if (!entriesAllocator_g->use) {
    tmk_setFontANSIColor(tmk_ANSIColor_LightBlack, tmk_FontLayer_Foreground);
    tmk_writeLine("%*s",
                  29 + indexColumnLength + groupColumnLength +
                      userColumnLength + sizeColumnLength,
                  "DIRECTORY IS EMPTY");
    tmk_resetFontColors();
  }
  for (size_t index = 0; index < entriesAllocator_g->use; ++index) {
    struct Entry entry = *((struct Entry *)entriesAllocator_g->buffer + index);
    tmk_write("%*zu ", indexColumnLength, index + 1);
    if (entry.group) {
      tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
      tmk_write("%-*s ", groupColumnLength, entry.group->name.buffer);
    } else {
      tmk_write("%-*c ", groupColumnLength, '-');
    }
    if (entry.user) {
      tmk_setFontANSIColor(tmk_ANSIColor_DarkGreen, tmk_FontLayer_Foreground);
      tmk_write("%-*s ", userColumnLength, entry.user->name.buffer);
    } else {
      tmk_resetFontColors();
      tmk_write("%-*c ", userColumnLength, '-');
    }
    struct tm *localModifiedTime = localtime(&entry.modifiedTime);
    size_t modifiedDateSize;
    char *modifiedDate = formatModifiedDate(
        localModifiedTime->tm_mon, localModifiedTime->tm_mday,
        localModifiedTime->tm_year + 1900, &modifiedDateSize);
    tmk_setFontANSIColor(tmk_ANSIColor_DarkYellow, tmk_FontLayer_Foreground);
    tmk_write("%s ", modifiedDate);
    resetArenaAllocator(temporaryDataAllocator_g);
    tmk_setFontANSIColor(tmk_ANSIColor_DarkMagenta, tmk_FontLayer_Foreground);
    tmk_write("%02d:%02d ", localModifiedTime->tm_hour,
              localModifiedTime->tm_min);
    if (entry.size) {
      tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
      tmk_write("%*s ", sizeColumnLength, entry.size);
    } else {
      tmk_resetFontColors();
      tmk_write("%*c ", sizeColumnLength, '-');
    }
    PARSE_MODE(S_IRUSR, 'r', tmk_ANSIColor_DarkRed);
    PARSE_MODE(S_IWUSR, 'w', tmk_ANSIColor_DarkGreen);
    PARSE_MODE(S_IXUSR, 'x', tmk_ANSIColor_DarkYellow);
    PARSE_MODE(S_IRGRP, 'r', tmk_ANSIColor_DarkRed);
    PARSE_MODE(S_IWGRP, 'w', tmk_ANSIColor_DarkGreen);
    PARSE_MODE(S_IXGRP, 'x', tmk_ANSIColor_DarkYellow);
    PARSE_MODE(S_IROTH, 'r', tmk_ANSIColor_DarkRed);
    PARSE_MODE(S_IWOTH, 'w', tmk_ANSIColor_DarkGreen);
    PARSE_MODE(S_IXOTH, 'x', tmk_ANSIColor_DarkYellow);
    tmk_setFontANSIColor(tmk_ANSIColor_DarkMagenta, tmk_FontLayer_Foreground);
    tmk_write(" %-3o ",
              entry.mode & (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP |
                            S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH));
    if (S_ISREG(entry.mode)) {
      tmk_resetFontColors();
    } else {
      tmk_setFontANSIColor(S_ISDIR(entry.mode)    ? tmk_ANSIColor_DarkYellow
                           : S_ISLNK(entry.mode)  ? tmk_ANSIColor_DarkBlue
                           : S_ISBLK(entry.mode)  ? tmk_ANSIColor_DarkMagenta
                           : S_ISCHR(entry.mode)  ? tmk_ANSIColor_DarkGreen
                           : S_ISFIFO(entry.mode) ? tmk_ANSIColor_DarkBlue
                                                  : tmk_ANSIColor_DarkCyan,
                           tmk_FontLayer_Foreground);
    }
    if (tmk_isStreamRedirected(tmk_Stream_Output)) {
      tmk_write(S_ISDIR(entry.mode)    ? "d "
                : S_ISLNK(entry.mode)  ? "l "
                : S_ISBLK(entry.mode)  ? "b "
                : S_ISCHR(entry.mode)  ? "c "
                : S_ISFIFO(entry.mode) ? "f "
                : S_ISREG(entry.mode)  ? "- "
                                       : "s ");
    } else {
      tmk_write(S_ISDIR(entry.mode)    ? " "
                : S_ISLNK(entry.mode)  ? "󰌷 "
                : S_ISBLK(entry.mode)  ? "󰇖 "
                : S_ISCHR(entry.mode)  ? "󱣴 "
                : S_ISFIFO(entry.mode) ? "󰟦 "
                : S_ISREG(entry.mode)  ? " "
                                       : "󱄙 ");
    }
    tmk_resetFontColors();
    tmk_write("%s", entry.name);
    if (entry.link) {
      tmk_setFontANSIColor(tmk_ANSIColor_LightBlack, tmk_FontLayer_Foreground);
      tmk_write(" -> ");
      tmk_resetFontColors();
      tmk_writeLine(entry.link);
    } else {
      tmk_writeLine("");
    }
  }
  resetArenaAllocator(entriesAllocator_g);
  resetArenaAllocator(entriesDataAllocator_g);
}
#endif

static void writeLines(size_t totalLines, ...) {
  va_list arguments;
  va_start(arguments, totalLines);
  for (size_t index = 0; index < totalLines; ++index) {
    for (int length = va_arg(arguments, int); length; --length) {
      tmk_write("-");
    }
    if (index < totalLines - 1) {
      tmk_write(" ");
    }
  }
  va_end(arguments);
  tmk_writeLine("");
}

static char *formatModifiedDate(int month, int day, int year,
                                size_t *bufferSize) {
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
  char *buffer = allocateArenaMemory(temporaryDataAllocator_g, *bufferSize);
  memcpy(buffer, formatBuffer, *bufferSize);
  return buffer;
}

static int sortEntriesAlphabetically(const void *entryI, const void *entryII) {
  return strcmp(((struct Entry *)entryI)->name,
                ((struct Entry *)entryII)->name);
}

static char *formatSize(size_t *bufferLength, unsigned long long entrySize,
                        bool isDirectory) {
  if (isDirectory) {
    *bufferLength = 0;
    return NULL;
  }
  char formatBuffer[9];
  struct SIMultiplier multipliers[] = {
      {1099511627776, 'T'}, {1073741824, 'G'}, {1048576, 'M'}, {1024, 'k'}};
  for (size_t index = 0; index < 4; ++index) {
    if (entrySize >= multipliers[index].value) {
      float formatedSize = entrySize / multipliers[index].value;
      sprintf(formatBuffer, "%.1f%cB", formatedSize, multipliers[index].prefix);
      goto end_l;
    }
  }
  sprintf(formatBuffer, "%lldB", entrySize);
end_l:
  *bufferLength = strlen(formatBuffer);
  char *buffer = allocateArenaMemory(entriesDataAllocator_g, *bufferLength + 1);
  memcpy(buffer, formatBuffer, *bufferLength + 1);
  return buffer;
}

static int countDigits(size_t number) {
  int totalDigits;
  for (totalDigits = !number; number; number /= 10) {
    ++totalDigits;
  }
  return totalDigits;
}

static void writeErrorArguments(const char *format, va_list arguments) {
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_writeError("[ERROR] ");
  tmk_resetFontColors();
  tmk_writeError("dl");
  tmk_setFontANSIColor(tmk_ANSIColor_LightBlack, tmk_FontLayer_Foreground);
  tmk_writeError(" (code 1)");
  tmk_resetFontColors();
  tmk_writeError(": ");
  tmk_writeErrorArgumentsLine(format, arguments);
  exitCode_g = 1;
}

static void writeError(const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  writeErrorArguments(format, arguments);
  va_end(arguments);
}

static void throwError(const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  writeErrorArguments(format, arguments);
  va_end(arguments);
  exit(1);
}

static void writeHelp(void) {
  tmk_setFontWeight(tmk_FontWeight_Bold);
  tmk_write("Usage: ");
  tmk_resetFontWeight();
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_write("%s ", PROGRAM_NAME);
  tmk_resetFontColors();
  tmk_write("[");
  tmk_setFontEffects(tmk_FontEffect_Underline);
  tmk_write("PATH");
  tmk_resetFontEffects();
  tmk_write(" | ");
  tmk_setFontEffects(tmk_FontEffect_Underline);
  tmk_write("OPTION");
  tmk_resetFontEffects();
  tmk_writeLine("]...");
  tmk_writeLine("Directory Lister - List the entries inside of directories "
                "given their paths.");
  tmk_writeLine("");
  tmk_writeLine(
      "If no path is given, it considers the current active directory.");
  tmk_writeLine("");
  tmk_writeLine("For each entry, it shows:");
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_write("    * ");
  tmk_resetFontColors();
#if defined(_WIN32)
  tmk_writeLine("Its domain and user.");
#else
  tmk_writeLine("Its group and user.");
#endif
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_write("    * ");
  tmk_resetFontColors();
  tmk_writeLine("Its last modified date.");
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_write("    * ");
  tmk_resetFontColors();
  tmk_writeLine(
      "Its size in a human-readable unit: terabyte (TB), gigabyte (GB),");
  tmk_writeLine("      megabyte (MB), kilobyte (kB) or byte (B).");
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_write("    * ");
  tmk_resetFontColors();
#if defined(_WIN32)
  tmk_writeLine("Its attributes: hidden (h), archive (a), read-only (r), "
                "temporary (t) and");
  tmk_writeLine("      reparse point (l).");
#else
  tmk_writeLine("Its read (r), write (w), execute (x) and lack (-) permissions "
                "for user,");
  tmk_writeLine("      group and others, respectively, and its representation "
                "in octal base.");
#endif
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_write("    * ");
  tmk_resetFontColors();
  tmk_writeLine("An icon or, in case of the terminal output stream is "
                "redirected, a letter");
  tmk_write("      representing its type: ");
#if defined(_WIN32)
  tmk_writeLine("directory (d) or file (-).");
#else
  tmk_writeLine("directory (d), symlink (l), block device (d),");
  tmk_writeLine(
      "      character device (c), fifo (f), socket (s) or regular (-).");
#endif
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_write("    * ");
  tmk_resetFontColors();
#if defined(_WIN32)
  tmk_writeLine("Its name.");
#else
  tmk_writeLine(
      "Its name. If it is a symlink, it also contains the path it points to.");
#endif
  tmk_writeLine("");
  tmk_setFontWeight(tmk_FontWeight_Bold);
  tmk_writeLine("AVAILABLE OPTIONS");
  tmk_resetFontWeight();
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_write("       --help  ");
  tmk_resetFontColors();
  tmk_writeLine("shows these usage instructions.");
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_write("    --version  ");
  tmk_resetFontColors();
  tmk_writeLine("shows its version.");
  tmk_writeLine("");
  tmk_setFontWeight(tmk_FontWeight_Bold);
  tmk_writeLine("SOURCE CODE");
  tmk_resetFontWeight();
  tmk_write("Its source is available at <");
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_setFontEffects(tmk_FontEffect_Underline);
  tmk_write("https://github.com/skippyr/dl");
  tmk_resetFontEffects();
  tmk_resetFontColors();
  tmk_writeLine(">.");
  tmk_writeLine("");
  tmk_setFontWeight(tmk_FontWeight_Bold);
  tmk_writeLine("HELP");
  tmk_resetFontWeight();
  tmk_writeLine("If you need help related to this project, open a new issue in "
                "its issues");
  tmk_write("page (");
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_setFontEffects(tmk_FontEffect_Underline);
  tmk_write("https://github.com/skippyr/dl/issues");
  tmk_resetFontEffects();
  tmk_resetFontColors();
  tmk_writeLine(") or send me an");
  tmk_write("e-mail (");
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_setFontEffects(tmk_FontEffect_Underline);
  tmk_write("mailto:skippyr.developer@icloud.com");
  tmk_resetFontEffects();
  tmk_resetFontColors();
  tmk_writeLine(") describing what is going on.");
  tmk_writeLine("");
  tmk_setFontWeight(tmk_FontWeight_Bold);
  tmk_writeLine("CONTRIBUTING");
  tmk_resetFontWeight();
  tmk_writeLine("This project is open to review and possibly accept "
                "contributions, specially");
  tmk_writeLine("fixes and suggestions. If you are interested, send your "
                "contribution to its pull");
  tmk_write("requests page (");
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_setFontEffects(tmk_FontEffect_Underline);
  tmk_write("https://github.com/skippyr/dl/pulls");
  tmk_resetFontEffects();
  tmk_resetFontColors();
  tmk_writeLine(") or to my");
  tmk_write("e-mail (");
  tmk_setFontANSIColor(tmk_ANSIColor_DarkRed, tmk_FontLayer_Foreground);
  tmk_setFontEffects(tmk_FontEffect_Underline);
  tmk_write("mailto:skippyr.developer@icloud.com");
  tmk_resetFontEffects();
  tmk_resetFontColors();
  tmk_writeLine(").");
  tmk_writeLine("");
  tmk_setFontWeight(tmk_FontWeight_Bold);
  tmk_writeLine("LICENSE");
  tmk_resetFontWeight();
  tmk_writeLine("This is free software licensed under the BSD-3-Clause License "
                "that comes WITH");
  tmk_writeLine("NO WARRANTY. Refer to the LICENSE file that comes in its "
                "source code for license");
  tmk_writeLine("and copyright details.");
}

static void writeVersion(void) {
  tmk_setFontANSIColor(tmk_ANSIColor_DarkGreen, tmk_FontLayer_Foreground);
  tmk_write("dl ");
  tmk_resetFontColors();
  tmk_setFontANSIColor(tmk_ANSIColor_DarkYellow, tmk_FontLayer_Foreground);
  tmk_write("%s ", PROGRAM_VERSION);
  tmk_resetFontColors();
  tmk_writeLine("compiled for %s %s.", PROGRAM_PLATFORM, PROGRAM_ARCHITECTURE);
  tmk_setFontANSIColor(tmk_ANSIColor_LightBlack, tmk_FontLayer_Foreground);
  tmk_write("Copyright (c) 2024, Sherman Rofeman <");
  tmk_setFontEffects(tmk_FontEffect_Underline);
  tmk_write("skippyr.developer@icloud.com");
  tmk_resetFontEffects();
  tmk_writeLine(">");
  tmk_resetFontColors();
}

static void *allocateHeapMemory(size_t totalBytes) {
  void *allocation = malloc(totalBytes);
  if (allocation) {
    return allocation;
  }
  throwError("can not allocate %zuB of memory on the heap.", totalBytes);
  return NULL;
}

static void createArenaAllocator(const char *name, size_t unit, size_t capacity,
                                 struct ArenaAllocator **allocator) {
  if (*allocator) {
    return;
  }
  *allocator = allocateHeapMemory(sizeof(struct ArenaAllocator));
  size_t nameSize = strlen(name) + 1;
  (*allocator)->name = allocateHeapMemory(nameSize);
  memcpy((*allocator)->name, name, nameSize);
  (*allocator)->buffer = allocateHeapMemory(capacity * unit);
  (*allocator)->capacity = capacity;
  (*allocator)->unit = unit;
  (*allocator)->use = 0;
}

static void *allocateArenaMemory(struct ArenaAllocator *allocator,
                                 size_t totalAllocations) {
  if (allocator->buffer +
          (allocator->use + totalAllocations) * allocator->unit >
      allocator->buffer + allocator->capacity * allocator->unit) {
    throwError("can not allocate %zu items (%zuB) in the allocator \"%s\".",
               totalAllocations, totalAllocations * allocator->unit,
               allocator->name);
  }
  void *allocation = allocator->buffer + allocator->use * allocator->unit;
  allocator->use += totalAllocations;
  return allocation;
}

static void resetArenaAllocator(struct ArenaAllocator *allocator) {
  allocator->use = 0;
}

static void freeArenaMemory(struct ArenaAllocator *allocator,
                            size_t totalAllocations) {
  if (totalAllocations > allocator->use) {
    throwError("can not free %zu items (%zuB) from allocator \"%s\".",
               totalAllocations, totalAllocations * allocator->unit,
               allocator->name);
  }
  allocator->use -= totalAllocations;
}

static void freeArenaAllocator(struct ArenaAllocator *allocator) {
  if (!allocator) {
    return;
  }
  free(allocator->buffer);
  free(allocator->name);
  free(allocator);
}

int main(int totalRawCMDArguments, const char **rawCMDArguments) {
  struct tmk_CMDArguments cmdArguments;
  tmk_getCMDArguments(totalRawCMDArguments, rawCMDArguments, &cmdArguments);
  if (cmdArguments.totalArguments == 1) {
#if defined(_WIN32)
    readDirectory(".", L".");
#else
    readDirectory(".");
#endif
    goto end_l;
  }
  for (int offset = 1; offset < cmdArguments.totalArguments; ++offset) {
    PARSE_OPTION("help", writeHelp());
    PARSE_OPTION("version", writeVersion());
  }
  for (int offset = 1; offset < cmdArguments.totalArguments; ++offset) {
    if (cmdArguments.utf8Arguments[offset][0] == '-' &&
        cmdArguments.utf8Arguments[offset][1] == '-') {
      writeError("the option \"%s\" is unrecognized or malformed.",
                 cmdArguments.utf8Arguments[offset]);
      continue;
    }
#if defined(_WIN32)
    readDirectory(cmdArguments.utf8Arguments[offset],
                  cmdArguments.utf16Arguments[offset]);
#else
    readDirectory(cmdArguments.utf8Arguments[offset]);
#endif
  }
end_l:
#if DEBUG
#if defined(_WIN32)
  debugArenaAllocator(temporaryWideDataAllocator_g);
  debugArenaAllocator(credentialsAllocator_g);
  debugArenaAllocator(credentialsDataAllocator_g);
#else
  debugArenaAllocator(userCredentialsAllocator_g);
  debugArenaAllocator(userCredentialsDataAllocator_g);
  debugArenaAllocator(groupCredentialsAllocator_g);
  debugArenaAllocator(groupCredentialsDataAllocator_g);
#endif
  debugArenaAllocator(entriesAllocator_g);
  debugArenaAllocator(entriesDataAllocator_g);
  debugArenaAllocator(temporaryDataAllocator_g);
#endif
  tmk_freeCMDArguments(&cmdArguments);
#if defined(_WIN32)
  freeArenaAllocator(temporaryWideDataAllocator_g);
  freeArenaAllocator(credentialsAllocator_g);
  freeArenaAllocator(credentialsDataAllocator_g);
#else
  freeArenaAllocator(userCredentialsAllocator_g);
  freeArenaAllocator(userCredentialsDataAllocator_g);
  freeArenaAllocator(groupCredentialsAllocator_g);
  freeArenaAllocator(groupCredentialsDataAllocator_g);
#endif
  freeArenaAllocator(entriesAllocator_g);
  freeArenaAllocator(entriesDataAllocator_g);
  freeArenaAllocator(temporaryDataAllocator_g);
  return exitCode_g;
}
