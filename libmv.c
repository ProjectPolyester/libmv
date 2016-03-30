#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <dlfcn.h>
#include <utime.h>
#include <dirent.h>

/* There's no d_off on GNU/kFreeBSD */
#if defined(__FreeBSD_kernel__)
#define D_OFF(X) (-1)
#else
#define D_OFF(X) (X)
#endif
	
#include "localdecls.h"

#define DEBUG 1  

#define LOGLEVEL (LOG_USER | LOG_INFO | LOG_PID)
#define BUFSIZE 1024

#define error(X) (X < 0 ? strerror(errno) : "success")

int __installwatch_refcount = 0;
int __installwatch_timecount = 0;

#define REFCOUNT __installwatch_refcount++
#define TIMECOUNT __installwatch_timecount++

static time_t (*true_time) (time_t *);
static int (*true_chdir)(const char *);
static int (*true_chmod)(const char *, mode_t);
static int (*true_chown)(const char *, uid_t, gid_t);
static int (*true_chroot)(const char *);
static int (*true_creat)(const char *, mode_t);
static int (*true_fchmod)(int, mode_t);
static int (*true_fchown)(int, uid_t, gid_t);
static FILE *(*true_fopen)(const char *,const char*);
FILE *fopen(const char * name,const char* mode)
{
	if(true_fopen==NULL) {
        mtrace_init();
    }

	fopen(name,mode);
	
}
static int (*true_ftruncate)(int, TRUNCATE_T);
static char *(*true_getcwd)(char*,size_t);
static int (*true_lchown)(const char *, uid_t, gid_t);
static int (*true_link)(const char *, const char *);
static int (*true_mkdir)(const char *, mode_t);
static int (*true_xmknod)(int ver,const char *, mode_t, dev_t *);
static int (*true_open)(const char *, int, ...);
static DIR *(*true_opendir)(const char *);
static struct dirent *(*true_readdir)(DIR *dir);
#if (GLIBC_MINOR <= 4)
static int (*true_readlink)(const char*,char *,size_t);
#else
static ssize_t (*true_readlink)(const char*,char *,size_t);
#endif
static char *(*true_realpath)(const char *,char *);
static int (*true_rename)(const char *, const char *);
static int (*true_rmdir)(const char *);
static int (*true_xstat)(int,const char *,struct stat *);
static int (*true_lxstat)(int,const char *,struct stat *);

#if(GLIBC_MINOR >= 10)

static int (*true_scandir)(	const char *,struct dirent ***,
				int (*)(const struct dirent *),
				int (*)(const struct dirent **,const struct dirent **));

#else

static int (*true_scandir)(	const char *,struct dirent ***,
				int (*)(const struct dirent *),
				int (*)(const void *,const void *));
#endif

static int (*true_symlink)(const char *, const char *);
static int (*true_truncate)(const char *, TRUNCATE_T);
static int (*true_unlink)(const char *);
static int (*true_utime)(const char *,const struct utimbuf *);
static int (*true_utimes)(const char *,const struct timeval *);
static int (*true_access)(const char *, int);
static int (*true_setxattr)(const char *,const char *,const void *,
                            size_t, int);
static int (*true_removexattr)(const char *,const char *);

#if(GLIBC_MINOR >= 1)

static int (*true_creat64)(const char *, __mode_t);
static FILE *(*true_fopen64)(const char *,const char *);
static int (*true_ftruncate64)(int, __off64_t);
static int (*true_open64)(const char *, int, ...);
static struct dirent64 *(*true_readdir64)(DIR *dir);

#if(GLIBC_MINOR >= 10)
static int (*true_scandir64)(	const char *,struct dirent64 ***,
				int (*)(const struct dirent64 *),
				int (*)(const struct dirent64 **,const struct dirent64 **));
#else
static int (*true_scandir64)(	const char *,struct dirent64 ***,
				int (*)(const struct dirent64 *),
				int (*)(const void *,const void *));
#endif
static int (*true_xstat64)(int,const char *, struct stat64 *);
static int (*true_lxstat64)(int,const char *, struct stat64 *);
static int (*true_truncate64)(const char *, __off64_t);

#endif

