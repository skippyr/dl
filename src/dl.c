#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <tdk.h>

#ifdef __x86_64__
#define ARCHITECTURE "x86_64"
#else
#define ARCHITECTURE "x86"
#endif
#define BUFSZ 1024
#define PARSEPERM(perm_a, ch_r, clr_r) \
	if (e->mode & perm_a) { \
		tdk_set256clr(clr_r, TDK_LYRFG); \
		putchar(ch_r); \
	} else { \
		tdk_set256clr(TDK_CLRDFT, TDK_LYRFG); \
		putchar('-'); \
	}
#define PARSEOPT(opt_a, act_a) \
	if (!strcmp("--" opt_a, argv[i])) { \
		act_a; \
		return 0; \
	}
#define SAVEGREATER(buf_a, val_a) \
	if ((int)val_a > buf_a) \
		buf_a = val_a;

struct arn {
	char *name;
	char *buf;
	size_t use;
	size_t unit;
	size_t cap;
};

struct cred {
	char *name;
	size_t namelen;
	uid_t id;
};

struct ent {
	char *name;
	char *lnk;
	char *sz;
	struct cred *usr;
	struct cred *grp;
	time_t modtt;
	mode_t mode;
};

struct linux_dirent64 {
	ino64_t d_ino;
	off64_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};

static void allocarn(struct arn **a, char *name, size_t unit, size_t cap);
static void *allocarnmem(struct arn *a, size_t s);
static void *allocheapmem(size_t s);
static int countdigits(long n);
static struct cred *findcred(uid_t id, int isusr);
static char *fmtsz(struct stat *s, size_t *len);
static void freearn(struct arn *a);
static void listdir(char *dirpath);
static int sortents(const void *e0, const void *e1);
static void threrr(char *fmt, ...);
static void writeentno(int no, int align);
static void writehelp(void);
static void writeln(int totln, ...);
static void writewarn(char *fmt, ...);

static struct arn *usrarn_g = NULL;
static struct arn *usrdarn_g = NULL;
static struct arn *grparn_g = NULL;
static struct arn *grpdarn_g = NULL;
static struct arn *entarn_g = NULL;
static struct arn *entdarn_g = NULL;
static struct arn *tmparn_g = NULL;
static char *buf_g = NULL;
static int isouttty_g;
static int exitcd_g = 0;

static void
allocarn(struct arn **a, char *name, size_t unit, size_t cap)
{
	size_t namesize;
	if (*a)
		return;
	namesize = strlen(name) + 1;
	*a = allocheapmem(sizeof(struct arn));
	(*a)->buf = allocheapmem(unit * cap);
	(*a)->use = 0;
	(*a)->unit = unit;
	(*a)->cap = cap;
	(*a)->name = allocheapmem(namesize);
	memcpy((*a)->name, name, namesize);
}

static void *
allocarnmem(struct arn *a, size_t s)
{
	void *alloc;
	if (a->use + s <= a->cap) {
		alloc = a->buf + a->unit * a->use;
		a->use += s;
		return alloc;
	}
	threrr("can not allocate %zuB of memory on arena \"%s\".", s, a->name);
	return NULL;
}

static void *
allocheapmem(size_t s)
{
	void *alloc = malloc(s);
	if (alloc)
		return alloc;
	threrr("can not allocate %zuB of memory on the heap.", s);
	return NULL;
}

static int
countdigits(long n)
{
	int i;
	for (i = 0; n; n /= 10)
		++i;
	return i;
}

