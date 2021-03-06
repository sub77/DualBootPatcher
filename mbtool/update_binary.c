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

#include "update_binary.h"

#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#include <archive.h>
#include <archive_entry.h>

#include <libmbp/cwrapper/cbootimage.h>
#include <libmbp/cwrapper/ccommon.h>
#include <libmbp/cwrapper/ccpiofile.h>
#include <libmbp/cwrapper/cdevice.h>
#include <libmbp/cwrapper/cpatcherconfig.h>

#include <loki.h>

#include "main.h"
#include "multiboot.h"
#include "roms.h"
#include "external/sha.h"
#include "util/archive.h"
#include "util/chown.h"
#include "util/command.h"
#include "util/copy.h"
#include "util/delete.h"
#include "util/directory.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/loopdev.h"
#include "util/mount.h"
#include "util/properties.h"
#include "util/selinux.h"
#include "util/string.h"


// Set to 1 to spawn a shell after installation
// NOTE: This should ONLY be used through adb. For example:
//
//     $ adb push mbtool_recovery /tmp/updater
//     $ adb shell /tmp/updater 3 1 /path/to/file_patched.zip
#define DEBUG_SHELL 0

// Use an update-binary file not from the zip file
#define DEBUG_USE_ALTERNATE_UPDATER 0
#define DEBUG_ALTERNATE_UPDATER_PATH "/tmp/updater.orig"


/* Lots of paths */

#define CHROOT          "/chroot"
#define MB_TEMP         "/multiboot"

#define HELPER_TOOL     "/update-binary-tool"

#define LOG_FILE        "/data/media/0/MultiBoot.log"

#define ZIP_UPDATER     "META-INF/com/google/android/update-binary"
#define ZIP_UNZIP       "multiboot/unzip"
#define ZIP_AROMA       "multiboot/aromawrapper.zip"
#define ZIP_BBWRAPPER   "multiboot/bb-wrapper.sh"
#define ZIP_DEVICE      "multiboot/device"


static const char *interface;
static const char *output_fd_str;
static const char *zip_file;

static int output_fd;


static void ui_print(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    char newfmt[100];
    snprintf(newfmt, 100, "ui_print [MultiBoot] %s\n", fmt);
    vdprintf(output_fd, newfmt, ap);
    dprintf(output_fd, "ui_print\n");

    va_end(ap);
}

/*
 * To work around denials (on Samsung devices):
 * 1. Mount the system and data partitions
 *
 *      $ mount /system
 *      $ mount /data
 *
 * 2. Start the audit daemon
 *
 *      $ /system/bin/auditd
 *
 * 3. From another window, run mbtool's update-binary wrapper
 *
 *      $ /tmp/mbtool_recovery updater 3 1 /path/to/file_patched.zip
 *
 * 4. Pull /data/misc/audit/audit.log and run it through audit2allow
 *
 *      $ adb pull /data/misc/audit/audit.log
 *      $ grep denied audit.log | audit2allow
 *
 * 5. Allow the rule using mb_selinux_add_rule()
 *
 *    Rules of the form:
 *      allow source target:class perm;
 *    Are allowed by calling:
 *      mb_selinux_add_rule(&pdb, source, target, class, perm);
 *
 * --
 *
 * To view the allow rules for the currently loaded policy:
 *
 * 1. Pull the current policy file
 *
 *      $ adb pull /sys/fs/selinux/policy
 *
 * 2. View the rules (the -s, -t, -c, and -p parameters can be used to filter
 *    the rules by source, target, class, and permission, respectively)
 *
 *      $ sesearch -A policy
 */
static int patch_sepolicy()
{
    policydb_t pdb;

    if (policydb_init(&pdb) < 0) {
        LOGE("Failed to initialize policydb");
        return -1;
    }

    if (mb_selinux_read_policy(MB_SELINUX_POLICY_FILE, &pdb) < 0) {
        LOGE("Failed to read SELinux policy file: %s", MB_SELINUX_POLICY_FILE);
        policydb_destroy(&pdb);
        return -1;
    }

    LOGD("Policy version: %u", pdb.policyvers);

    // Debugging rules (for CWM and Philz)
    mb_selinux_add_rule(&pdb, "adbd",  "block_device",    "blk_file",   "relabelto");
    mb_selinux_add_rule(&pdb, "adbd",  "graphics_device", "chr_file",   "relabelto");
    mb_selinux_add_rule(&pdb, "adbd",  "graphics_device", "dir",        "relabelto");
    mb_selinux_add_rule(&pdb, "adbd",  "input_device",    "chr_file",   "relabelto");
    mb_selinux_add_rule(&pdb, "adbd",  "input_device",    "dir",        "relabelto");
    mb_selinux_add_rule(&pdb, "adbd",  "rootfs",          "dir",        "relabelto");
    mb_selinux_add_rule(&pdb, "adbd",  "rootfs",          "file",       "relabelto");
    mb_selinux_add_rule(&pdb, "adbd",  "rootfs",          "lnk_file",   "relabelto");
    mb_selinux_add_rule(&pdb, "adbd",  "system_file",     "file",       "relabelto");
    mb_selinux_add_rule(&pdb, "adbd",  "tmpfs",           "file",       "relabelto");

    mb_selinux_add_rule(&pdb, "rootfs", "tmpfs",          "filesystem", "associate");
    mb_selinux_add_rule(&pdb, "tmpfs",  "rootfs",         "filesystem", "associate");

    if (mb_selinux_write_policy(MB_SELINUX_LOAD_FILE, &pdb) < 0) {
        LOGE("Failed to write SELinux policy file: %s", MB_SELINUX_LOAD_FILE);
        policydb_destroy(&pdb);
        return -1;
    }

    policydb_destroy(&pdb);

    return 0;
}

#define MKDIR_CHECKED(a, b) \
    if (mkdir(a, b) < 0) { \
        LOGE("Failed to create %s: %s", a, strerror(errno)); \
        goto error; \
    }
#define MOUNT_CHECKED(a, b, c, d, e) \
    if (mount(a, b, c, d, e) < 0) { \
        LOGE("Failed to mount %s (%s) at %s: %s", a, c, b, strerror(errno)); \
        goto error; \
    }
#define MKNOD_CHECKED(a, b, c) \
    if (mknod(a, b, c) < 0) { \
        LOGE("Failed to create special file %s: %s", a, strerror(errno)); \
        goto error; \
    }
