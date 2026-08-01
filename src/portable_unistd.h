#ifndef portable_unistd_h_INCLUDED
#define portable_unistd_h_INCLUDED

// this file is to have portability across the various platforms.
// if you implement on your own os, just edit the definitions here:
//
// Everything here reports failure the POSIX way: a sentinel return value plus
// `errno`. The Windows backend translates GetLastError() into errno so that
// callers can disambiguate ENOENT / EACCES / EEXIST / ENOTDIR / ELOOP
// identically on every platform.
//
// Paths are UTF-8 in and UTF-8 out. Both '/' and '\\' are accepted as
// separators; zpk_getcwd() always hands back '/'.
//
// Deliberately absent: any access()/stat() style "can I?" predicate. Attempt
// the operation and inspect errno instead -- a pre-check is a TOCTOU race.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define ZPK_WINDOWS 1
#else
#define ZPK_POSIX 1
#endif

// ---------------------------------------------------------------------------
// open() flags
// ---------------------------------------------------------------------------
// Our own values rather than the platform's, so a caller can't accidentally
// pass a native flag we don't know how to translate.

#define ZPK_O_RDONLY 0x0001
#define ZPK_O_WRONLY 0x0002
#define ZPK_O_RDWR 0x0004
#define ZPK_O_CREAT 0x0008
#define ZPK_O_EXCL 0x0010
#define ZPK_O_TRUNC 0x0020
#define ZPK_O_APPEND 0x0040
// Fail with ELOOP if the final component is a symlink (Windows: any reparse
// point). Does NOT protect intermediate components -- see the note on
// zpk_openat() about confinement.
#define ZPK_O_NOFOLLOW 0x0080
// Close on exec. This is the default on Windows; on POSIX you almost always
// want it, since anything you fork/exec inherits the fd otherwise.
#define ZPK_O_CLOEXEC 0x0100
#define ZPK_O_BINARY 0x0200

#define ZPK_ACCMODE(f) ((f) & (ZPK_O_RDONLY | ZPK_O_WRONLY | ZPK_O_RDWR))

// ===========================================================================
#if defined(ZPK_POSIX)
// ===========================================================================

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// A directory handle. Cheap to copy, must be closed with zpk_dirclose().
typedef int ZpkDir;

#define ZPK_DIR_INVALID (-1)
// Resolve relative to the process working directory (i.e. AT_FDCWD).
#define ZPK_DIR_CWD (AT_FDCWD)

static inline int zpk__native_oflags(int flags) {
  int o;
  switch (ZPK_ACCMODE(flags)) {
  case ZPK_O_WRONLY:
    o = O_WRONLY;
    break;
  case ZPK_O_RDWR:
    o = O_RDWR;
    break;
  default:
    o = O_RDONLY;
    break;
  }
  if (flags & ZPK_O_CREAT) {
    o |= O_CREAT;
  }
  if (flags & ZPK_O_EXCL) {
    o |= O_EXCL;
  }
  if (flags & ZPK_O_TRUNC) {
    o |= O_TRUNC;
  }
  if (flags & ZPK_O_APPEND) {
    o |= O_APPEND;
  }
  if (flags & ZPK_O_NOFOLLOW) {
    o |= O_NOFOLLOW;
  }
  if (flags & ZPK_O_CLOEXEC) {
    o |= O_CLOEXEC;
  }
  return o; // ZPK_O_BINARY has no meaning here
}

