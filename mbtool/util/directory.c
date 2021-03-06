/*
 * Copyright (C) 2014  Andrew Gunnerson <andrewgunnerson@gmail.com>
 *
 * This file is part of MultiBootPatcher
 *
 * MultiBootPatcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiBootPatcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiBootPatcher.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "util/directory.h"

#include <errno.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int mb_mkdir_recursive(const char *dir, mode_t mode)
{
    struct stat st;
    char *p;
    char *save_ptr;
    char *temp = NULL;
    char *copy = NULL;
    int len;

    if (!dir || strlen(dir) == 0) {
        errno = EINVAL;
        goto error;
    }

    copy = strdup(dir);
    len = strlen(dir);
    temp = malloc(len + 2);
    temp[0] = '\0';

    if (dir[0] == '/') {
        strcat(temp, "/");
    }

    p = strtok_r(copy, "/", &save_ptr);
    while (p != NULL) {
        strcat(temp, p);
        strcat(temp, "/");

        if (stat(temp, &st) < 0 && mkdir(temp, mode) < 0) {
            goto error;
        }

        p = strtok_r(NULL, "/", &save_ptr);
    }

    free(copy);
    free(temp);
    return 0;

error:
    free(copy);
    free(temp);
    return -1;
}

int mb_mkdir_parent(const char *path, mode_t perms)
{
    struct stat sb;
    char *dup = NULL;
    char *dir;

    if (!path) {
        errno = EINVAL;
        goto error;
    }

    dup = strdup(path);
    if (!dup) {
        errno = ENOMEM;
        goto error;
    }

    dir = dirname(dup);

    if (mb_mkdir_recursive(dir, perms) < 0 && errno != EEXIST) {
        goto error;
    }

    if (stat(dir, &sb) < 0) {
        goto error;
    }

    if (!S_ISDIR(sb.st_mode)) {
        errno = ENOTDIR;
        goto error;
    }

    free(dup);
    return 0;

error:
    free(dup);
    return -1;
}