#define IS_MOUNTED_CHECKED(a) \
    if (!mb_is_mounted(a)) { \
        LOGE("%s is not mounted", a); \
        goto error; \
    }
#define UNMOUNT_ALL_CHECKED(a) \
    if (mb_unmount_all(a) < 0) { \
        LOGE("Failed to unmount all mountpoints within %s", a); \
        goto error; \
    }
#define RECURSIVE_DELETE_CHECKED(a) \
    if (mb_delete_recursive(a) < 0) { \
        LOGE("Failed to recursively remove %s", a); \
        goto error; \
    }
#define COPY_DIR_CONTENTS_CHECKED(a, b, c) \
    if (mb_copy_dir(a, b, c) < 0) { \
        LOGE("Failed to copy contents of %s/ to %s/", a, b); \
        goto error; \
    }

static int create_chroot(void)
{
    // We'll just call the recovery's mount tools directly to avoid having to
    // parse TWRP and CWM's different fstab formats
    char *mount_system[] = { "mount", "/system", NULL };
    char *mount_cache[] = { "mount", "/cache", NULL };
    char *mount_data[] = { "mount", "/data", NULL };
    mb_run_command(mount_system);
    mb_run_command(mount_cache);
    mb_run_command(mount_data);

    // Make sure everything really is mounted
    IS_MOUNTED_CHECKED("/system");
    IS_MOUNTED_CHECKED("/cache");
    IS_MOUNTED_CHECKED("/data");

    // Unmount everything previously mounted in /chroot
    UNMOUNT_ALL_CHECKED(CHROOT);

    // Remove /chroot if it exists
    RECURSIVE_DELETE_CHECKED(CHROOT);

    // Setup directories
    MKDIR_CHECKED(CHROOT, 0755);
    MKDIR_CHECKED(CHROOT MB_TEMP, 0755);
    MKDIR_CHECKED(CHROOT "/dev", 0755);
    MKDIR_CHECKED(CHROOT "/etc", 0755);
    MKDIR_CHECKED(CHROOT "/proc", 0755);
    MKDIR_CHECKED(CHROOT "/sbin", 0755);
    MKDIR_CHECKED(CHROOT "/sys", 0755);
    MKDIR_CHECKED(CHROOT "/tmp", 0755);

    MKDIR_CHECKED(CHROOT "/data", 0755);
    MKDIR_CHECKED(CHROOT "/cache", 0755);
    MKDIR_CHECKED(CHROOT "/system", 0755);

    // Other mounts
    MOUNT_CHECKED("none", CHROOT "/dev",            "tmpfs",     0, "");
    MKDIR_CHECKED(CHROOT "/dev/pts", 0755);
    MOUNT_CHECKED("none", CHROOT "/dev/pts",        "devpts",    0, "");
    MOUNT_CHECKED("none", CHROOT "/proc",           "proc",      0, "");
    MOUNT_CHECKED("none", CHROOT "/sys",            "sysfs",     0, "");

    // Some recoveries don't have SELinux enabled
    if (mount(    "none", CHROOT "/sys/fs/selinux", "selinuxfs", 0, "") < 0
            && errno != ENOENT) {
        LOGE("Failed to mount %s (%s) at %s: %s",
             "none", "selinuxfs", CHROOT "/sys/fs/selinux", strerror(errno));
        goto error;
    }

    MOUNT_CHECKED("none", CHROOT "/tmp",            "tmpfs",     0, "");
    // Copy the contents of sbin since we need to mess with some of the binaries
    // there. Also, for whatever reason, bind mounting /sbin results in EINVAL
    // no matter if it's done from here or from busybox.
    MOUNT_CHECKED("none", CHROOT "/sbin",           "tmpfs",     0, "");
    COPY_DIR_CONTENTS_CHECKED("/sbin", CHROOT "/sbin",
                              MB_COPY_ATTRIBUTES
                              | MB_COPY_XATTRS
                              | MB_COPY_EXCLUDE_TOP_LEVEL);

    // Don't create unnecessary special files in /dev to avoid install scripts
    // from overwriting partitions
    MKNOD_CHECKED(CHROOT "/dev/console",      S_IFCHR | 0644, makedev(5, 1));
    MKNOD_CHECKED(CHROOT "/dev/null",         S_IFCHR | 0644, makedev(1, 3));
    MKNOD_CHECKED(CHROOT "/dev/ptmx",         S_IFCHR | 0644, makedev(5, 2));
    MKNOD_CHECKED(CHROOT "/dev/random",       S_IFCHR | 0644, makedev(1, 8));
    MKNOD_CHECKED(CHROOT "/dev/tty",          S_IFCHR | 0644, makedev(5, 0));
    MKNOD_CHECKED(CHROOT "/dev/urandom",      S_IFCHR | 0644, makedev(1, 9));
    MKNOD_CHECKED(CHROOT "/dev/zero",         S_IFCHR | 0644, makedev(1, 5));
    MKNOD_CHECKED(CHROOT "/dev/loop-control", S_IFCHR | 0644, makedev(10, 237));

    // Create a few loopback devices in case we need to use them
    MKDIR_CHECKED(CHROOT "/dev/block", 0755);
    MKNOD_CHECKED(CHROOT "/dev/block/loop0",  S_IFBLK | 0644, makedev(7, 0));
    MKNOD_CHECKED(CHROOT "/dev/block/loop1",  S_IFBLK | 0644, makedev(7, 1));
    MKNOD_CHECKED(CHROOT "/dev/block/loop2",  S_IFBLK | 0644, makedev(7, 2));

    // We need /dev/input/* and /dev/graphics/* for AROMA
#if 1
    COPY_DIR_CONTENTS_CHECKED("/dev/input", CHROOT "/dev/input",
                              MB_COPY_ATTRIBUTES
                              | MB_COPY_XATTRS
                              | MB_COPY_EXCLUDE_TOP_LEVEL);
    COPY_DIR_CONTENTS_CHECKED("/dev/graphics", CHROOT "/dev/graphics",
                              MB_COPY_ATTRIBUTES
                              | MB_COPY_XATTRS
                              | MB_COPY_EXCLUDE_TOP_LEVEL);
#else
    MKDIR_CHECKED(CHROOT "/dev/input", 0755);
    MKDIR_CHECKED(CHROOT "/dev/graphics", 0755);
    MOUNT_CHECKED("/dev/input", CHROOT "/dev/input", "", MS_BIND, "");
    MOUNT_CHECKED("/dev/graphics", CHROOT "/dev/graphics", "", MS_BIND, "");
#endif

    mb_create_empty_file(CHROOT "/.chroot");

    return 0;

error:
    return -1;
}