static struct cred *
findcred(uid_t id, int isusr)
{
	struct arn *carn = isusr ? usrarn_g : grparn_g;
	struct arn *darn = isusr ? usrdarn_g : grpdarn_g;
	size_t i;
	struct cred *c;
	struct passwd *p;
	struct group *g;
	char *name;
	for (i = 0; i < carn->use; ++i)
		if ((c = (struct cred *)carn->buf + i)->id == id)
			return c;
	if (isusr) {
		if (!(p = getpwuid(id)))
			return NULL;
		name = p->pw_name;
	} else {
		if (!(g = getgrgid(id)))
			return NULL;
		name = g->gr_name;
	}
	c = allocarnmem(carn, 1);
	c->id = id;
	c->namelen = strlen(name);
	c->name = allocarnmem(darn, c->namelen + 1);
	memcpy(c->name, name, c->namelen + 1);
	return c;
}

static char *
fmtsz(struct stat *s, size_t *len)
{
	float mval[] = {1099511627776, 1073741824, 1048576, 1024};
	char mpref[] = {'T', 'G', 'M', 'k'};
	char fmt[10];
	char num[7];
	char pref = 0;
	char *buf;
	float sz;
	float tmp;
	int seppos = 0;
	int i;
	int j;
	if (S_ISDIR(s->st_mode))
		return NULL;
	for (i = 0; i < 4; ++i)
		if (s->st_size >= mval[i]) {
			sz = s->st_size / mval[i];
			pref = mpref[i];
			sprintf(num, ((tmp = sz - (int)sz) >= 0 && tmp < 0.1) ||
				    tmp >= 0.95 ? "%.0f" : "%.1f", sz);
			seppos = -2;
			goto fmt_l;
		}
	sprintf(num, "%ld", s->st_size);
fmt_l:
	seppos += strlen(num) - 3;
	for (i = 0, j = 0; num[i]; (void)(++i && ++j)) {
		if (i == seppos && seppos)
			fmt[j++] = ',';
		fmt[j] = num[i];
	}
	if (pref)
		fmt[j++] = pref;
	*(short *)(fmt + j) = *(short *)"B";
	buf = allocarnmem(entdarn_g, (*len = j + 1) + 1);
	memcpy(buf, fmt, *len + 1);
	return buf;
}

static void
freearn(struct arn *a)
{
	if (!a)
		return;
	free(a->buf);
	free(a->name);
	free(a);
}