static inline ZpkDir zpk_diropen(const char *path) {
  return open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

static inline ZpkDir zpk_diropenat(ZpkDir base, const char *rel) {
  return openat(base, rel, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

static inline void zpk_dirclose(ZpkDir dir) {
  if (dir >= 0) {
    close(dir);
  }
}

static inline int zpk_openat(ZpkDir base, const char *rel, int flags,
                             unsigned mode) {
  return openat(base, rel, zpk__native_oflags(flags), mode);
}

static inline char *zpk_getcwd(void) {
  size_t cap = 256;
  for (;;) {
    char *buf = (char *)malloc(cap);
    if (buf == NULL) {
      errno = ENOMEM;
      return NULL;
    }
    if (getcwd(buf, cap) != NULL) {
      return buf;
    }
    free(buf);
    if (errno != ERANGE) {
      return NULL;
    }
    if (cap >= (1u << 20)) {
      errno = ENAMETOOLONG;
      return NULL;
    }
    cap *= 2;
  }
}

// ===========================================================================
#else // ZPK_WINDOWS
// ===========================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h> // NtCreateFile: link with -lntdll

#include <fcntl.h>
#include <io.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(st) (((NTSTATUS)(st)) >= 0)
#endif
#ifndef OBJ_CASE_INSENSITIVE
#define OBJ_CASE_INSENSITIVE 0x00000040L
#endif
#ifndef FILE_OVERWRITE
#define FILE_OVERWRITE 4
#endif

// A directory handle. NULL means "the working directory"; every other value is
// a real kernel handle.
//
// Relative opens go through NtCreateFile with OBJECT_ATTRIBUTES.RootDirectory
// set to this handle, which is the actual openat primitive on Windows: the
// object manager resolves the name against the handle, so no path is ever
// reconstructed or concatenated. Win32's CreateFileW cannot do this -- the
// nearest approximation is re-deriving the directory's path with
// GetFinalPathNameByHandleW and joining strings, which is both racy and
// (verified under wine) capable of handing back a stale name after a rename.
//
// Two useful consequences: MAX_PATH does not apply, and the object manager
// does not interpret ".." -- a relative name containing it fails rather than
// escaping the directory, giving confinement close to Linux's RESOLVE_BENEATH.
typedef HANDLE ZpkDir;

#define ZPK_DIR_INVALID (INVALID_HANDLE_VALUE)
#define ZPK_DIR_CWD ((ZpkDir)NULL)

static inline void zpk__set_errno(DWORD e) {
  switch (e) {
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
  case ERROR_INVALID_NAME:
  case ERROR_BAD_NETPATH:
  case ERROR_BAD_PATHNAME:
  case ERROR_NO_MORE_FILES:
    errno = ENOENT;
    break;
  case ERROR_ACCESS_DENIED:
  case ERROR_SHARING_VIOLATION:
  case ERROR_LOCK_VIOLATION:
  case ERROR_WRITE_PROTECT:
  case ERROR_CURRENT_DIRECTORY:
    errno = EACCES;
    break;
  case ERROR_FILE_EXISTS:
  case ERROR_ALREADY_EXISTS:
    errno = EEXIST;
    break;
  case ERROR_DIRECTORY:
    errno = ENOTDIR;
    break;
  case ERROR_DIR_NOT_EMPTY:
    errno = ENOTEMPTY;
    break;
  case ERROR_CANT_RESOLVE_FILENAME:
  case ERROR_SYMLINK_NOT_SUPPORTED:
    errno = ELOOP;
    break;
  case ERROR_BUFFER_OVERFLOW:
  case ERROR_FILENAME_EXCED_RANGE:
    errno = ENAMETOOLONG;
    break;
  case ERROR_TOO_MANY_OPEN_FILES:
    errno = EMFILE;
    break;
  case ERROR_NOT_ENOUGH_MEMORY:
  case ERROR_OUTOFMEMORY:
    errno = ENOMEM;
    break;
  case ERROR_DISK_FULL:
    errno = ENOSPC;
    break;
  case ERROR_INVALID_HANDLE:
    errno = EBADF;
    break;
  case ERROR_INVALID_PARAMETER:
    errno = EINVAL;
    break;
  default:
    errno = EIO;
    break;
  }
}

// UTF-8 -> UTF-16. Returns a malloc'd string, NULL on failure (errno set).
static inline wchar_t *zpk__widen(const char *s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
  if (n <= 0) {
    zpk__set_errno(GetLastError());
    return NULL;
  }
  wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
  if (w == NULL) {
    errno = ENOMEM;
    return NULL;
  }
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) <= 0) {
    zpk__set_errno(GetLastError());
    free(w);
    return NULL;
  }
  return w;
}

// UTF-16 -> UTF-8. Returns a malloc'd string, NULL on failure (errno set).
static inline char *zpk__narrow(const wchar_t *w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  if (n <= 0) {
    zpk__set_errno(GetLastError());
    return NULL;
  }
  char *s = (char *)malloc((size_t)n);
  if (s == NULL) {
    errno = ENOMEM;
    return NULL;
  }
  if (WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL) <= 0) {
    zpk__set_errno(GetLastError());
    free(s);
    return NULL;
  }
  return s;
}

static inline int zpk__is_absolute(const char *p) {
  if (p[0] == '/' || p[0] == '\\') {
    return 1;
  }
  return (p[0] != '\0' && p[1] == ':') ? 1 : 0;
}

static inline void zpk__set_errno_nt(NTSTATUS st) {
  zpk__set_errno(RtlNtStatusToDosError(st));
}