static int destroy_chroot(void)
{
    umount(CHROOT "/system");
    umount(CHROOT "/cache");
    umount(CHROOT "/data");

    umount(CHROOT MB_TEMP "/system.img");

    umount(CHROOT "/dev/pts");
    umount(CHROOT "/dev");
    umount(CHROOT "/proc");
    umount(CHROOT "/sys/fs/selinux");
    umount(CHROOT "/sys");
    umount(CHROOT "/tmp");
    umount(CHROOT "/sbin");

    // Unmount everything previously mounted in /chroot
    if (mb_unmount_all(CHROOT) < 0) {
        LOGE("Failed to unmount previous mount points in " CHROOT);
        return -1;
    }

    mb_delete_recursive(CHROOT);

    return 0;
}

/*!
 * \brief Extract needed multiboot files from the patched zip file
 *
 * \return 0 on success, -1 on failure
 */
static int extract_zip_files(void)
{
    static const struct extract_info files[] = {
        { ZIP_UPDATER ".orig",  MB_TEMP "/updater"          },
        { ZIP_UNZIP,            MB_TEMP "/unzip"            },
        { ZIP_AROMA,            MB_TEMP "/aromawrapper.zip" },
        { ZIP_BBWRAPPER,        MB_TEMP "/bb-wrapper.sh"    },
        { ZIP_DEVICE,           MB_TEMP "/device"           },
        { NULL, NULL }
    };

    if (mb_extract_files2(zip_file, files) < 0) {
        LOGE("Failed to extract all multiboot files");
        return -1;
    }

    return 0;
}

/*!
 * \brief Get the target device of the patched zip file
 *
 * \return Dynamically allocated string containing the device codename or NULL
 *         if the device could not be determined
 */
static char * get_target_device(void)
{
    char *device = NULL;
    if (mb_file_first_line(MB_TEMP "/device", &device) < 0) {
        LOGE("Failed to read " MB_TEMP "/device");
        return NULL;
    }

    return device;
}

/*!
 * \brief Replace /sbin/unzip in the chroot with one supporting zip flags 1 & 8
 *
 * \return 0 on success, -1 on failure
 */
static int setup_unzip(void)
{
    remove(CHROOT "/sbin/unzip");

    if (mb_copy_file(MB_TEMP "/unzip",
                     CHROOT "/sbin/unzip",
                     MB_COPY_ATTRIBUTES | MB_COPY_XATTRS) < 0) {
        LOGE("Failed to copy %s to %s: %s",
             MB_TEMP "/unzip", CHROOT "/sbin/unzip", strerror(errno));
        return -1;
    }

    if (chmod(CHROOT "/sbin/unzip", 0555) < 0) {
        LOGE("Failed to chmod %s: %s",
             CHROOT "/sbin/unzip", strerror(errno));
        return -1;
    }

    return 0;
}

/*!
 * \brief Replace busybox in the chroot with a wrapper that disables certain
 *        functions
 *
 * \return 0 on success, -1 on failure
 */
static int setup_busybox_wrapper(void)
{
    rename(CHROOT "/sbin/busybox", CHROOT "/sbin/busybox_orig");

    if (mb_copy_file(MB_TEMP "/bb-wrapper.sh",
                     CHROOT "/sbin/busybox",
                     MB_COPY_ATTRIBUTES | MB_COPY_XATTRS) < 0) {
        LOGE("Failed to copy %s to %s: %s",
             MB_TEMP "/bb-wrapper.sh", CHROOT "/sbin/busybox", strerror(errno));
        return -1;
    }

    if (chmod(CHROOT "/sbin/busybox", 0555) < 0) {
        LOGE("Failed to chmod %s: %s", CHROOT "/sbin/busybox", strerror(errno));
        return -1;
    }

    return 0;
}

/*!
 * \brief Create 3G temporary ext4 image
 *
 * \param path Image file path
 *
 * \return 0 on success, -1 on failure
 */
static int create_temporary_image(const char *path)
{
#define IMAGE_SIZE "3G"

    if (mb_mkdir_parent(path, S_IRWXU) < 0) {
        LOGE("%s: Failed to create parent directory: %s",
             path, strerror(errno));
        return -1;
    }

    struct stat sb;
    if (stat(path, &sb) < 0) {
        if (errno != ENOENT) {
            LOGE("%s: Failed to stat: %s", path, strerror(errno));
            return -1;
        } else {
            LOGD("%s: Creating new %s ext4 image", path, IMAGE_SIZE);

            // Create new image
            const char *argv[] = { "make_ext4fs", "-l", IMAGE_SIZE, path, NULL };
            if (mb_run_command((char **) argv) != 0) {
                LOGE("%s: Failed to create image", path);
                return -1;
            }
            return 0;
        }
    }

    LOGE("%s: File already exists", path);
    return -1;
}

/*!
 * \brief Compute SHA1 hash of a file
 *
 * \param path Path to file
 * \param digest `unsigned char` array of size `SHA_DIGEST_SIZE` to store
 *               computed hash value
 *
 * \return 0 on success, -1 on failure and errno set appropriately
 */
static int sha1_hash(const char *path, unsigned char digest[SHA_DIGEST_SIZE])
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOGE("%s: Failed to open: %s", path, strerror(errno));
        return -1;
    }

    unsigned char buf[10240];
    size_t n;

    SHA_CTX ctx;
    SHA_init(&ctx);

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        SHA_update(&ctx, buf, n);
        if (n < sizeof(buf)) {
            break;
        }
    }

    if (ferror(fp)) {
        LOGE("%s: Failed to read file", path);
        fclose(fp);
        errno = EIO;
        return -1;
    }

    fclose(fp);

    memcpy(digest, SHA_final(&ctx), SHA_DIGEST_SIZE);

    return 0;
}

/*!
 * \brief Convert binary data to its hex string representation
 *
 * The size of the output string should be at least `2 * size + 1` bytes.
 * (Two characters in string for each byte in the binary data + one byte for
 * the NULL terminator.)
 *
 * \param data Binary data
 * \param size Size of binary data
 * \param out Output string
 */