static void
listdir(char *dirpath)
{
	int fd = open(dirpath, O_RDONLY);
	struct stat s;
	struct linux_dirent64 *d;
	struct ent *e;
	struct tm *t;
	size_t dirpathlen;
	size_t entnamesz;
	size_t entpathsz;
	size_t tmpsz;
	char *entpath;
	char *tmp;
	char moddate[12];
	long i;
	long readsz;
	int nocollen = 3;
	int usrcollen = 4;
	int grpcollen = 5;
	int szcollen = 4;
	if (fd < 0) {
		writewarn(stat(dirpath, &s) ? "can not find the entry \"%s\"." :
				 S_ISDIR(s.st_mode) ? "can not open the directory \"%s\"." :
				 "the entry \"%s\" is not a directory.", dirpath);
		return;
	}
	if (!buf_g)
		buf_g = allocheapmem(BUFSZ);
	allocarn(&usrarn_g, "usrarn_g", sizeof(struct cred), 20);
	allocarn(&usrdarn_g, "usrdarn_g", sizeof(char), 320);
	allocarn(&grparn_g, "grparn_g", sizeof(struct cred), 20);
	allocarn(&grpdarn_g, "grpdarn_g", sizeof(char), 320);
	allocarn(&entarn_g, "entarn_g", sizeof(struct ent), 30000);
	allocarn(&entdarn_g, "entdarn_g", sizeof(char), 2097152);
	allocarn(&tmparn_g, "tmparn_g", sizeof(char), 600);
	dirpathlen = strlen(dirpath);
	while ((readsz = syscall(SYS_getdents64, fd, buf_g, BUFSZ)) > 0)
		for (i = 0; i < readsz; i += d->d_reclen) {
			d = (struct linux_dirent64 *)(buf_g + i);
			if (*d->d_name == '.' && (!d->d_name[1] || (d->d_name[1] == '.' &&
				!d->d_name[2])))
				continue;
			e = allocarnmem(entarn_g, 1);
			entnamesz = strlen(d->d_name) + 1;
			entpathsz = dirpathlen + entnamesz + 1;
			entpath = allocarnmem(tmparn_g, entpathsz);
			memcpy(entpath, dirpath, dirpathlen);
			entpath[dirpathlen] = '/';
			memcpy(entpath + dirpathlen + 1, d->d_name, entnamesz);
			lstat(entpath, &s);
			e->name = allocarnmem(entdarn_g, entnamesz);
			memcpy(e->name, d->d_name, entnamesz);
			if (d->d_type == DT_LNK) {
				tmp = allocarnmem(tmparn_g, 300);
				tmp[readlink(entpath, tmp, 300)] = 0;
				tmpsz = strlen(tmp) + 1;
				e->lnk = allocarnmem(entdarn_g, tmpsz);
				memcpy(e->lnk, tmp, tmpsz);
			} else {
				e->lnk = NULL;
			}
			tmparn_g->use = 0;
			e->usr = findcred(s.st_uid, 1);
			e->grp = findcred(s.st_gid, 0);
			e->modtt = s.st_mtime;
			e->mode = s.st_mode;
			e->sz = fmtsz(&s, &tmpsz);
			if (e->usr)
				SAVEGREATER(usrcollen, e->usr->namelen);
			if (e->grp)
				SAVEGREATER(grpcollen, e->grp->namelen);
			if (e->sz)
				SAVEGREATER(szcollen, tmpsz);
		}
	close(fd);
	i = countdigits(entarn_g->use) + (entarn_g->use > 1000);
	SAVEGREATER(nocollen, i);
	tdk_set256clr(TDK_CLRYLW, TDK_LYRFG);
	if (isouttty_g)
		printf("󰝰 ");
	tdk_set256clr(TDK_CLRDFT, TDK_LYRFG);
	realpath(dirpath, (tmp = allocarnmem(tmparn_g, 300)));
	tdk_setwgt(TDK_WGTBLD);
	printf("%s:\n", tmp);
	tdk_setwgt(TDK_WGTDFT);
	tmparn_g->use = 0;
	printf("%*s %-*s %-*s %-*s %*s %-*s Name\n", nocollen, "No.", grpcollen,
		   "Group", usrcollen, "User", 17, "Modified Date", szcollen, "Size",
		   13, "Permissions");
	writeln(7, nocollen, grpcollen, usrcollen, 17, szcollen, 13, 16);
	if (!entarn_g->use) {
		tdk_set256clr(TDK_CLRLBLK, TDK_LYRFG);
		printf("%*s\n", 27 + nocollen + grpcollen + usrcollen + szcollen,
			   "DIRECTORY IS EMPTY");
		tdk_set256clr(TDK_CLRDFT, TDK_LYRFG);
	}
	qsort(entarn_g->buf, entarn_g->use, sizeof(struct ent), sortents);
	for (i = 0; i < (long)entarn_g->use; ++i) {
		e = (struct ent *)entarn_g->buf + i;
		writeentno(i + 1, nocollen);
		putchar(' ');
		if (e->grp) {
			tdk_set256clr(TDK_CLRRED, TDK_LYRFG);
			printf("%-*s ", grpcollen, e->grp->name);
		} else {
			printf("%-*c ", grpcollen, '-');
		}
		if (e->usr) {
			tdk_set256clr(TDK_CLRGRN, TDK_LYRFG);
			printf("%-*s ", usrcollen, e->usr->name);
		} else {
			tdk_set256clr(TDK_CLRDFT, TDK_LYRFG);
			printf("%-*c ", usrcollen, '-');
		}
		t = localtime(&e->modtt);
		strftime(moddate, sizeof(moddate), "%b/%d/%Y", t);
		tdk_set256clr(TDK_CLRYLW, TDK_LYRFG);
		printf("%s ", moddate);
		tdk_set256clr(TDK_CLRMAG, TDK_LYRFG);
		printf("%02d:%02d ", t->tm_hour, t->tm_min);
		if (e->sz) {
			tdk_set256clr(TDK_CLRRED, TDK_LYRFG);
			printf("%*s ", szcollen, e->sz);
		} else {
			tdk_set256clr(TDK_CLRDFT, TDK_LYRFG);
			printf("%*c ", szcollen, '-');
		}
		PARSEPERM(S_IRUSR, 'r', TDK_CLRRED);
		PARSEPERM(S_IWUSR, 'w', TDK_CLRGRN);
		PARSEPERM(S_IXUSR, 'x', TDK_CLRYLW);
		PARSEPERM(S_IRGRP, 'r', TDK_CLRRED);
		PARSEPERM(S_IWGRP, 'w', TDK_CLRGRN);
		PARSEPERM(S_IXGRP, 'x', TDK_CLRYLW);
		PARSEPERM(S_IROTH, 'r', TDK_CLRRED);
		PARSEPERM(S_IWOTH, 'w', TDK_CLRGRN);
		PARSEPERM(S_IXOTH, 'x', TDK_CLRYLW);
		tdk_set256clr(TDK_CLRMAG, TDK_LYRFG);
		printf(" %03o ", e->mode & (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP |
									S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH |
									S_IXOTH));
		tdk_set256clr(S_ISDIR(e->mode) ? TDK_CLRYLW : S_ISLNK(e->mode) ?
					  TDK_CLRBLE : S_ISBLK(e->mode) ? TDK_CLRMAG :
					  S_ISCHR(e->mode) ? TDK_CLRGRN : S_ISFIFO(e->mode) ?
					  TDK_CLRBLE : S_ISREG(e->mode) ? TDK_CLRDFT : TDK_CLRCYN,
					  TDK_LYRFG);
		isouttty_g ?
		printf(S_ISDIR(e->mode) ? "󰝰 " : S_ISLNK(e->mode) ? "󰌷 " :
			   S_ISBLK(e->mode) ? "󰇖 " : S_ISCHR(e->mode) ? "󱣴 " :
			   S_ISFIFO(e->mode) ? "󰟦 " : S_ISREG(e->mode) ? " " : "󱄙 ") :
		printf(S_ISDIR(e->mode) ? "d " : S_ISLNK(e->mode) ? "l " :
			   S_ISBLK(e->mode) ? "b " : S_ISCHR(e->mode) ? "c " :
			   S_ISFIFO(e->mode) ? "f " : S_ISREG(e->mode) ? "r " : "s ");
		tdk_set256clr(TDK_CLRDFT, TDK_LYRFG);
		printf(e->name);
		if (e->lnk) {
			tdk_set256clr(TDK_CLRBLE, TDK_LYRFG);
			printf(" -> ");
			tdk_set256clr(TDK_CLRDFT, TDK_LYRFG);
			printf(e->lnk);
		}
		putchar('\n');
	}
	entarn_g->use = 0;
	entdarn_g->use = 0;
}

