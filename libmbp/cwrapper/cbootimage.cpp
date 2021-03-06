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

#include "cwrapper/cbootimage.h"

#include <cassert>

#include "cwrapper/private/util.h"

#include "bootimage.h"


#define CAST(x) \
    assert(x != nullptr); \
    BootImage *bi = reinterpret_cast<BootImage *>(x);
#define CCAST(x) \
    assert(x != nullptr); \
    const BootImage *bi = reinterpret_cast<const BootImage *>(x);


/*!
 * \file cbootimage.h
 * \brief C Wrapper for BootImage
 *
 * Please see the documentation for BootImage from the C++ API for more details.
 * The C functions directly correspond to the BootImage member functions.
 *
 * \sa BootImage
 */

extern "C" {

/*!
 * \brief Create a new CBootImage object.
 *
 * \note The returned object must be freed with mbp_bootimage_destroy().
 *
 * \return New CBootImage
 */
CBootImage * mbp_bootimage_create(void)
{
    return reinterpret_cast<CBootImage *>(new BootImage());
}

/*!
 * \brief Destroys a CBootImage object.
 *
 * \param bootImage CBootImage to destroy
 */
void mbp_bootimage_destroy(CBootImage *bootImage)
{
    CAST(bootImage);
    delete bi;
}

/*!
 * \brief Get the error information
 *
 * \note The returned CPatcherError is filled with valid data only if a
 *       CBootImage operation has failed.
 *
 * \note The returned CPatcherError should be freed with mbp_error_destroy()
 *       when it is no longer needed.
 *
 * \param bootImage CBootImage object
 *
 * \return CPatcherError
 *
 * \sa BootImage::error()
 */
CPatcherError * mbp_bootimage_error(const CBootImage *bootImage)
{
    CCAST(bootImage);
    PatcherError *pe = new PatcherError(bi->error());
    return reinterpret_cast<CPatcherError *>(pe);
}

/*!
 * \brief Load boot image from binary data
 *
 * \param bootImage CBootImage object
 * \param data Byte array containing binary data
 * \param size Size of byte array
 *
 * \return 0 on success or -1 on failure and error set appropriately
 *
 * \sa BootImage::load(const std::vector<unsigned char> &)
 */
int mbp_bootimage_load_data(CBootImage *bootImage,
                            const void *data, size_t size)
{
    CAST(bootImage);
    bool ret = bi->load(data_to_vector(data, size));
    return ret ? 0 : -1;
}

/*!
 * \brief Load boot image from a file
 *
 * \param bootImage CBootImage object
 * \param filename Path to boot image file
 *
 * \return 0 on success or -1 on failure and error set appropriately
 *
 * \sa BootImage::load(const std::string &)
 */
int mbp_bootimage_load_file(CBootImage *bootImage,
                            const char *filename)
{
    CAST(bootImage);
    bool ret = bi->load(filename);
    return ret ? 0 : -1;
}

/*!
 * \brief Constructs the boot image binary data
 *
 * \note The output data is dynamically allocated. It should be free()'d
 *       when it is no longer needed.
 *
 * \param bootImage CBootImage object
 * \param data Output data
 * \param size Size of output data
 *
 * \sa BootImage::create()
 */
void mbp_bootimage_create_data(const CBootImage *bootImage, void **data, size_t *size)
{
    CCAST(bootImage);
    std::vector<unsigned char> vData = bi->create();
    vector_to_data(vData, data, size);
}

/*!
 * \brief Constructs boot image and writes it to a file
 *
 * \param bootImage CBootImage object
 * \param filename Path to output file
 *
 * \return 0 on success or -1 on failure and error set appropriately
 *
 * \sa BootImage::createFile()
 */
int mbp_bootimage_create_file(CBootImage *bootImage,
                              const char *filename)
{
    CAST(bootImage);
    bool ret = bi->createFile(filename);
    return ret ? 0 : -1;
}

/*!
 * \brief Extracts boot image header and data to a directory
 *
 * \param bootImage CBootImage object
 * \param directory Output directory
 * \param prefix Filename prefix
 *
 * \return 0 on success or -1 on failure and error set appropriately
 *
 * \sa BootImage::extract()
 */
int mbp_bootimage_extract(CBootImage *bootImage,
                          const char *directory, const char *prefix)
{
    CAST(bootImage);
    bool ret = bi->extract(directory, prefix);
    return ret ? 0 : -1;
}

int mbp_bootimage_is_loki(CBootImage *bootImage)
{
    CCAST(bootImage);
    return bi->isLoki() ? 0 : -1;
}

/*!
 * \brief Board name field in the boot image header
 *
 * \note The returned string is dynamically allocated. It should be free()'d
 *       when it is no longer needed.
 *
 * \param bootImage CBootImage object
 *
 * \return Board name
 *
 * \sa BootImage::boardName()
 */
char * mbp_bootimage_boardname(const CBootImage *bootImage)
{
    CCAST(bootImage);
    return string_to_cstring(bi->boardName());
}

/*!
 * \brief Set the board name field in the boot image header
 *
 * \param bootImage CBootImage object
 * \param name Board name
 *
 * \sa BootImage::setBoardName()
 */
void mbp_bootimage_set_boardname(CBootImage *bootImage,
                                 const char *name)
{
    CAST(bootImage);
    bi->setBoardName(name);
}

/*!
 * \brief Resets the board name field in the boot image header to the default
 *
 * \param bootImage CBootImage object
 *
 * \sa BootImage::resetBoardName()
 */
void mbp_bootimage_reset_boardname(CBootImage *bootImage)
{
    CAST(bootImage);
    bi->resetBoardName();
}

/*!
 * \brief Kernel cmdline in the boot image header
 *
 * \note The returned string is dynamically allocated. It should be free()'d
 *       when it is no longer needed.
 *
 * \param bootImage CBootImage object
 *
 * \return Kernel cmdline
 *
 * \sa BootImage::kernelCmdline()
 */
char * mbp_bootimage_kernel_cmdline(const CBootImage *bootImage)
{
    CCAST(bootImage);
    return string_to_cstring(bi->kernelCmdline());
}

/*!
 * \brief Set the kernel cmdline in the boot image header
 *
 * \param bootImage CBootImage object
 * \param cmdline Kernel cmdline
 *
 * \sa BootImage::setKernelCmdline()
 */
void mbp_bootimage_set_kernel_cmdline(CBootImage *bootImage,
                                      const char *cmdline)
{
    CAST(bootImage);
    bi->setKernelCmdline(cmdline);
}

/*!
 * \brief Resets the kernel cmdline to the default
 *
 * \param bootImage CBootImage object
 *
 * \sa BootImage::resetKernelCmdline()
 */
void mbp_bootimage_reset_kernel_cmdline(CBootImage *bootImage)
{
    CAST(bootImage);
    bi->resetKernelCmdline();
}

/*!
 * \brief Page size field in the boot image header
 *
 * \param bootImage CBootImage object
 *
 * \return Page size
 *
 * \sa BootImage::pageSize()
 */
unsigned int mbp_bootimage_page_size(const CBootImage *bootImage)
{
    CCAST(bootImage);
    return bi->pageSize();
}

/*!
 * \brief Set the page size field in the boot image header
 *
 * \param bootImage CBootImage object
 * \param pageSize Page size
 *
 * \sa BootImage::setPageSize()
 */
void mbp_bootimage_set_page_size(CBootImage *bootImage,
                                 unsigned int pageSize)
{
    CAST(bootImage);
    bi->setPageSize(pageSize);
}

/*!
 * \brief Resets the page size field in the header to the default
 *
 * \param bootImage CBootImage object
 *
 * \sa BootImage::resetPageSize()
 */
void mbp_bootimage_reset_page_size(CBootImage *bootImage)
{
    CAST(bootImage);
    bi->resetPageSize();
}

/*!
 * \brief Kernel address field in the boot image header
 *
 * \param bootImage CBootImage object
 *
 * \return Kernel address
 *
 * \sa BootImage::kernelAddress()
 */
unsigned int mbp_bootimage_kernel_address(const CBootImage *bootImage)
{
    CCAST(bootImage);
    return bi->kernelAddress();
}

/*!
 * \brief Set the kernel address field in the boot image header
 *
 * \param bootImage CBootImage object
 * \param address Kernel address
 *
 * \sa BootImage::setKernelAddress()
 */
void mbp_bootimage_set_kernel_address(CBootImage *bootImage,
                                      unsigned int address)
{
    CAST(bootImage);
    bi->setKernelAddress(address);
}

/*!
 * \brief Resets the kernel address field in the header to the default
 *
 * \param bootImage CBootImage object
 *
 * \sa BootImage::resetKernelAddress()
 */
void mbp_bootimage_reset_kernel_address(CBootImage *bootImage)
{
    CAST(bootImage);
    bi->resetKernelAddress();
}

/*!
 * \brief Ramdisk address field in the boot image header
 *
 * \param bootImage CBootImage object
 *
 * \return Ramdisk address
 *
 * \sa BootImage::ramdiskAddress()
 */
unsigned int mbp_bootimage_ramdisk_address(const CBootImage *bootImage)
{
    CCAST(bootImage);
    return bi->ramdiskAddress();
}

/*!
 * \brief Set the ramdisk address field in the boot image header
 *
 * \param bootImage CBootImage object
 * \param address Ramdisk address
 *
 * \sa BootImage::setRamdiskAddress()
 */
void mbp_bootimage_set_ramdisk_address(CBootImage *bootImage,
                                       unsigned int address)
{
    CAST(bootImage);
    bi->setRamdiskAddress(address);
}

/*!
 * \brief Resets the ramdisk address field in the header to the default
 *
 * \param bootImage CBootImage object
 *
 * \sa BootImage::resetRamdiskAddress()
 */
void mbp_bootimage_reset_ramdisk_address(CBootImage *bootImage)
{
    CAST(bootImage);
    bi->resetRamdiskAddress();
}

/*!
 * \brief Second bootloader address field in the boot image header
 *
 * \param bootImage CBootImage object
 *
 * \return Second bootloader address
 *
 * \sa BootImage::secondBootloaderAddress()
 */
unsigned int mbp_bootimage_second_bootloader_address(const CBootImage *bootImage)
{
    CCAST(bootImage);
    return bi->secondBootloaderAddress();
}

/*!
 * \brief Set the second bootloader address field in the boot image header
 *
 * \param bootImage CBootImage object
 * \param address Second bootloader address
 *
 * \sa BootImage::setSecondBootloaderAddress()
 */
void mbp_bootimage_set_second_bootloader_address(CBootImage *bootImage,
                                                 unsigned int address)
{
    CAST(bootImage);
    bi->setSecondBootloaderAddress(address);
}

/*!
 * \brief Resets the second bootloader address field in the header to the
 *        default
 *
 * \param bootImage CBootImage object
 *
 * \sa BootImage::resetSecondBootloaderAddress()
 */
void mbp_bootimage_reset_second_bootloader_address(CBootImage *bootImage)
{
    CAST(bootImage);
    bi->resetSecondBootloaderAddress();
}

/*!
 * \brief Kernel tags address field in the boot image header
 *
 * \param bootImage CBootImage object
 *
 * \return Kernel tags address
 *
 * \sa BootImage::kernelTagsAddress()
 */
unsigned int mbp_bootimage_kernel_tags_address(const CBootImage *bootImage)
{
    CCAST(bootImage);
    return bi->kernelTagsAddress();
}

/*!
 * \brief Set the kernel tags address field in the boot image header
 *
 * \param bootImage CBootImage object
 * \param address Kernel tags address
 *
 * \sa BootImage::setKernelTagsAddress()
 */
void mbp_bootimage_set_kernel_tags_address(CBootImage *bootImage,
                                           unsigned int address)
{
    CAST(bootImage);
    bi->setKernelTagsAddress(address);
}

/*!
 * \brief Resets the kernel tags address field in the header to the default
 *
 * \param bootImage CBootImage object
 *
 * \sa BootImage::resetKernelTagsAddress()
 */
void mbp_bootimage_reset_kernel_tags_address(CBootImage *bootImage)
{
    CAST(bootImage);
    bi->resetKernelTagsAddress();
}

/*!
 * \brief Set all of the addresses using offsets and a base address
 *
 * \param bootImage CBootImage object
 * \param base Base address
 * \param kernelOffset Kernel offset
 * \param ramdiskOffset Ramdisk offset
 * \param secondBootloaderOffset Second bootloader offset
 * \param kernelTagsOffset Kernel tags offset
 *
 * \sa BootImage::setAddresses()
 */
void mbp_bootimage_set_addresses(CBootImage *bootImage,
                                 unsigned int base,
                                 unsigned int kernelOffset,
                                 unsigned int ramdiskOffset,
                                 unsigned int secondBootloaderOffset,
                                 unsigned int kernelTagsOffset)
{
    CAST(bootImage);
    bi->setAddresses(base, kernelOffset, ramdiskOffset,
                     secondBootloaderOffset, kernelTagsOffset);
}

/*!
 * \brief Kernel image
 *
 * \note The output data is dynamically allocated. It should be free()'d
 *       when it is no longer needed.
 *
 * \param bootImage CBootImage object
 * \param data Output data
 *
 * \return Size of output data
 *
 * \sa BootImage::kernelImage()
 */
size_t mbp_bootimage_kernel_image(const CBootImage *bootImage, void **data)
{
    CCAST(bootImage);
    return vector_to_data2(bi->kernelImage(), data);
}

/*!
 * \brief Set the kernel image
 *
 * \param bootImage CBootImage object
 * \param data Byte array containing kernel image
 * \param size Size of byte array
 *
 * \sa BootImage::setKernelImage()
 */
void mbp_bootimage_set_kernel_image(CBootImage *bootImage,
                                    const void *data, size_t size)
{
    CAST(bootImage);
    bi->setKernelImage(data_to_vector(data, size));
}

/*!
 * \brief Ramdisk image
 *
 * \note The output data is dynamically allocated. It should be free()'d
 *       when it is no longer needed.
 *
 * \param bootImage CBootImage object
 * \param data Output data
 *
 * \return Size of output data
 *
 * \sa BootImage::ramdiskImage()
 */
size_t mbp_bootimage_ramdisk_image(const CBootImage *bootImage, void **data)
{
    CCAST(bootImage);
    return vector_to_data2(bi->ramdiskImage(), data);
}

/*!
 * \brief Set the ramdisk image
 *
 * \param bootImage CBootImage object
 * \param data Byte array containing ramdisk image
 * \param size Size of byte array
 *
 * \sa BootImage::setRamdiskImage()
 */
void mbp_bootimage_set_ramdisk_image(CBootImage *bootImage,
                                     const void *data, size_t size)
{
    CAST(bootImage);
    bi->setRamdiskImage(data_to_vector(data, size));
}

/*!
 * \brief Second bootloader image
 *
 * \note The output data is dynamically allocated. It should be free()'d
 *       when it is no longer needed.
 *
 * \param bootImage CBootImage object
 * \param data Output data
 *
 * \return Size of output data
 *
 * \sa BootImage::secondBootloaderImage()
 */
size_t mbp_bootimage_second_bootloader_image(const CBootImage *bootImage,
                                             void **data)
{
    CCAST(bootImage);
    return vector_to_data2(bi->secondBootloaderImage(), data);
}

/*!
 * \brief Set the second bootloader image
 *
 * \param bootImage CBootImage object
 * \param data Byte array containing second bootloader image
 * \param size Size of byte array
 *
 * \sa BootImage::setSecondBootloaderImage()
 */
void mbp_bootimage_set_second_bootloader_image(CBootImage *bootImage,
                                               const void *data, size_t size)
{
    CAST(bootImage);
    bi->setSecondBootloaderImage(data_to_vector(data, size));
}

/*!
 * \brief Device tree image
 *
 * \note The output data is dynamically allocated. It should be free()'d
 *       when it is no longer needed.
 *
 * \param bootImage CBootImage object
 * \param data Output data
 *
 * \return Size of output data
 *
 * \sa BootImage::deviceTreeImage()
 */
size_t mbp_bootimage_device_tree_image(const CBootImage *bootImage,
                                       void **data)
{
    CCAST(bootImage);
    return vector_to_data2(bi->deviceTreeImage(), data);
}

/*!
 * \brief Set the device tree image
 *
 * \param bootImage CBootImage object
 * \param data Byte array containing device tree image
 * \param size Size of byte array
 *
 * \sa BootImage::setDeviceTreeImage()
 */
void mbp_bootimage_set_device_tree_image(CBootImage *bootImage,
                                         const void *data, size_t size)
{
    CAST(bootImage);
    bi->setDeviceTreeImage(data_to_vector(data, size));
}

}