static void to_hex_string(unsigned char *data, size_t size, char *out)
{
    static const char digits[] = "0123456789abcdef";

    for (unsigned int i = 0; i < size; ++i) {
        out[2 * i] = digits[(data[i] >> 4) & 0xf];
        out[2 * i + 1] = digits[data[i] & 0xf];
    }

    out[2 * size] = '\0';
}

/*!
 * \brief Guess if an installer file in an AROMA installer
 *
 * \return 1 if file in an AROMA installer, 0 if the file is not, -1 on error
 *         (and errno set appropriately)
 */
static int is_aroma(const char *path)
{
    // Possible strings we can search for
    static const char *AROMA_MAGIC[] = {
        "AROMA Installer",
        "support@amarullz.com",
        "(c) 2013 by amarullz xda-developers",
        "META-INF/com/google/android/aroma-config",
        "META-INF/com/google/android/aroma",
        "AROMA_NAME",
        "AROMA_BUILD",
        "AROMA_VERSION",
        NULL
    };

    struct stat sb;
    void *map = MAP_FAILED;
    int fd = -1;
    const char **magic;
    int found = 0;
    int saved_errno;

    if ((fd = open(path, O_RDONLY)) < 0) {
        goto error;
    }

    if (fstat(fd, &sb) < 0) {
        goto error;
    }

    map = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        goto error;
    }

    for (magic = AROMA_MAGIC; *magic; ++magic) {
        if (memmem(map, sb.st_size, *magic, strlen(*magic))) {
            found = 1;
            break;
        }
    }

    if (munmap(map, sb.st_size) < 0) {
        map = MAP_FAILED;
        goto error;
    }

    if (close(fd) < 0) {
        fd = -1;
        goto error;
    }

    return found;

error:
    saved_errno = errno;

    if (map != MAP_FAILED) {
        munmap(map, sb.st_size);
    }

    if (fd >= 0) {
        close(fd);
    }

    errno = saved_errno;

    return -1;
}

/*!
 * \brief Run AROMA wrapper for ROM installation selection
 *
 * \return 0 on success, -1 on failure
 */
static int run_aroma_selection(void)
{
    struct extract_info aroma_files[] = {
        { ZIP_UPDATER, CHROOT MB_TEMP "/updater" },
        { NULL, NULL }
    };

    if (mb_extract_files2(MB_TEMP "/aromawrapper.zip", aroma_files) < 0) {
        LOGE("Failed to extract %s", ZIP_UPDATER);
        return -1;
    }

    if (chmod(CHROOT MB_TEMP "/updater", 0555) < 0) {
        LOGE("%s: Failed to chmod: %s",
             CHROOT MB_TEMP "/updater", strerror(errno));
        return -1;
    }

    if (mb_copy_file(MB_TEMP "/aromawrapper.zip",
                     CHROOT MB_TEMP "/aromawrapper.zip",
                     MB_COPY_ATTRIBUTES | MB_COPY_XATTRS) < 0) {
        LOGE("Failed to copy %s to %s: %s",
             MB_TEMP "/aromawrapper.zip", CHROOT MB_TEMP "/aromawrapper.zip",
             strerror(errno));
        return -1;
    }

    const char *argv[] = {
        MB_TEMP "/updater",
        interface,
        // Force output to stderr
        //output_fd_str,
        "2",
        MB_TEMP "/aromawrapper.zip",
        NULL
    };

    // Stop parent process (/sbin/recovery), so AROMA doesn't fight over the
    // framebuffer. AROMA normally already does this, but when we wrap it, it
    // just stops the wraapper.
    pid_t parent = getppid();

    kill(parent, SIGSTOP);

    int ret;
    if ((ret = mb_run_command_chroot(CHROOT, (char **) argv)) != 0) {
        kill(parent, SIGCONT);

        if (ret < 0) {
            LOGE("Failed to execute %s: %s",
                 MB_TEMP "/updater", strerror(errno));
        } else {
            LOGE("%s returned non-zero exit status",
                 MB_TEMP "/updater");
        }
        return -1;
    }

    kill(parent, SIGCONT);

    return 0;
}

/*!
 * \brief Run real update-binary in the chroot
 *
 * \return 0 on success, -1 on failure
 */
static int run_real_updater(void)
{
#if DEBUG_USE_ALTERNATE_UPDATER
#define UPDATER DEBUG_ALTERNATE_UPDATER_PATH
#else
#define UPDATER MB_TEMP "/updater"
#endif

    if (mb_copy_file(UPDATER, CHROOT MB_TEMP "/updater",
                     MB_COPY_ATTRIBUTES | MB_COPY_XATTRS) < 0) {
        LOGE("Failed to copy %s to %s: %s",
             UPDATER, CHROOT MB_TEMP "/updater", strerror(errno));
        return -1;
    }

    if (chmod(CHROOT MB_TEMP "/updater", 0555) < 0) {
        LOGE("%s: Failed to chmod: %s",
             CHROOT MB_TEMP "/updater", strerror(errno));
        return -1;
    }

    const char *argv[] = {
        MB_TEMP "/updater",
        interface,
        output_fd_str,
        MB_TEMP "/install.zip",
        NULL
    };

    pid_t parent = getppid();
    int aroma = is_aroma(CHROOT MB_TEMP "/updater") > 0;

    LOGD("update-binary is AROMA: %d", aroma);

    if (aroma) {
        kill(parent, SIGSTOP);
    }

    int ret;
    if ((ret = mb_run_command_chroot(CHROOT, (char **) argv)) != 0) {
        if (aroma) {
            kill(parent, SIGCONT);
        }

        if (ret < 0) {
            LOGE("Failed to execute %s: %s",
                 MB_TEMP "/updater", strerror(errno));
        } else {
            LOGE("%s returned non-zero exit status",
                 MB_TEMP "/updater");
        }
        return -1;
    }

    if (aroma) {
        kill(parent, SIGCONT);
    }

    return 0;
}

/*!
 * \brief Copy /system directory excluding multiboot files
 *
 * \param source Source directory
 * \param target Target directory
 *
 * \return 0 on success, -1 on error
 */