static int
sortents(const void *e0, const void *e1)
{
	return strcmp(((struct ent *)e0)->name, ((struct ent *)e1)->name);
}

static void
threrr(char *fmt, ...)
{
	va_list args;
	tdk_set256clr(TDK_CLRRED, TDK_LYRFG);
	fflush(stdout);
	fprintf(stderr, "[ERROR] ");
	tdk_set256clr(TDK_CLRLBLK, TDK_LYRFG);
	fflush(stdout);
	printf("(exit code 1) ");
	tdk_set256clr(TDK_CLRDFT, TDK_LYRFG);
	tdk_setwgt(TDK_WGTBLD);
	fflush(stdout);
	fprintf(stderr, "dl: ");
	tdk_setwgt(TDK_WGTDFT);
	fflush(stdout);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(1);
}

static void
writeln(int totln, ...)
{
	va_list args;
	int i;
	int len;
	va_start(args, totln);
	for (i = 0; i < totln; ++i) {
		for (len = va_arg(args, int); len; --len)
			putchar('-');
		if (i < totln - 1)
			putchar(' ');
	}
	va_end(args);
	putchar('\n');
}

static void
writeentno(int no, int align)
{
	char buf[7];
	int len;
	int seppos;
	int i;
	int lpad;
	sprintf(buf, "%d", no);
	for (len = 0; buf[len]; ++len);
	lpad = align - len - (len >= 4);
	for (i = 0; i < lpad; ++i)
		putchar(' ');
	for (seppos = len - 3, i = 0; i < len; ++i) {
		if (i == seppos && seppos)
			putchar(',');
		putchar(buf[i]);
	}
}