#if (GLIBC_MINOR >= 4)
static int (*true_openat)(int, const char *, int, ...);
static int (*true_fchmodat)(int, const char *, mode_t, int);
static int (*true_fchownat)(int, const char *, uid_t, gid_t, int);
static int (*true_fxstatat)(int, int, const char *, struct stat *, int);
static int (*true_fxstatat64)(int, int, const char *, struct stat64 *, int);
static int (*true_linkat)(int, const char *, int, const char *, int);
static int (*true_mkdirat)(int, const char *, mode_t);
static int (*true_readlinkat)(int, const char *, char *, size_t);
static int (*true_xmknodat)(int, int, const char *, mode_t, dev_t *);
static int (*true_renameat)(int, const char *, int, const char *);
static int (*true_symlinkat)(const char *, int, const char *);
static int (*true_unlinkat)(int, const char *, int);
#endif

#if defined __GNUC__ && __GNUC__>=2
	#define inline inline
#else
	#define inline
#endif	

static inline int true_stat(const char *pathname,struct stat *info) {
	return true_xstat(_STAT_VER,pathname,info);
}

static inline int true_mknod(const char *pathname,mode_t mode,dev_t dev) {
	return true_xmknod(_MKNOD_VER,pathname,mode,&dev);
}

static inline int true_lstat(const char *pathname,struct stat *info) {
	return true_lxstat(_STAT_VER,pathname,info);
}

#if (GLIBC_MINOR >= 4)
static inline int true_fstatat(int dirfd, const char *pathname, struct stat *info, int flags) {
	return true_fxstatat(_STAT_VER, dirfd, pathname, info, flags);
}

static inline int true_fstatat64(int dirfd, const char *pathname, struct stat64 *info, int flags) {
	return true_fxstatat64(_STAT_VER, dirfd, pathname, info, flags);
}

static inline int true_mknodat(int dirfd, const char *pathname,mode_t mode,dev_t dev) {
	return true_xmknodat(_MKNOD_VER, dirfd, pathname, mode, &dev);
}

#endif

  /* A few defines to fix things a little */
#define INSTW_OK 0 
  /* If not set, no work with translation is allowed */
#define INSTW_INITIALIZED	(1<<0)
  /* If not set, a wrapped function only do its "real" job */
#define INSTW_OKWRAP		(1<<1)
#define INSTW_OKBACKUP		(1<<2)
#define INSTW_OKTRANSL		(1<<3)

#define INSTW_TRANSLATED	(1<<0)
  /* Indicates that a translated file is identical to original */
#define INSTW_IDENTITY  	(1<<1)

  /* The file currently exists in the root filesystem */
#define INSTW_ISINROOT		(1<<6)
  /* The file currently exists in the translated filesystem */
#define INSTW_ISINTRANSL	(1<<7)

#define _BACKUP "/BACKUP"
#define _TRANSL "/TRANSL"

  /* The root that contains all the needed metas-infos */
#define _META   "/META"
  /* We store under this subtree the translated status */
#define _MTRANSL _TRANSL
  /* We construct under this subtree fake directory listings */
#define _MDIRLS  "/DIRLS"  

  /* String cell used to chain excluded paths */
typedef struct string_t string_t;
struct string_t {
	char *string;
	string_t *next;
};	

  /* Used to keep all infos needed to cope with backup, translated fs... */
typedef struct instw_t {
	  /*
	   * global fields 
	   */
	int gstatus;
	int dbglvl;
	pid_t pid;
	char *root;
	char *backup;
	char *transl;
	char *meta;
	char *mtransl;
	char *mdirls;
	  /* the list of all the paths excluded from translation */
	string_t *exclude;
	
	  /*
	   * per instance fields
	   */
	int error;
	int status;
	  /* the public path, hiding translation */
	char path[PATH_MAX+1];
	  /* the public resolved path, hiding translation */
	char reslvpath[PATH_MAX+1];  
	  /* the real resolved path, exposing tranlsation */
	char truepath[PATH_MAX+1];
	  /* the real translated path */
	char translpath[PATH_MAX+1];
	  /* the list of all the equiv paths conducing to "reslvpath" */
	string_t *equivpaths;  
	  /* the real path used to flag translation status */
	char mtranslpath[PATH_MAX+1];
	  /* the path given to a wrapped opendir */
	char mdirlspath[PATH_MAX+1];
} instw_t;

static instw_t __instw;

static int canonicalize(const char *,char *);
static int reduce(char *);
static int make_path(const char *);
static int copy_path(const char *,const char *);
static inline int path_excluded(const char *);
static int unlink_recursive(const char *);

int expand_path(string_t **,const char *,const char *);
int parse_suffix(char *,char *,const char *);

void mtrace_init(void)
{
    true_fopen = dlsym(RTLD_NEXT, "fopen");
    if (NULL == true_fopen) {
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
    }
}