static int copy_system(const char *source, const char *target)
{
    int ret = 0;
    FTS *ftsp = NULL;
    FTSENT *curr;

    char *files[] = { (char *) source, NULL };

    ftsp = fts_open(files, FTS_NOCHDIR | FTS_PHYSICAL | FTS_XDEV, NULL);
    if (!ftsp) {
        LOGE("%s: fts_open failed: %s", source, strerror(errno));
        ret = -1;
        goto finish;
    }

    size_t pathsize;
    char *pathbuf = NULL;

    while ((curr = fts_read(ftsp))) {
        switch (curr->fts_info) {
        case FTS_NS:
        case FTS_DNR:
        case FTS_ERR:
            LOGW("%s: fts_read error: %s",
                 curr->fts_accpath, strerror(curr->fts_errno));
            continue;

        case FTS_DC:
        case FTS_DOT:
        case FTS_NSOK:
            // Not reached
            continue;
        }

        if (curr->fts_level != 1) {
            continue;
        }

        if (strcmp(curr->fts_name, "multiboot") == 0) {
            fts_set(ftsp, curr, FTS_SKIP);
            continue;
        }

        pathsize = strlen(target) + 1 + strlen(curr->fts_name) + 1;

        char *temp_pathbuf;
        if ((temp_pathbuf = realloc(pathbuf, pathsize))) {
            pathbuf = temp_pathbuf;
        } else {
            ret = -1;
            errno = ENOMEM;
            goto finish;
        }
        pathbuf[0] = '\0';

        strlcat(pathbuf, target, pathsize);
        strlcat(pathbuf, "/", pathsize);
        strlcat(pathbuf, curr->fts_name, pathsize);

        switch (curr->fts_info) {
        case FTS_D:
            // target is the correct parameter here (or pathbuf and
            // MB_COPY_EXCLUDE_TOP_LEVEL flag)
            if (mb_copy_dir(curr->fts_accpath, target,
                            MB_COPY_ATTRIBUTES | MB_COPY_XATTRS) < 0) {
                LOGW("%s: Failed to copy directory: %s",
                     curr->fts_path, strerror(errno));
                ret = -1;
            }
            fts_set(ftsp, curr, FTS_SKIP);
            continue;

        case FTS_DP:
            continue;

        case FTS_F:
        case FTS_SL:
        case FTS_SLNONE:
        case FTS_DEFAULT:
            if (mb_copy_file(curr->fts_accpath, pathbuf,
                             MB_COPY_ATTRIBUTES | MB_COPY_XATTRS) < 0) {
                LOGW("%s: Failed to copy file: %s",
                     curr->fts_path, strerror(errno));
                ret = -1;
            }
            continue;
        }
    }

finish:
    if (ftsp) {
        fts_close(ftsp);
    }

    return ret;
}

/*!
 * \brief Copy a /system directory to an image file
 *
 * \param source Source directory
 * \param image Target image file
 * \param reverse If non-zero, then the image file is the source and the
 *                directory is the target
 *
 * \return 0 on success, -1 on error
 */
static int system_image_copy(const char *source, const char *image, int reverse)
{
    struct stat sb;
    char *loopdev = NULL;

    if (stat(source, &sb) < 0
            && mb_mkdir_recursive(source, 0755) < 0) {
        LOGE("Failed to create %s: %s",
             source, strerror(errno));
        goto error;
    }

    if (stat(MB_TEMP "/.system.tmp", &sb) < 0
            && mkdir(MB_TEMP "/.system.tmp", 0755) < 0) {
        LOGE("Failed to create %s: %s",
             MB_TEMP "/.system.tmp", strerror(errno));
        goto error;
    }

    if (!(loopdev = mb_loopdev_find_unused())) {
        LOGE("Failed to find unused loop device: %s", strerror(errno));
        goto error;
    }

    if (mb_loopdev_setup_device(loopdev, image, 0, 0) < 0) {
        LOGE("Failed to setup loop device %s: %s", loopdev, strerror(errno));
        goto error;
    }

    if (mount(loopdev, MB_TEMP "/.system.tmp", "ext4", 0, "") < 0) {
        LOGE("Failed to mount %s: %s", loopdev, strerror(errno));
        goto error;
    }

    if (reverse) {
        if (copy_system(MB_TEMP "/.system.tmp", source) < 0) {
            LOGE("Failed to copy system files from %s to %s",
                 MB_TEMP "/.system.tmp", source);
            goto error;
        }
    } else {
        if (copy_system(source, MB_TEMP "/.system.tmp") < 0) {
            LOGE("Failed to copy system files from %s to %s",
                 source, MB_TEMP "/.system.tmp");
            goto error;
        }
    }

    if (umount(MB_TEMP "/.system.tmp") < 0) {
        LOGE("Failed to unmount %s: %s",
             MB_TEMP "/.system.tmp", strerror(errno));
        goto error;
    }

    if (mb_loopdev_remove_device(loopdev) < 0) {
        LOGE("Failed to remove loop device %s: %s", loopdev, strerror(errno));
        goto error;
    }

    free(loopdev);

    return 0;

error:
    if (loopdev) {
        umount(MB_TEMP "/.system.tmp");
        mb_loopdev_remove_device(loopdev);
        free(loopdev);
    }
    return -1;
}