static void
writehelp(void)
{
	tdk_setwgt(TDK_WGTBLD);
	printf("Usage: ");
	tdk_setwgt(TDK_WGTDFT);
	printf("dl ");
	tdk_set256clr(TDK_CLRLBLK, TDK_LYRFG);
	printf("[OPTION | PATH]...\n");
	tdk_set256clr(TDK_CLRDFT, TDK_LYRFG);
	printf("Lists the contents of directories given its PATH(s).\n");
	printf("If no path is provided, the current directory is considered.\n\n");
	tdk_setwgt(TDK_WGTBLD);
	printf("AVAILABLE OPTIONS\n");
	tdk_setwgt(TDK_WGTDFT);
	tdk_set256clr(TDK_CLRLBLK, TDK_LYRFG);
	printf("    --help     ");
	tdk_set256clr(TDK_CLRDFT, TDK_LYRFG);
	printf("show these help instructions.\n");
	tdk_set256clr(TDK_CLRLBLK, TDK_LYRFG);
	printf("    --version  ");
	tdk_set256clr(TDK_CLRDFT, TDK_LYRFG);
	printf("show its version and platform.\n\n");
	tdk_setwgt(TDK_WGTBLD);
	printf("SOURCE CODE\n");
	tdk_setwgt(TDK_WGTDFT);
	printf("Its source code is available at: <");
	tdk_seteff(TDK_EFFUND, 1);
	printf("https://github.com/skippyr/dl");
	tdk_seteff(TDK_EFFUND, 0);
	printf(">.\n");
}

static void
writewarn(char *fmt, ...)
{
	va_list args;
	tdk_set256clr(TDK_CLRYLW, TDK_LYRFG);
	fflush(stdout);
	fprintf(stderr, "[WARNING] ");
	tdk_set256clr(TDK_CLRDFT, TDK_LYRFG);
	tdk_setwgt(TDK_WGTBLD);
	fflush(stdout);
	fprintf(stderr, "dl: ");
	tdk_setwgt(TDK_WGTDFT);
	fflush(stdout);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	exitcd_g = 1;
}

int
main(int argc, char **argv)
{
	int i;
	isouttty_g = isatty(1);
	if (argc == 1) {
		listdir(".");
		goto end_l;
	}
	for (i = 1; i < argc; ++i) {
		PARSEOPT("version", printf("dl %s (compiled for Linux %s)\n", VERSION,
								   ARCHITECTURE));
		PARSEOPT("help", writehelp());
		if (*argv[i] == '-' && (argv[i][1] == '-' || !argv[i][2]))
			threrr("the option \"%s\" is unrecognized.", argv[i]);
	}
	for (i = 1; i < argc; ++i)
		listdir(argv[i]);
end_l:
	if (buf_g)
		free(buf_g);
	freearn(usrarn_g);
	freearn(usrdarn_g);
	freearn(grparn_g);
	freearn(grpdarn_g);
	freearn(entarn_g);
	freearn(entdarn_g);
	freearn(tmparn_g);
	return exitcd_g;
}
