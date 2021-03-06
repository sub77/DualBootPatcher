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

#pragma once

#include <stddef.h>

#include "cwrapper/ctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

CBootImage * mbp_bootimage_create(void);
void mbp_bootimage_destroy(CBootImage *bootImage);

CPatcherError * mbp_bootimage_error(const CBootImage *bootImage);

int mbp_bootimage_load_data(CBootImage *bootImage,
                            const void *data, size_t size);
int mbp_bootimage_load_file(CBootImage *bootImage,
                            const char *filename);

void mbp_bootimage_create_data(const CBootImage *bootImage,
                               void **data, size_t *size);
int mbp_bootimage_create_file(CBootImage *bootImage,
                              const char *filename);
int mbp_bootimage_extract(CBootImage *bootImage,
                          const char *directory, const char *prefix);

int mbp_bootimage_is_loki(CBootImage *bootImage);

char * mbp_bootimage_boardname(const CBootImage *bootImage);
void mbp_bootimage_set_boardname(CBootImage *bootImage,
                                 const char *name);
void mbp_bootimage_reset_boardname(CBootImage *bootImage);

char * mbp_bootimage_kernel_cmdline(const CBootImage *bootImage);
void mbp_bootimage_set_kernel_cmdline(CBootImage *bootImage,
                                      const char *cmdline);
void mbp_bootimage_reset_kernel_cmdline(CBootImage *bootImage);

unsigned int mbp_bootimage_page_size(const CBootImage *bootImage);
void mbp_bootimage_set_page_size(CBootImage *bootImage,
                                 unsigned int pageSize);
void mbp_bootimage_reset_page_size(CBootImage *bootImage);

unsigned int mbp_bootimage_kernel_address(const CBootImage *bootImage);
void mbp_bootimage_set_kernel_address(CBootImage *bootImage,
                                      unsigned int address);
void mbp_bootimage_reset_kernel_address(CBootImage *bootImage);

unsigned int mbp_bootimage_ramdisk_address(const CBootImage *bootImage);
void mbp_bootimage_set_ramdisk_address(CBootImage *bootImage,
                                       unsigned int address);
void mbp_bootimage_reset_ramdisk_address(CBootImage *bootImage);

unsigned int mbp_bootimage_second_bootloader_address(const CBootImage *bootImage);
void mbp_bootimage_set_second_bootloader_address(CBootImage *bootImage,
                                                 unsigned int address);
void mbp_bootimage_reset_second_bootloader_address(CBootImage *bootImage);

unsigned int mbp_bootimage_kernel_tags_address(const CBootImage *bootImage);
void mbp_bootimage_set_kernel_tags_address(CBootImage *bootImage,
                                           unsigned int address);
void mbp_bootimage_reset_kernel_tags_address(CBootImage *bootImage);

void mbp_bootimage_set_addresses(CBootImage *bootImage,
                                 unsigned int base, unsigned int kernelOffset,
                                 unsigned int ramdiskOffset,
                                 unsigned int secondBootloaderOffset,
                                 unsigned int kernelTagsOffset);

size_t mbp_bootimage_kernel_image(const CBootImage *bootImage,
                                  void **data);
void mbp_bootimage_set_kernel_image(CBootImage *bootImage,
                                    const void *data, size_t size);

size_t mbp_bootimage_ramdisk_image(const CBootImage *bootImage,
                                   void **data);
void mbp_bootimage_set_ramdisk_image(CBootImage *bootImage,
                                     const void *data, size_t size);

size_t mbp_bootimage_second_bootloader_image(const CBootImage *bootImage,
                                             void **data);
void mbp_bootimage_set_second_bootloader_image(CBootImage *bootImage,
                                               const void *data, size_t size);

size_t mbp_bootimage_device_tree_image(const CBootImage *bootImage,
                                       void **data);
void mbp_bootimage_set_device_tree_image(CBootImage *bootImage,
                                         const void *data, size_t size);

#ifdef __cplusplus
}
#endif