static int copy_blockdev(const char *source, const char *target)
{
    int fd_source = -1;
    int fd_target = -1;

    if ((fd_source = open(source, O_RDONLY)) < 0) {
        goto error;
    }

    if ((fd_target = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
        goto error;
    }

    if (mb_copy_data_fd(fd_source, fd_target) < 0) {
        goto error;
    }

    close(fd_source);
    close(fd_target);

    return 0;

error:
    close(fd_source);
    close(fd_target);

    return 0;
}

/*!
 * \brief Main wrapper function
 *
 * \return 0 on success, -1 on error
 */
static int update_binary(void)
{
    int ret = 0;

    struct roms r;
    mb_roms_init(&r);
    mb_roms_get_builtin(&r);

    CPatcherConfig * pc = NULL;

    char *device = NULL;
    char *boot_block_dev = NULL;


    struct stat sb;
    if (stat("/sys/fs/selinux", &sb) == 0) {
        if (patch_sepolicy() < 0) {
            LOGE("Failed to patch sepolicy");
            int fd = open(MB_SELINUX_ENFORCE_FILE, O_WRONLY);
            if (fd >= 0) {
                write(fd, "0", 1);
                close(fd);
            } else {
                LOGE("Failed to set SELinux to permissive mode");
                ui_print("Could not patch or disable SELinux");
            }
        }
    }

    ui_print("Creating chroot environment");

    if (create_chroot() < 0) {
        ui_print("Failed to create chroot environment");
        goto error;
    }


    RECURSIVE_DELETE_CHECKED(MB_TEMP);
    MKDIR_CHECKED(MB_TEMP, 0755);

    if (extract_zip_files() < 0) {
        ui_print("Failed to extract multiboot files from zip");
        goto error;
    }


    // Verify device
    device = get_target_device();
    char prop1[MB_PROP_VALUE_MAX] = { 0 };
    char prop2[MB_PROP_VALUE_MAX] = { 0 };
    mb_get_property("ro.product.device", prop1, "");
    mb_get_property("ro.build.product", prop2, "");

    if (!device) {
        ui_print("Failed to determine target device");
        goto error;
    }

    LOGD("ro.product.device = %s", prop1);
    LOGD("ro.build.product = %s", prop2);
    LOGD("Target device = %s", device);


    pc = mbp_config_create();

    char *version = mbp_config_version(pc);
    if (version) {
        LOGD("libmbp-mini version: %s", version);
        mbp_free(version);
    }

    // Due to optimizations in libc, strlen() may trigger valgrind errors like
    //     Address 0x4c0bf04 is 4 bytes inside a block of size 6 alloc'd
    // It's an annoyance, but not a big deal
    int device_error = 0;

    CDevice **device_list = mbp_config_devices(pc);
    for (CDevice **cdev_iter = device_list; *cdev_iter; ++cdev_iter) {
        // My C wrapper really needs some work...

        if (strcmp(device, mbp_device_id(*cdev_iter)) != 0) {
            // Haven't found device
            continue;
        }

        int matches = 0;

        // Verify codename
        char **codenames = mbp_device_codenames(*cdev_iter);
        for (char **name_iter = codenames; *name_iter; ++name_iter) {
            if (strstr(prop1, *name_iter) || strstr(prop2, *name_iter)) {
                matches = 1;
                break;
            }
        }

        if (!matches) {
            ui_print("Patched zip is for:");
            for (char **name_iter = codenames; *name_iter; ++name_iter) {
                ui_print("- %s", *name_iter);
            }
            ui_print("This device is '%s'", prop1);

            device_error = 1;
        }

        mbp_free_array((void **) codenames);

        if (device_error) {
            break;
        }

        // Copy boot partition block devices to the chroot
        char **boot_devs = mbp_device_boot_block_devs(*cdev_iter);
        if (*boot_devs) {
            boot_block_dev = strdup(*boot_devs);
        }

        // Copy any other required block devices to the chroot
        char **extra_devs = mbp_device_extra_block_devs(*cdev_iter);

        char **dev_lists[] = {
            boot_devs,
            extra_devs,
            NULL
        };

        for (char ***dev_list = dev_lists; *dev_list; ++dev_list) {
            for (char **blk_iter = *dev_list; *blk_iter; ++blk_iter) {
                size_t dev_path_size = strlen(*blk_iter) + strlen(CHROOT) + 2;
                char *dev_path = malloc(dev_path_size);
                dev_path[0] = '\0';
                strlcat(dev_path, CHROOT, dev_path_size);
                strlcat(dev_path, "/", dev_path_size);
                strlcat(dev_path, *blk_iter, dev_path_size);

                if (mb_mkdir_parent(dev_path, 0755) < 0) {
                    LOGE("Failed to create parent directory of %s", dev_path);
                }

                // Follow symlinks just in case the symlink source isn't in the list
                if (mb_copy_file(*blk_iter, dev_path, MB_COPY_ATTRIBUTES
                                                     | MB_COPY_XATTRS
                                                     | MB_COPY_FOLLOW_SYMLINKS) < 0) {
                    LOGE("Failed to copy %s. Continuing anyway", *blk_iter);
                }

                LOGD("Copied %s to the chroot", dev_path);

                free(dev_path);
            }
        }

        mbp_free_array((void **) boot_devs);
        mbp_free_array((void **) extra_devs);


        break;
    }

    mbp_free(device_list);

    if (device_error) {
        goto error;
    }

    if (!boot_block_dev) {
        ui_print("Could not determine the boot block device");
        goto error;
    }

    LOGD("Boot block device: %s", boot_block_dev);


    // Choose install type
    if (run_aroma_selection() < 0) {
        ui_print("Failed to run AROMA");
        goto error;
    }

    char *install_type;
    if (mb_file_first_line(CHROOT MB_TEMP "/installtype", &install_type) < 0) {
        ui_print("Failed to determine install type");
        goto error;
    }

    if (strcmp(install_type, "cancelled") == 0) {
        ui_print("Cancelled installation");
        free(install_type);
        goto success;
    }

    struct rom *rom = mb_find_rom_by_id(&r, install_type);
    if (!rom) {
        ui_print("Unknown ROM ID: %s", install_type);
        free(install_type);
        goto error;
    }

    free(install_type);

    ui_print("ROM ID: %s", rom->id);
    ui_print("- /system: %s", rom->system_path);
    ui_print("- /cache: %s", rom->cache_path);
    ui_print("- /data: %s", rom->data_path);


    // Calculate SHA1 hash of the boot partition
    unsigned char hash[SHA_DIGEST_SIZE];
    if (sha1_hash(boot_block_dev, hash) < 0) {
        ui_print("Failed to compute sha1sum of boot partition");
        goto error;
    }

    char digest[2 * SHA_DIGEST_SIZE + 1];
    to_hex_string(hash, SHA_DIGEST_SIZE, digest);
    LOGD("Boot partition SHA1sum: %s", digest);

    // Save a copy of the boot image that we'll restore if the installation fails
    if (copy_blockdev(boot_block_dev, MB_TEMP "/boot.orig") < 0) {
        ui_print("Failed to backup boot partition");
        goto error;
    }


    // Extract busybox's unzip tool with support for zip file data descriptors
    if (setup_unzip() < 0) {
        ui_print("Failed to extract unzip tool");
        goto error;
    }


    // Mount target filesystems
    if (mb_bind_mount(rom->cache_path, 0771, CHROOT "/cache", 0771) < 0) {
        ui_print("Failed to bind mount %s to %s",
                 rom->cache_path, CHROOT "/cache");
        goto error;
    }

    if (mb_bind_mount(rom->data_path, 0771, CHROOT "/data", 0771) < 0) {
        ui_print("Failed to bind mount %s to %s",
                 rom->data_path, CHROOT "/data");
        goto error;
    }

    // Create a temporary image if the zip file has a system.transfer.list file
    struct exists_info info[] = {
        { "system.transfer.list", 0 },
        { NULL, 0}
    };
    if (mb_archive_exists(zip_file, info) < 0) {
        ui_print("Failed to read zip file");
        goto error;
    }

    if (!info[0].exists && strcmp(rom->id, "primary") != 0) {
        if (mb_bind_mount(rom->system_path, 0771, CHROOT "/system", 0771) < 0) {
            ui_print("Failed to bind mount %s to %s",
                     rom->system_path, CHROOT "/system");
            goto error;
        }
    } else {
        ui_print("Copying system to temporary image");

        // Create temporary image in /data
        if (create_temporary_image("/data/.system.img.tmp") < 0) {
            ui_print("Failed to create temporary image %s",
                     "/data/.system.img.tmp");
            goto error;
        }

        // Copy current /system files to the image
        if (system_image_copy(rom->system_path, "/data/.system.img.tmp", 0) < 0) {
            ui_print("Failed to copy %s to %s",
                     rom->system_path, "/data/.system.img.tmp");
            goto error;
        }

        // Install to the image
        mb_create_empty_file(CHROOT MB_TEMP "/system.img");
        MOUNT_CHECKED("/data/.system.img.tmp", CHROOT MB_TEMP "/system.img",
                      "", MS_BIND, "");
    }


    // Bind-mount zip file
    mb_create_empty_file(CHROOT MB_TEMP "/install.zip");
    MOUNT_CHECKED(zip_file, CHROOT MB_TEMP "/install.zip", "", MS_BIND, "");


    // Wrap busybox to disable some applets
    setup_busybox_wrapper();


    // Copy ourself for the real update-binary to use
    mb_copy_file(mb_self_get_path(), CHROOT HELPER_TOOL,
                 MB_COPY_ATTRIBUTES | MB_COPY_XATTRS);
    chmod(CHROOT HELPER_TOOL, 0555);


    // Copy /default.prop
    mb_copy_file("/default.prop", CHROOT "/default.prop",
                 MB_COPY_ATTRIBUTES | MB_COPY_XATTRS);


    // Copy file_contexts
    mb_copy_file("/file_contexts", CHROOT "/file_contexts",
                 MB_COPY_ATTRIBUTES | MB_COPY_XATTRS);


    // Run real update-binary
    ui_print("Running real update-binary");
    ui_print("Here we go!");

    int updater_ret;
    if ((updater_ret = run_real_updater()) < 0) {
        ui_print("Failed to run real update-binary");
    }

#if DEBUG_SHELL
    {
        const char *argv[] = { "/sbin/sh", "-i", NULL };
        mb_run_command_chroot(CHROOT, (char **) argv);
    }
#endif


    // Umount filesystems from inside the chroot
    const char *umount_system[] = { HELPER_TOOL, "unmount", "/system", NULL };
    const char *umount_cache[] = { HELPER_TOOL, "unmount", "/cache", NULL };
    const char *umount_data[] = { HELPER_TOOL, "unmount", "/data", NULL };
    mb_run_command_chroot(CHROOT, (char **) umount_system);
    mb_run_command_chroot(CHROOT, (char **) umount_cache);
    mb_run_command_chroot(CHROOT, (char **) umount_data);


    if (info[0].exists || strcmp(rom->id, "primary") == 0) {
        ui_print("Copying temporary image to system");

        // Format system directory
        if (mb_wipe_directory(rom->system_path, 1) < 0) {
            ui_print("Failed to wipe %s", rom->system_path);
            goto error;
        }

        // Copy image back to system directory
        if (system_image_copy(rom->system_path,
                              "/data/.system.img.tmp", 1) < 0) {
            ui_print("Failed to copy %s to %s",
                     "/data/.system.img.tmp", rom->system_path);
            goto error;
        }
    }


    if (updater_ret < 0) {
        goto error;
    }


    // Calculate SHA1 hash of the boot partition after installation
    unsigned char new_hash[SHA_DIGEST_SIZE];
    if (sha1_hash(boot_block_dev, new_hash) < 0) {
        ui_print("Failed to compute sha1sum of boot partition");
        goto error;
    }

    char new_digest[2 * SHA_DIGEST_SIZE + 1];
    to_hex_string(new_hash, SHA_DIGEST_SIZE, new_digest);
    LOGD("Old boot partition SHA1sum: %s", digest);
    LOGD("New boot partition SHA1sum: %s", new_digest);


    // Set kernel if it was changed
    if (memcmp(hash, new_hash, SHA_DIGEST_SIZE) != 0) {
        ui_print("Boot partition was modified. Setting kernel");

        // Add /romid with the ROM ID to the ramdisk

        CBootImage *bi = mbp_bootimage_create();
        if (mbp_bootimage_load_file(bi, boot_block_dev) < 0) {
            ui_print("Failed to load boot partition image");
            mbp_bootimage_destroy(bi);
            goto error;
        }

        void *ramdisk_data;
        size_t ramdisk_size;
        ramdisk_size = mbp_bootimage_ramdisk_image(bi, &ramdisk_data);

        CCpioFile *cpio = mbp_cpiofile_create();
        if (mbp_cpiofile_load_data(cpio, ramdisk_data, ramdisk_size) < 0) {
            ui_print("Failed to load ramdisk");
            mbp_free(ramdisk_data);
            mbp_cpiofile_destroy(cpio);
            mbp_bootimage_destroy(bi);
            goto error;
        }

        // cpiofile keeps a copy of what it needs
        mbp_free(ramdisk_data);

        if (mbp_cpiofile_add_file_from_data(
                cpio, rom->id, strlen(rom->id), "romid", 0444) < 0) {
            ui_print("Failed to add ROM ID to ramdisk");
            mbp_cpiofile_destroy(cpio);
            mbp_bootimage_destroy(bi);
            goto error;
        }

        if (mbp_cpiofile_create_data(
                cpio, &ramdisk_data, &ramdisk_size) < 0) {
            ui_print("Failed to create new ramdisk");
            mbp_cpiofile_destroy(cpio);
            mbp_bootimage_destroy(bi);
            goto error;
        }

        mbp_cpiofile_destroy(cpio);

        mbp_bootimage_set_ramdisk_image(bi, ramdisk_data, ramdisk_size);
        mbp_free(ramdisk_data);

        void *bootimg_data;
        size_t bootimg_size;
        mbp_bootimage_create_data(bi, &bootimg_data, &bootimg_size);

        int was_loki = mbp_bootimage_is_loki(bi) == 0;

        mbp_bootimage_destroy(bi);

        // Backup kernel

        if (mb_file_write_data(MB_TEMP "/boot.img", bootimg_data, bootimg_size) < 0) {
            LOGE("Failed to write %s: %s", MB_TEMP "/boot.img", strerror(errno));
            ui_print("Failed to write %s", MB_TEMP "/boot.img");
            mbp_free(bootimg_data);
            goto error;
        }

        mbp_free(bootimg_data);

        // Reloki if needed
        if (was_loki) {
            if (copy_blockdev("/dev/block/platform/msm_sdcc.1/by-name/aboot",
                              MB_TEMP "/aboot.img") < 0) {
                ui_print("Failed to copy aboot partition");
                goto error;
            }

            if (loki_patch("boot", MB_TEMP "/aboot.img",
                           MB_TEMP "/boot.img", MB_TEMP "/boot.lok") != 0) {
                ui_print("Failed to run loki");
                goto error;
            }

            ui_print("Successfully loki'd boot image");

            unlink(MB_TEMP "/boot.img");
            rename(MB_TEMP "/boot.lok", MB_TEMP "/boot.img");
        }

        // Write to multiboot directory and boot partition

        char path[100];
        snprintf(path, sizeof(path), "/data/media/0/MultiBoot/%s", rom->id);
        if (mb_mkdir_recursive(path, 0775) < 0) {
            ui_print("Failed to create %s", path);
            goto error;
        }
        strlcat(path, "/boot.img", sizeof(path));

        int fd_source = open(MB_TEMP "/boot.img", O_RDONLY);
        if (fd_source < 0) {
            LOGE("Failed to open %s: %s", MB_TEMP "/boot.img", strerror(errno));
            goto error;
        }

        int fd_boot = open(boot_block_dev, O_WRONLY);
        if (fd_boot < 0) {
            LOGE("Failed to open %s: %s", boot_block_dev, strerror(errno));
            close(fd_source);
            goto error;
        }

        int fd_backup = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0775);
        if (fd_backup < 0) {
            LOGE("Failed to open %s: %s", path, strerror(errno));
            close(fd_source);
            close(fd_boot);
            goto error;
        }

        if (mb_copy_data_fd(fd_source, fd_boot) < 0) {
            LOGE("Failed to write %s: %s", boot_block_dev, strerror(errno));
            close(fd_source);
            close(fd_boot);
            close(fd_backup);
            goto error;
        }

        lseek(fd_source, 0, SEEK_SET);

        if (mb_copy_data_fd(fd_source, fd_backup) < 0) {
            LOGE("Failed to write %s: %s", path, strerror(errno));
            close(fd_source);
            close(fd_boot);
            close(fd_backup);
            goto error;
        }

        if (fchmod(fd_backup, 0775) < 0) {
            // Non-fatal
            LOGE("%s: Failed to chmod: %s", path, strerror(errno));
        }

        close(fd_source);
        close(fd_boot);
        close(fd_backup);

        if (mb_chown_name(path, "media_rw", "media_rw") < 0) {
            // Non-fatal
            LOGE("%s: Failed to chown: %s", path, strerror(errno));
        }
    }

finish:
    ui_print("Destroying chroot environment");

    remove("/data/.system.img.tmp");

    if (ret < 0 && boot_block_dev
            && copy_blockdev(MB_TEMP "/boot.orig", boot_block_dev) < 0) {
        LOGE("Failed to restore boot partition: %s", strerror(errno));
        ui_print("Failed to restore boot partition");
    }

    mb_roms_cleanup(&r);

    if (pc) {
        mbp_config_destroy(pc);
    }

    free(device);
    free(boot_block_dev);

    if (destroy_chroot() < 0) {
        ui_print("Failed to destroy chroot environment. You should reboot into"
                 " recovery again to avoid flashing issues.");
        return -1;
    }

    if (ret < 0) {
        if (mb_copy_file("/tmp/recovery.log", LOG_FILE, 0) < 0) {
            LOGE("Failed to copy log file: %s", strerror(errno));
        }

        if (chmod(LOG_FILE, 0664) < 0) {
            LOGE("%s: Failed to chmod: %s", LOG_FILE, strerror(errno));
        }

        if (mb_chown_name(LOG_FILE, "media_rw", "media_rw") < 0) {
            LOGE("%s: Failed to chown: %s", LOG_FILE, strerror(errno));
            if (chown(LOG_FILE, 1023, 1023) < 0) {
                LOGE("%s: Failed to chown: %s", LOG_FILE, strerror(errno));
            }
        }

        if (mb_selinux_set_context(
                LOG_FILE, "u:object_r:media_rw_data_file:s0") < 0) {
            LOGE("%s: Failed to set context: %s", LOG_FILE, strerror(errno));
        }

        ui_print("The log file was saved as MultiBoot.log on the internal "
                 "storage.");
    }

    return ret;

error:
    ui_print("Failed to flash zip file.");
    ret = -1;
    goto finish;

success:
    ret = 0;
    goto finish;
}