// The openat core: resolve `rel` against the directory handle `base` without
// ever naming the directory. `base` must be a real handle -- callers route
// ZPK_DIR_CWD and absolute paths to CreateFileW instead.
static inline HANDLE zpk__nt_openat(ZpkDir base, const char *rel,
                                    ACCESS_MASK access, ULONG file_attrs,
                                    ULONG disposition, ULONG options) {
  wchar_t *w = zpk__widen(rel);
  if (w == NULL) {
    return INVALID_HANDLE_VALUE;
  }

  size_t wlen = wcslen(w);
  // UNICODE_STRING measures bytes in a USHORT, and a leading separator would
  // mean "start from the object root", ignoring RootDirectory entirely.
  if (wlen == 0 || wlen * sizeof(wchar_t) > 0xFFFCu) {
    free(w);
    errno = (wlen == 0) ? ENOENT : ENAMETOOLONG;
    return INVALID_HANDLE_VALUE;
  }
  for (size_t i = 0; i < wlen; i++) {
    if (w[i] == L'/') {
      w[i] = L'\\';
    }
  }
  if (w[0] == L'\\') {
    free(w);
    errno = EINVAL;
    return INVALID_HANDLE_VALUE;
  }

  UNICODE_STRING name;
  name.Buffer = w;
  name.Length = (USHORT)(wlen * sizeof(wchar_t));
  name.MaximumLength = (USHORT)(name.Length + sizeof(wchar_t));

  OBJECT_ATTRIBUTES oa;
  oa.Length = sizeof(oa);
  oa.RootDirectory = base;
  oa.ObjectName = &name;
  oa.Attributes = OBJ_CASE_INSENSITIVE;
  oa.SecurityDescriptor = NULL;
  oa.SecurityQualityOfService = NULL;

  IO_STATUS_BLOCK iosb;
  HANDLE h = NULL;
  NTSTATUS st = NtCreateFile(
      &h, access | SYNCHRONIZE, &oa, &iosb, NULL, file_attrs,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, disposition,
      options | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
  free(w);

  if (!NT_SUCCESS(st)) {
    zpk__set_errno_nt(st);
    return INVALID_HANDLE_VALUE;
  }
  return h;
}

static inline ZpkDir zpk_diropen(const char *path) {
  wchar_t *w = zpk__widen(path);
  if (w == NULL) {
    return ZPK_DIR_INVALID;
  }
  // FILE_FLAG_BACKUP_SEMANTICS is what makes CreateFileW willing to hand back
  // a handle to a directory at all. The CRT's _wopen cannot do this.
  HANDLE h = CreateFileW(w, FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  free(w);
  if (h == INVALID_HANDLE_VALUE) {
    zpk__set_errno(GetLastError());
  }
  return h;
}

static inline ZpkDir zpk_diropenat(ZpkDir base, const char *rel) {
  if (base == ZPK_DIR_CWD || zpk__is_absolute(rel)) {
    return zpk_diropen(rel);
  }
  return zpk__nt_openat(base, rel, FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
                        FILE_ATTRIBUTE_NORMAL, FILE_OPEN, FILE_DIRECTORY_FILE);
}

static inline void zpk_dirclose(ZpkDir dir) {
  if (dir != ZPK_DIR_INVALID && dir != ZPK_DIR_CWD) {
    CloseHandle(dir);
  }
}

static inline int zpk_openat(ZpkDir base, const char *rel, int flags,
                             unsigned mode) {
  wchar_t *full = zpk__resolve(base, rel);
  if (full == NULL) {
    return -1;
  }

  DWORD access;
  switch (ZPK_ACCMODE(flags)) {
  case ZPK_O_WRONLY:
    access = GENERIC_WRITE;
    break;
  case ZPK_O_RDWR:
    access = GENERIC_READ | GENERIC_WRITE;
    break;
  default:
    access = GENERIC_READ;
    break;
  }

  DWORD disp;
  if (flags & ZPK_O_CREAT) {
    if (flags & ZPK_O_EXCL) {
      disp = CREATE_NEW;
    } else if (flags & ZPK_O_TRUNC) {
      disp = CREATE_ALWAYS;
    } else {
      disp = OPEN_ALWAYS;
    }
  } else if (flags & ZPK_O_TRUNC) {
    disp = TRUNCATE_EXISTING;
  } else {
    disp = OPEN_EXISTING;
  }

  DWORD attrs = FILE_ATTRIBUTE_NORMAL;
  if ((flags & ZPK_O_CREAT) && !(mode & 0200u)) {
    attrs = FILE_ATTRIBUTE_READONLY;
  }
  if (flags & ZPK_O_NOFOLLOW) {
    attrs |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  // bInheritHandle defaults to FALSE, so ZPK_O_CLOEXEC is already the behaviour.
  HANDLE h = CreateFileW(full, access,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, disp, attrs, NULL);
  free(full);
  if (h == INVALID_HANDLE_VALUE) {
    zpk__set_errno(GetLastError());
    return -1;
  }

  // FILE_FLAG_OPEN_REPARSE_POINT *opens* the link rather than refusing it,
  // whereas POSIX O_NOFOLLOW fails outright. Reject after the fact to match.
  if (flags & ZPK_O_NOFOLLOW) {
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileInformationByHandle(h, &info) &&
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
      CloseHandle(h);
      errno = ELOOP;
      return -1;
    }
  }

  int crt = 0;
  if (ZPK_ACCMODE(flags) == ZPK_O_RDONLY) {
    crt |= _O_RDONLY;
  }
  if (flags & ZPK_O_APPEND) {
    crt |= _O_APPEND;
  }
  crt |= (flags & ZPK_O_BINARY) ? _O_BINARY : _O_TEXT;
  crt |= _O_NOINHERIT;

  int fd = _open_osfhandle((intptr_t)h, crt);
  if (fd < 0) {
    CloseHandle(h);
    errno = EMFILE;
    return -1;
  }
  return fd; // the fd owns the handle now; close it with _close/fclose
}

static inline char *zpk_getcwd(void) {
  DWORD need = GetCurrentDirectoryW(0, NULL);
  if (need == 0) {
    zpk__set_errno(GetLastError());
    return NULL;
  }
  wchar_t *w = (wchar_t *)malloc((size_t)need * sizeof(wchar_t));
  if (w == NULL) {
    errno = ENOMEM;
    return NULL;
  }
  DWORD got = GetCurrentDirectoryW(need, w);
  if (got == 0 || got >= need) {
    zpk__set_errno(GetLastError());
    free(w);
    return NULL;
  }
  char *s = zpk__narrow(w);
  free(w);
  if (s == NULL) {
    return NULL;
  }
  for (char *p = s; *p != '\0'; p++) {
    if (*p == '\\') {
      *p = '/';
    }
  }
  return s;
}

#endif // ZPK_WINDOWS

// ===========================================================================
// Platform independent helpers
// ===========================================================================

// fopen() relative to a directory handle. `mode` is an ordinary stdio mode
// string; binary is assumed unless it contains 't', which is the opposite of
// stdio's default but the right default for a package manager.
//
// On success the returned stream owns the descriptor: fclose() is enough.
static inline FILE *zpk_fopenat(ZpkDir base, const char *rel, const char *mode,
                                unsigned perm) {
  int flags = 0;
  switch (mode[0]) {
  case 'r':
    flags = ZPK_O_RDONLY;
    break;
  case 'w':
    flags = ZPK_O_WRONLY | ZPK_O_CREAT | ZPK_O_TRUNC;
    break;
  case 'a':
    flags = ZPK_O_WRONLY | ZPK_O_CREAT | ZPK_O_APPEND;
    break;
  default:
    errno = EINVAL;
    return NULL;
  }
  if (strchr(mode, '+') != NULL) {
    flags = (flags & ~(ZPK_O_RDONLY | ZPK_O_WRONLY)) | ZPK_O_RDWR;
  }
  if (strchr(mode, 'x') != NULL) {
    flags |= ZPK_O_EXCL;
  }
  if (strchr(mode, 't') == NULL) {
    flags |= ZPK_O_BINARY;
  }
  flags |= ZPK_O_CLOEXEC;

  int fd = zpk_openat(base, rel, flags, perm);
  if (fd < 0) {
    return NULL;
  }

#if defined(ZPK_WINDOWS)
  FILE *f = _fdopen(fd, mode);
#else
  FILE *f = fdopen(fd, mode);
#endif
  if (f == NULL) {
    int saved = errno;
#if defined(ZPK_WINDOWS)
    _close(fd);
#else
    close(fd);
#endif
    errno = saved;
    return NULL;
  }
  return f;
}

// Close a descriptor returned by zpk_openat().
static inline void zpk_close(int fd) {
#if defined(ZPK_WINDOWS)
  _close(fd);
#else
  close(fd);
#endif
}

#endif // portable_unistd_h_INCLUDED
