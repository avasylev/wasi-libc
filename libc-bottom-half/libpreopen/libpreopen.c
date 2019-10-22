//! Preopen functionality for WASI libc, for emulating support for absolute
//! path names on top of WASI's OCap-style API.
//!
//! This file is derived from code in libpreopen, the upstream for which is:
//!
//!     https://github.com/musec/libpreopen
//!
//! and which bears the following copyrights and license:

/*-
 * Copyright (c) 2016-2017 Stanley Uche Godfrey
 * Copyright (c) 2016-2018 Jonathan Anderson
 * All rights reserved.
 *
 * This software was developed at Memorial University under the
 * NSERC Discovery program (RGPIN-2015-06048).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef _REENTRANT
#error "__wasilibc_register_preopened_fd doesn't yet support multiple threads"
#endif

#define _ALL_SOURCE
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <assert.h>
#include <wasi/libc.h>
#include <wasi/libc-find-relpath.h>

////////////////////////////////////////////////////////////////////////////////
//
// POSIX API compatibility wrappers
//
////////////////////////////////////////////////////////////////////////////////

int
open(const char *path, int flags, ...)
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(path, __WASI_RIGHT_PATH_OPEN, 0,
                                        &relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    // WASI libc's openat ignores the mode argument, so don't bother passing
    // the actual mode value through.
    return openat(dirfd, relative_path, flags);
}

int
access(const char *path, int mode)
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(path, __WASI_RIGHT_PATH_FILESTAT_GET, 0,
                                        &relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    return faccessat(dirfd, relative_path, mode, 0);
}

int
lstat(const char *path, struct stat *st)
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(path, __WASI_RIGHT_PATH_FILESTAT_GET, 0,
                                        &relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    return fstatat(dirfd, relative_path, st, AT_SYMLINK_NOFOLLOW);
}

int
rename(const char *from, const char *to)
{
    const char *from_relative_path;
    int from_dirfd = __wasilibc_find_relpath(from, __WASI_RIGHT_PATH_RENAME_SOURCE, 0,
                                             &from_relative_path);

    const char *to_relative_path;
    int to_dirfd = __wasilibc_find_relpath(to, __WASI_RIGHT_PATH_RENAME_TARGET, 0,
                                           &to_relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (from_dirfd == -1 || to_dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    return renameat(from_dirfd, from_relative_path, to_dirfd, to_relative_path);
}

int
stat(const char *path, struct stat *st)
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(path, __WASI_RIGHT_PATH_FILESTAT_GET, 0,
                                        &relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    return fstatat(dirfd, relative_path, st, AT_SYMLINK_NOFOLLOW);
}

int
unlink(const char *path)
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(path, __WASI_RIGHT_PATH_UNLINK_FILE, 0,
                                        &relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    // `unlinkat` ends up importing `__wasi_path_remove_directory` even
    // though we're not passing `AT_REMOVEDIR` here. So instead, use a
    // specialized function which just imports `__wasi_path_unlink_file`.
    return __wasilibc_unlinkat(dirfd, relative_path);
}

int
rmdir(const char *pathname)
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(pathname, __WASI_RIGHT_PATH_REMOVE_DIRECTORY, 0,
                                        &relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    return __wasilibc_rmdirat(dirfd, relative_path);
}

int
remove(const char *pathname)
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(pathname,
                                        __WASI_RIGHT_PATH_UNLINK_FILE |
                                        __WASI_RIGHT_PATH_REMOVE_DIRECTORY,
                                        0,
                                        &relative_path);

    // If searching for both file and directory rights failed, try searching
    // for either individually.
    if (dirfd == -1) {
        dirfd = __wasilibc_find_relpath(pathname, __WASI_RIGHT_PATH_UNLINK_FILE, 0,
                                        &relative_path);
        if (dirfd == -1) {
            dirfd = __wasilibc_find_relpath(pathname, __WASI_RIGHT_PATH_REMOVE_DIRECTORY, 0,
                                            &relative_path);
        }
    }

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    int r = __wasilibc_unlinkat(dirfd, relative_path);
    if (r != 0 && (errno == EISDIR || errno == ENOTCAPABLE))
        r = __wasilibc_rmdirat(dirfd, relative_path);
    return r;
}

int
link(const char *oldpath, const char *newpath)
{
    const char *old_relative_path;
    int old_dirfd = __wasilibc_find_relpath(oldpath, __WASI_RIGHT_PATH_LINK_SOURCE, 0,
                                            &old_relative_path);

    const char *new_relative_path;
    int new_dirfd = __wasilibc_find_relpath(newpath, __WASI_RIGHT_PATH_LINK_TARGET, 0,
                                            &new_relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (old_dirfd == -1 || new_dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    return linkat(old_dirfd, old_relative_path, new_dirfd, new_relative_path, 0);
}

int
mkdir(const char *pathname, mode_t mode)
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(pathname, __WASI_RIGHT_PATH_CREATE_DIRECTORY, 0,
                                        &relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    return mkdirat(dirfd, relative_path, mode);
}

DIR *
opendir(const char *name)
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(name, __WASI_RIGHT_PATH_OPEN, 0,
                                        &relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return NULL;
    }

    return opendirat(dirfd, relative_path);
}

ssize_t
readlink(const char *pathname, char *buf, size_t bufsiz)
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(pathname, __WASI_RIGHT_PATH_READLINK, 0,
                                        &relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    return readlinkat(dirfd, relative_path, buf, bufsiz);
}

int
scandir(
    const char *dirp,
    struct dirent ***namelist,
    int (*filter)(const struct dirent *),
    int (*compar)(const struct dirent **, const struct dirent **))
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(dirp,
                                        __WASI_RIGHT_PATH_OPEN,
                                        __WASI_RIGHT_FD_READDIR,
                                        &relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    return scandirat(dirfd, relative_path, namelist, filter, compar);
}

int
symlink(const char *target, const char *linkpath)
{
    const char *relative_path;
    int dirfd = __wasilibc_find_relpath(linkpath, __WASI_RIGHT_PATH_SYMLINK, 0,
                                        &relative_path);

    // If we can't find a preopened directory handle to open this file with,
    // indicate that the program lacks the capabilities.
    if (dirfd == -1) {
        errno = ENOTCAPABLE;
        return -1;
    }

    return symlinkat(target, dirfd, relative_path);
}

////////////////////////////////////////////////////////////////////////////////
//
// Support library
//
////////////////////////////////////////////////////////////////////////////////

/// An entry in a po_map.
struct po_map_entry {
    /// The name this file or directory is mapped to.
    ///
    /// This name should look like a path, but it does not necessarily need
    /// match to match the path it was originally obtained from.
    const char *name;

    /// File descriptor (which may be a directory)
    int fd;

    /// Capability rights associated with the file descriptor
    __wasi_rights_t rights_base;
    __wasi_rights_t rights_inheriting;
};

/// A vector of po_map_entry.
struct po_map {
    struct po_map_entry *entries;
    size_t capacity;
    size_t length;
};

static struct po_map global_map;

/// Is a directory a prefix of a given path?
///
/// @param   dir     a directory path, e.g., `/foo/bar`
/// @param   dirlen  the length of @b dir
/// @param   path    a path that may have @b dir as a prefix,
///                  e.g., `/foo/bar/baz`
static bool
po_isprefix(const char *dir, size_t dirlen, const char *path)
{
    assert(dir != NULL);
    assert(path != NULL);

    // Allow an empty string as a prefix of any relative path.
    if (path[0] != '/' && dirlen == 0)
        return true;

    size_t i;
    for (i = 0; i < dirlen; i++)
    {
        if (path[i] != dir[i])
            return false;
    }

    // Ignore trailing slashes in directory names.
    while (i > 0 && dir[i - 1] == '/') {
        --i;
    }

    return path[i] == '/' || path[i] == '\0';
}

/// Enlarge a @ref po_map's capacity.
///
/// This results in new memory being allocated and existing entries being copied.
/// If the allocation fails, the function will return -1.
static int
po_map_enlarge(void)
{
    size_t start_capacity = 4;
    size_t new_capacity = global_map.capacity == 0 ?
                          start_capacity :
                          global_map.capacity * 2;

    struct po_map_entry *enlarged =
        calloc(sizeof(struct po_map_entry), new_capacity);
    if (enlarged == NULL) {
        return -1;
    }
    memcpy(enlarged, global_map.entries, global_map.length * sizeof(*enlarged));
    free(global_map.entries);
    global_map.entries = enlarged;
    global_map.capacity = new_capacity;
    return 0;
}

/// Assert that the global_map is valid.
#ifdef NDEBUG
#define po_map_assertvalid()
#else
static void
po_map_assertvalid(void)
{
    const struct po_map_entry *entry;
    size_t i;

    assert(global_map.length <= global_map.capacity);
    assert(global_map.entries != NULL || global_map.capacity == 0);

    for (i = 0; i < global_map.length; i++) {
        entry = &global_map.entries[i];

        assert(entry->name != NULL);
        assert(entry->fd >= 0);
    }
}
#endif

/// Register the given pre-opened file descriptor under the given path.
int
__wasilibc_register_preopened_fd(int fd, const char *path)
{
    po_map_assertvalid();

    assert(fd >= 0);
    assert(path != NULL);

    if (global_map.length == global_map.capacity) {
        int n = po_map_enlarge();

        po_map_assertvalid();

        if (n != 0) {
            return n;
        }
    }

    __wasi_fdstat_t statbuf;
    int r = __wasi_fd_fdstat_get((__wasi_fd_t)fd, &statbuf);
    if (r != 0) {
        errno = r;
        return -1; // TODO: Add an infallible way to get the rights?
    }

    const char *name = strdup(path);
    if (name == NULL) {
        return -1;
    }

    struct po_map_entry *entry = &global_map.entries[global_map.length++];

    entry->name = name;
    entry->fd = fd;
    entry->rights_base = statbuf.fs_rights_base;
    entry->rights_inheriting = statbuf.fs_rights_inheriting;

    po_map_assertvalid();

    return 0;
}

int
__wasilibc_find_relpath(
    const char *path,
    __wasi_rights_t rights_base,
    __wasi_rights_t rights_inheriting,
    const char **relative_path)
{
    size_t bestlen = 0;
    int best = -1;

    po_map_assertvalid();

    assert(path != NULL);
    assert(relative_path != NULL);

    bool any_matches = false;
    for (size_t i = 0; i < global_map.length; i++) {
        const struct po_map_entry *entry = &global_map.entries[i];
        const char *name = entry->name;
        size_t len = strlen(name);

        if (path[0] != '/' && (path[0] != '.' || (path[1] != '/' && path[1] != '\0'))) {
            // We're matching a relative path that doesn't start with "./" and isn't ".".
            if (len >= 2 && name[0] == '.' && name[1] == '/') {
                // The entry starts with "./", so skip that prefix.
                name += 2;
                len -= 2;
            } else if (len == 1 && name[0] == '.') {
                // The entry is ".", so match it as an empty string.
                name += 1;
                len -= 1;
            }
        }

        if ((any_matches && len <= bestlen) || !po_isprefix(name, len, path)) {
            continue;
        }

        if ((rights_base & ~entry->rights_base) != 0 ||
            (rights_inheriting & ~entry->rights_inheriting) != 0) {
            continue;
        }

        best = entry->fd;
        bestlen = len;
        any_matches = true;
    }

    const char *relpath = path + bestlen;

    while (*relpath == '/') {
        relpath++;
    }

    if (*relpath == '\0') {
        relpath = ".";
    }

    *relative_path = relpath;
    return best;
}