static void update_binary_usage(int error)
{
    FILE *stream = error ? stderr : stdout;

    fprintf(stream,
            "Usage: update-binary [interface version] [output fd] [zip file]\n\n"
            "This tool wraps the real update-binary program by mounting the correct\n"
            "partitions in a chroot environment and then calls the real program.\n"
            "The real update-binary must be " ZIP_UPDATER ".orig\n"
            "in the zip file.\n\n"
            "Note: The interface version argument is completely ignored.\n");
}

int update_binary_main(int argc, char *argv[])
{
    // Make stdout unbuffered
    setvbuf(stdout, NULL, _IONBF, 0);

    int opt;

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int long_index = 0;

    while ((opt = getopt_long(argc, argv, "h", long_options, &long_index)) != -1) {
        switch (opt) {
        case 'h':
            update_binary_usage(0);
            return EXIT_SUCCESS;

        default:
            update_binary_usage(1);
            return EXIT_FAILURE;
        }
    }

    if (argc - optind != 3) {
        update_binary_usage(1);
        return EXIT_FAILURE;
    }

    char *ptr;
    output_fd = strtol(argv[2], &ptr, 10);
    if (*ptr != '\0' || output_fd < 0) {
        fprintf(stderr, "Invalid output fd");
        return EXIT_FAILURE;
    }

    interface = argv[1];
    output_fd_str = argv[2];
    zip_file = argv[3];

    // stdout is messed up when it's appended to /tmp/recovery.log
    mb_log_set_standard_stream_all(stderr);

    return update_binary() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
