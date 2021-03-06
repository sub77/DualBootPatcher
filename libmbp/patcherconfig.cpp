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

#include "patcherconfig.h"

#include <boost/algorithm/string/erase.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#ifndef LIBMBP_MINI
#include <libxml/parser.h>
#include <libxml/tree.h>
#endif

#include "device.h"
#ifndef LIBMBP_MINI
#include "patcherinterface.h"
#include "patchinfo.h"
#endif
#include "private/logging.h"
#include "private/regex.h"

// Patchers
#ifndef LIBMBP_MINI
#include "patchers/multibootpatcher.h"
#include "autopatchers/jfltepatcher.h"
#include "autopatchers/patchfilepatcher.h"
#include "autopatchers/standardpatcher.h"
#include "ramdiskpatchers/baconramdiskpatcher.h"
#include "ramdiskpatchers/falconramdiskpatcher.h"
#include "ramdiskpatchers/floramdiskpatcher.h"
#include "ramdiskpatchers/hammerheadramdiskpatcher.h"
#include "ramdiskpatchers/hlteramdiskpatcher.h"
#include "ramdiskpatchers/jflteramdiskpatcher.h"
#include "ramdiskpatchers/klteramdiskpatcher.h"
#include "ramdiskpatchers/lgg2ramdiskpatcher.h"
#include "ramdiskpatchers/trlteramdiskpatcher.h"
#endif


/*! \cond INTERNAL */
class PatcherConfig::Impl
{
public:
    // Directories
    std::string dataDir;
    std::string tempDir;

    std::string version;
    std::vector<Device *> devices;
    std::vector<std::string> patchinfoIncludeDirs;

#ifndef LIBMBP_MINI
    // PatchInfos
    std::vector<PatchInfo *> patchInfos;
#endif

    bool loadedConfig;

    // Errors
    PatcherError error;

#ifndef LIBMBP_MINI
    // Created patchers
    std::vector<Patcher *> allocPatchers;
    std::vector<AutoPatcher *> allocAutoPatchers;
    std::vector<RamdiskPatcher *> allocRamdiskPatchers;
#endif

    void loadDefaultDevices();

#ifndef LIBMBP_MINI
    // XML parsing functions for the patchinfo files
    bool loadPatchInfoXml(const std::string &path, const std::string &pathId);
    void parsePatchInfoTagPatchinfo(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagMatches(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagNotMatched(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagName(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagRegex(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagExcludeRegex(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagRegexes(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagHasBootImage(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagRamdisk(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagAutopatchers(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagAutopatcher(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagDeviceCheck(xmlNode *node, PatchInfo * const info, const std::string &type);
#endif
};
/*! \endcond */


#ifndef LIBMBP_MINI
const xmlChar *PatchInfoTagPatchinfo = (xmlChar *) "patchinfo";
const xmlChar *PatchInfoTagMatches = (xmlChar *) "matches";
const xmlChar *PatchInfoTagNotMatched = (xmlChar *) "not-matched";
const xmlChar *PatchInfoTagName = (xmlChar *) "name";
const xmlChar *PatchInfoTagRegex = (xmlChar *) "regex";
const xmlChar *PatchInfoTagExcludeRegex = (xmlChar *) "exclude-regex";
const xmlChar *PatchInfoTagRegexes = (xmlChar *) "regexes";
const xmlChar *PatchInfoTagHasBootImage = (xmlChar *) "has-boot-image";
const xmlChar *PatchInfoTagRamdisk = (xmlChar *) "ramdisk";
const xmlChar *PatchInfoTagAutopatchers = (xmlChar *) "autopatchers";
const xmlChar *PatchInfoTagAutopatcher = (xmlChar *) "autopatcher";
const xmlChar *PatchInfoTagDeviceCheck = (xmlChar *) "device-check";

const xmlChar *PatchInfoAttrRegex = (xmlChar *) "regex";

const xmlChar *XmlTextTrue = (xmlChar *) "true";
const xmlChar *XmlTextFalse = (xmlChar *) "false";
#endif

/*!
 * \class PatcherConfig
 *
 * This is the main interface of the patcher.
 * Blah blah documenting later ;)
 */

PatcherConfig::PatcherConfig() : m_impl(new Impl())
{
    m_impl->loadDefaultDevices();

    m_impl->patchinfoIncludeDirs.push_back("Google_Apps");
    m_impl->patchinfoIncludeDirs.push_back("Other");

    m_impl->version = LIBMBP_VERSION;
}

PatcherConfig::~PatcherConfig()
{
    // Clean up devices
    for (Device *device : m_impl->devices) {
        delete device;
    }
    m_impl->devices.clear();

#ifndef LIBMBP_MINI
    // Clean up patchinfos
    for (PatchInfo *info : m_impl->patchInfos) {
        delete info;
    }
    m_impl->patchInfos.clear();

    for (Patcher *patcher : m_impl->allocPatchers) {
        destroyPatcher(patcher);
    }
    m_impl->allocPatchers.clear();

    for (AutoPatcher *patcher : m_impl->allocAutoPatchers) {
        destroyAutoPatcher(patcher);
    }
    m_impl->allocAutoPatchers.clear();

    for (RamdiskPatcher *patcher : m_impl->allocRamdiskPatchers) {
        destroyRamdiskPatcher(patcher);
    }
    m_impl->allocRamdiskPatchers.clear();
#endif
}

/*!
 * \brief Get error information
 *
 * \note The returned PatcherError contains valid information only if an
 *       operation has failed.
 *
 * \return PatcherError containing information about the error
 */
PatcherError PatcherConfig::error() const
{
    return m_impl->error;
}

/*!
 * \brief Get top-level data directory
 *
 * \return Data directory
 */
std::string PatcherConfig::dataDirectory() const
{
    return m_impl->dataDir;
}

/*!
 * \brief Get the temporary directory
 *
 * The default directory is the system's (OS-dependent) temporary directory.
 *
 * \return Temporary directory
 */
std::string PatcherConfig::tempDirectory() const
{
    if (m_impl->tempDir.empty()) {
        return boost::filesystem::temp_directory_path().string();
    } else {
        return m_impl->tempDir;
    }
}

/*!
 * \brief Set top-level data directory
 *
 * \param path Path to data directory
 */
void PatcherConfig::setDataDirectory(std::string path)
{
    m_impl->dataDir = std::move(path);
}

/*!
 * \brief Set the temporary directory
 *
 * \note This should only be changed if the system's temporary directory is not
 *       desired.
 *
 * \param path Path to temporary directory
 */
void PatcherConfig::setTempDirectory(std::string path)
{
    m_impl->tempDir = std::move(path);
}

/*!
 * \brief Get version number of the patcher
 *
 * \return Version number
 */
std::string PatcherConfig::version() const
{
    return m_impl->version;
}

/*!
 * \brief Get list of supported devices
 *
 * \return List of supported devices
 */
std::vector<Device *> PatcherConfig::devices() const
{
    return m_impl->devices;
}

#ifndef LIBMBP_MINI

/*!
 * \brief Get list of PatchInfos
 *
 * \return List of PatchInfos
 */
std::vector<PatchInfo *> PatcherConfig::patchInfos() const
{
    return m_impl->patchInfos;
}

/*!
 * \brief Get list of PatchInfos for a device
 *
 * \param device Supported device
 *
 * \return List of PatchInfos
 */
std::vector<PatchInfo *> PatcherConfig::patchInfos(const Device * const device) const
{
    std::vector<PatchInfo *> l;

    for (PatchInfo *info : m_impl->patchInfos) {
        if (boost::starts_with(info->id(), device->id())) {
            l.push_back(info);
            continue;
        }

        for (auto const &include : m_impl->patchinfoIncludeDirs) {
            if (boost::starts_with(info->id(), include)) {
                l.push_back(info);
                break;
            }
        }
    }

    return l;
}

/*!
 * \brief Find matching PatchInfo for a file
 *
 * \param device Supported device
 * \param filename Supported file
 *
 * \return PatchInfo if found, otherwise nullptr
 */
PatchInfo * PatcherConfig::findMatchingPatchInfo(Device *device,
                                                 const std::string &filename) const
{
    if (device == nullptr) {
        return nullptr;
    }

    if (filename.empty()) {
        return nullptr;
    }

    std::string noPath = boost::filesystem::path(filename).filename().string();

    for (PatchInfo *info : patchInfos(device)) {
        for (auto const &regex : info->regexes()) {
            if (MBP_regex_search(noPath, MBP_regex(regex))) {
                bool skipCurInfo = false;

                // If the regex matches, make sure the filename isn't matched
                // by one of the exclusion regexes
                for (auto const &excludeRegex : info->excludeRegexes()) {
                    if (MBP_regex_search(noPath, MBP_regex(excludeRegex))) {
                        skipCurInfo = true;
                        break;
                    }
                }

                if (skipCurInfo) {
                    break;
                }

                return info;
            }
        }
    }

    return nullptr;
}

#endif

void PatcherConfig::Impl::loadDefaultDevices()
{
    Device *device;

    const std::string qcomSystem("/dev/block/platform/msm_sdcc.1/by-name/system");
    const std::string qcomCache("/dev/block/platform/msm_sdcc.1/by-name/cache");
    const std::string qcomData("/dev/block/platform/msm_sdcc.1/by-name/userdata");
    const std::string qcomBoot("/dev/block/platform/msm_sdcc.1/by-name/boot");

    const std::string qcomAboot("/dev/block/platform/msm_sdcc.1/by-name/aboot");
    const std::string qcomImgdata("/dev/block/platform/msm_sdcc.1/by-name/imgdata");
    const std::string qcomMisc("/dev/block/platform/msm_sdcc.1/by-name/misc");
    const std::string qcomModem("/dev/block/platform/msm_sdcc.1/by-name/modem");
    const std::string qcomRpm("/dev/block/platform/msm_sdcc.1/by-name/rpm");
    const std::string qcomSbl1("/dev/block/platform/msm_sdcc.1/by-name/sbl1");
    const std::string qcomSdi("/dev/block/platform/msm_sdcc.1/by-name/sdi");
    const std::string qcomTz("/dev/block/platform/msm_sdcc.1/by-name/tz");

    // Samsung Galaxy S 4
    device = new Device();
    device->setId("jflte");
    device->setCodenames({ "jflte" });
    device->setName("Samsung Galaxy S 4");
    device->setSystemBlockDevs({ qcomSystem, "/dev/block/mmcblk0p16" });
    device->setCacheBlockDevs({ qcomCache, "/dev/block/mmcblk0p18" });
    device->setDataBlockDevs({ qcomData, "/dev/block/mmcblk0p29" });
    device->setBootBlockDevs({ qcomBoot, "/dev/block/mmcblk0p20" });
    device->setExtraBlockDevs({ qcomAboot });
    devices.push_back(device);

    // Samsung Galaxy S 5
    device = new Device();
    device->setId("klte");
    device->setCodenames({ "klte" });
    device->setName("Samsung Galaxy S 5");
    device->setSystemBlockDevs({ qcomSystem, "/dev/block/mmcblk0p23" });
    device->setCacheBlockDevs({ qcomCache, "/dev/block/mmcblk0p24" });
    device->setDataBlockDevs({ qcomData, "/dev/block/mmcblk0p26" });
    device->setBootBlockDevs({ qcomBoot, "/dev/block/mmcblk0p15" });
    devices.push_back(device);

    // Samsung Galaxy Note 3
    device = new Device();
    device->setId("hlte");
    device->setCodenames({ "hlte" });
    device->setName("Samsung Galaxy Note 3");
    device->setSystemBlockDevs({ qcomSystem, "/dev/block/mmcblk0p23" });
    device->setCacheBlockDevs({ qcomCache, "/dev/block/mmcblk0p24" });
    device->setDataBlockDevs({ qcomData, "/dev/block/mmcblk0p26" });
    device->setBootBlockDevs({ qcomBoot, "/dev/block/mmcblk0p14" });
    devices.push_back(device);

    // Samsung Galaxy Note 4
    device = new Device();
    device->setId("trlte");
    device->setCodenames({ "trltetmo" });
    device->setName("Samsung Galaxy Note 4 (T-Mobile ONLY)");
    device->setSystemBlockDevs({ qcomSystem, "/dev/block/mmcblk0p24" });
    device->setCacheBlockDevs({ qcomCache, "/dev/block/mmcblk0p25" });
    device->setDataBlockDevs({ qcomData, "/dev/block/mmcblk0p26" });
    device->setBootBlockDevs({ qcomBoot, "/dev/block/mmcblk0p17" });
    devices.push_back(device);

    // Google/LG Nexus 5
    device = new Device();
    device->setId("hammerhead");
    device->setCodenames({ "hammerhead" });
    device->setName("Google/LG Nexus 5");
    device->setSystemBlockDevs({ qcomSystem, "/dev/block/mmcblk0p25" });
    device->setCacheBlockDevs({ qcomCache, "/dev/block/mmcblk0p27" });
    device->setDataBlockDevs({ qcomData, "/dev/block/mmcblk0p28" });
    device->setBootBlockDevs({ qcomBoot, "/dev/block/mmcblk0p19" });
    device->setExtraBlockDevs({ qcomAboot, qcomImgdata, qcomMisc, qcomModem,
                                qcomRpm, qcomSbl1, qcomSdi, qcomTz });
    devices.push_back(device);

    // Google/ASUS Nexus 7 (2013)
    device = new Device();
    device->setId("flo");
    device->setCodenames({ "flo" });
    device->setName("Google/ASUS Nexus 7 (2013)");
    device->setSystemBlockDevs({ qcomSystem, "/dev/block/mmcblk0p22" });
    device->setCacheBlockDevs({ qcomCache, "/dev/block/mmcblk0p23" });
    device->setDataBlockDevs({ qcomData, "/dev/block/mmcblk0p30" });
    device->setBootBlockDevs({ qcomBoot, "/dev/block/mmcblk0p14" });
    devices.push_back(device);

    // OnePlus One
    device = new Device();
    device->setId("bacon");
    device->setCodenames({ "bacon" });
    device->setName("OnePlus One");
    device->setSystemBlockDevs({ qcomSystem, "/dev/block/mmcblk0p14" });
    device->setCacheBlockDevs({ qcomCache, "/dev/block/mmcblk0p16" });
    device->setDataBlockDevs({ qcomData, "/dev/block/mmcblk0p28" });
    device->setBootBlockDevs({ qcomBoot, "/dev/block/mmcblk0p7" });
    devices.push_back(device);

    // LG G2
    device = new Device();
    device->setId("lgg2");
    device->setCodenames({ "d800", "d802", "ls980", "vs980" });
    device->setName("LG G2");
    device->setSystemBlockDevs({ qcomSystem, "/dev/block/mmcblk0p34" });
    device->setCacheBlockDevs({ qcomCache, "/dev/block/mmcblk0p35" });
    device->setDataBlockDevs({ qcomData, "/dev/block/mmcblk0p38" });
    device->setBootBlockDevs({ qcomBoot, "/dev/block/mmcblk0p7" });
    device->setExtraBlockDevs({ qcomAboot });
    devices.push_back(device);

    // Falcon
    device = new Device();
    device->setId("falcon");
    device->setCodenames({ "falcon" });
    device->setName("Motorola Moto G");
    device->setSystemBlockDevs({ qcomSystem /*, TODO */ });
    device->setCacheBlockDevs({ qcomCache /*, TODO */ });
    device->setDataBlockDevs({ qcomData /*, TODO */ });
    device->setBootBlockDevs({ qcomBoot /*, TODO */ });
    devices.push_back(device);
}

#ifndef LIBMBP_MINI

/*!
 * \brief Get list of Patcher IDs
 *
 * \return List of Patcher names
 */
std::vector<std::string> PatcherConfig::patchers() const
{
    std::vector<std::string> list;
    list.push_back(MultiBootPatcher::Id);
    return list;
}

/*!
 * \brief Get list of AutoPatcher IDs
 *
 * \return List of AutoPatcher names
 */
std::vector<std::string> PatcherConfig::autoPatchers() const
{
    std::vector<std::string> list;
    list.push_back(JflteDalvikCachePatcher::Id);
    list.push_back(PatchFilePatcher::Id);
    list.push_back(StandardPatcher::Id);
    return list;
}

/*!
 * \brief Get list of RamdiskPatcher IDs
 *
 * \return List of RamdiskPatcher names
 */
std::vector<std::string> PatcherConfig::ramdiskPatchers() const
{
    std::vector<std::string> list;
    list.push_back(BaconRamdiskPatcher::Id);
    list.push_back(FalconRamdiskPatcher::Id);
    list.push_back(FloAOSPRamdiskPatcher::Id);
    list.push_back(HammerheadDefaultRamdiskPatcher::Id);
    list.push_back(HlteDefaultRamdiskPatcher::Id);
    list.push_back(JflteDefaultRamdiskPatcher::Id);
    list.push_back(KlteDefaultRamdiskPatcher::Id);
    list.push_back(LGG2RamdiskPatcher::Id);
    list.push_back(TrlteDefaultRamdiskPatcher::Id);
    return list;
}

/*!
 * \brief Get Patcher's friendly name
 *
 * \param id Patcher ID
 *
 * \return Patcher's name
 */
std::string PatcherConfig::patcherName(const std::string &id) const
{
    if (id == MultiBootPatcher::Id) {
        return MultiBootPatcher::Name;
    }

    return std::string();
}

/*!
 * \brief Create new Patcher
 *
 * \param id Patcher ID
 *
 * \return New Patcher
 */
Patcher * PatcherConfig::createPatcher(const std::string &id)
{
    Patcher *p = nullptr;

    if (id == MultiBootPatcher::Id) {
        p = new MultiBootPatcher(this);
    }

    if (p != nullptr) {
        m_impl->allocPatchers.push_back(p);
    }

    return p;
}

/*!
 * \brief Create new AutoPatcher
 *
 * \param id AutoPatcher ID
 * \param info FileInfo describing file to be patched
 * \param args AutoPatcher arguments
 *
 * \return New AutoPatcher
 */
AutoPatcher * PatcherConfig::createAutoPatcher(const std::string &id,
                                               const FileInfo * const info,
                                               const PatchInfo::AutoPatcherArgs &args)
{
    AutoPatcher *ap = nullptr;

    if (id == JflteDalvikCachePatcher::Id) {
        ap = new JflteDalvikCachePatcher(this, info);
    } else if (id == PatchFilePatcher::Id) {
        ap = new PatchFilePatcher(this, info, args);
    } else if (id == StandardPatcher::Id) {
        ap = new StandardPatcher(this, info, args);
    }

    if (ap != nullptr) {
        m_impl->allocAutoPatchers.push_back(ap);
    }

    return ap;
}

/*!
 * \brief Create new RamdiskPatcher
 *
 * \param id RamdiskPatcher ID
 * \param info FileInfo describing file to be patched
 * \param cpio CpioFile for the ramdisk archive
 *
 * \return New RamdiskPatcher
 */
RamdiskPatcher * PatcherConfig::createRamdiskPatcher(const std::string &id,
                                                     const FileInfo * const info,
                                                     CpioFile * const cpio)
{
    RamdiskPatcher *rp = nullptr;

    if (id == BaconRamdiskPatcher::Id) {
        rp = new BaconRamdiskPatcher(this, info, cpio);
    } else if (id == FalconRamdiskPatcher::Id) {
        rp = new FalconRamdiskPatcher(this, info, cpio);
    } else if (id == FloAOSPRamdiskPatcher::Id) {
        rp = new FloAOSPRamdiskPatcher(this, info, cpio);
    } else if (id == HammerheadDefaultRamdiskPatcher::Id) {
        rp = new HammerheadDefaultRamdiskPatcher(this, info, cpio);
    } else if (id == HlteDefaultRamdiskPatcher::Id) {
        rp = new HlteDefaultRamdiskPatcher(this, info, cpio);
    } else if (id == JflteDefaultRamdiskPatcher::Id) {
        rp = new JflteDefaultRamdiskPatcher(this, info, cpio);
    } else if (id == KlteDefaultRamdiskPatcher::Id) {
        rp = new KlteDefaultRamdiskPatcher(this, info, cpio);
    } else if (id == LGG2RamdiskPatcher::Id) {
        rp = new LGG2RamdiskPatcher(this, info, cpio);
    } else if (id == TrlteDefaultRamdiskPatcher::Id) {
        rp = new TrlteDefaultRamdiskPatcher(this, info, cpio);
    }

    if (rp != nullptr) {
        m_impl->allocRamdiskPatchers.push_back(rp);
    }

    return rp;
}

/*!
 * \brief Destroys a Patcher and frees its memory
 *
 * \param patcher Patcher to destroy
 */
void PatcherConfig::destroyPatcher(Patcher *patcher)
{
    auto it = std::find(m_impl->allocPatchers.begin(),
                        m_impl->allocPatchers.end(),
                        patcher);

    assert(it != m_impl->allocPatchers.end());

    m_impl->allocPatchers.erase(it);
}

/*!
 * \brief Destroys an AutoPatcher and frees its memory
 *
 * \param patcher AutoPatcher to destroy
 */
void PatcherConfig::destroyAutoPatcher(AutoPatcher *patcher)
{
    auto it = std::find(m_impl->allocAutoPatchers.begin(),
                        m_impl->allocAutoPatchers.end(),
                        patcher);

    assert(it != m_impl->allocAutoPatchers.end());

    m_impl->allocAutoPatchers.erase(it);
}

/*!
 * \brief Destroys a RamdiskPatcher and frees its memory
 *
 * \param patcher RamdiskPatcher to destroy
 */
void PatcherConfig::destroyRamdiskPatcher(RamdiskPatcher *patcher)
{
    auto it = std::find(m_impl->allocRamdiskPatchers.begin(),
                        m_impl->allocRamdiskPatchers.end(),
                        patcher);

    assert(it != m_impl->allocRamdiskPatchers.end());

    m_impl->allocRamdiskPatchers.erase(it);
}

// Based on the code from:
// [1] https://svn.boost.org/trac/boost/ticket/1976
// [2] https://svn.boost.org/trac/boost/ticket/6249
// [3] https://svn.boost.org/trac/boost/attachment/ticket/6249/make_relative_append_example.cpp
static boost::filesystem::path makeRelative(boost::filesystem::path from,
                                            boost::filesystem::path to)
{
    from = boost::filesystem::absolute(from);
    to = boost::filesystem::absolute(to);
    boost::filesystem::path ret;
    boost::filesystem::path::const_iterator iterFrom(from.begin());
    boost::filesystem::path::const_iterator iterTo(to.begin());

    // Find common base
    for (boost::filesystem::path::const_iterator toEnd(to.end()), fromEnd(from.end());
            iterFrom != fromEnd && iterTo != toEnd && *iterFrom == *iterTo;
            ++iterFrom, ++iterTo);

    // Navigate backwards in directory to reach previously found base
    for (boost::filesystem::path::const_iterator fromEnd(from.end());
            iterFrom != fromEnd; ++iterFrom) {
        if (*iterFrom != ".") {
            ret /= "..";
        }
    }

    // Now navigate down the directory branch
    for (; iterTo != to.end(); ++iterTo) {
        ret /= *iterTo;
    }
    return ret;
}

/*!
 * \brief Load all PatchInfo XML files
 *
 * \return Whether or not the XML files were successfully read
 */
bool PatcherConfig::loadPatchInfos()
{
    try {
        const boost::filesystem::path dirPath(dataDirectory() + "/patchinfos");

        boost::filesystem::recursive_directory_iterator it(dirPath);
        boost::filesystem::recursive_directory_iterator end;

        for (; it != end; ++it) {
            if (boost::filesystem::is_regular_file(it->status())
                    && it->path().extension() == ".xml") {
                boost::filesystem::path relPath = makeRelative(dirPath, it->path());
                std::string id = relPath.string();
                boost::erase_tail(id, 4);

                if (!m_impl->loadPatchInfoXml(it->path().string(), id)) {
                    m_impl->error = PatcherError::createXmlError(
                            MBP::ErrorCode::XmlParseFileError,
                            it->path().string());
                    return false;
                }
            }
        }

        return true;
    } catch (std::exception &e) {
        LOGW(e.what());
    }

    return false;
}

bool PatcherConfig::Impl::loadPatchInfoXml(const std::string &path,
                                           const std::string &pathId)
{
    (void) pathId;

    LIBXML_TEST_VERSION

    xmlDoc *doc = xmlReadFile(path.c_str(), nullptr, 0);
    if (doc == nullptr) {
        error = PatcherError::createXmlError(
                MBP::ErrorCode::XmlParseFileError, path);
        return false;
    }

    xmlNode *root = xmlDocGetRootElement(doc);

    for (auto *curNode = root; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagPatchinfo) == 0) {
            PatchInfo *info = new PatchInfo();
            parsePatchInfoTagPatchinfo(curNode, info);
            info->setId(pathId);
            patchInfos.push_back(info);
        } else {
            LOGW("Unknown tag: %s", (char *) curNode->name);
        }
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();

    return true;
}

static std::string xmlStringToStdString(const xmlChar* xmlString) {
    if (xmlString) {
        return std::string(reinterpret_cast<const char *>(xmlString));
    } else {
        return std::string();
    }
}

void PatcherConfig::Impl::parsePatchInfoTagPatchinfo(xmlNode *node,
                                                     PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagPatchinfo) == 0);

    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagPatchinfo) == 0) {
            LOGW("Nested <patchinfo> is not allowed");
        } else if (xmlStrcmp(curNode->name, PatchInfoTagMatches) == 0) {
            parsePatchInfoTagMatches(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagNotMatched) == 0) {
            parsePatchInfoTagNotMatched(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagName) == 0) {
            parsePatchInfoTagName(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRegex) == 0) {
            parsePatchInfoTagRegex(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRegexes) == 0) {
            parsePatchInfoTagRegexes(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagHasBootImage) == 0) {
            parsePatchInfoTagHasBootImage(curNode, info, PatchInfo::Default);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRamdisk) == 0) {
            parsePatchInfoTagRamdisk(curNode, info, PatchInfo::Default);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagAutopatchers) == 0) {
            parsePatchInfoTagAutopatchers(curNode, info, PatchInfo::Default);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagDeviceCheck) == 0) {
            parsePatchInfoTagDeviceCheck(curNode, info, PatchInfo::Default);
        } else {
            LOGW("Unrecognized tag within <patchinfo>: %s",
                 (char *) curNode->name);
        }
    }
}

void PatcherConfig::Impl::parsePatchInfoTagMatches(xmlNode *node,
                                                   PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagMatches) == 0);

    xmlChar *value = xmlGetProp(node, PatchInfoAttrRegex);
    if (value == nullptr) {
        LOGW("<matches> element has no 'regex' attribute");
        return;
    }

    const std::string regex = xmlStringToStdString(value);
    xmlFree(value);

    auto regexes = info->condRegexes();
    regexes.push_back(regex);
    info->setCondRegexes(std::move(regexes));

    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagMatches) == 0) {
            LOGW("Nested <matches> is not allowed");
        } else if (xmlStrcmp(curNode->name, PatchInfoTagHasBootImage) == 0) {
            parsePatchInfoTagHasBootImage(curNode, info, regex);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRamdisk) == 0) {
            parsePatchInfoTagRamdisk(curNode, info, regex);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagAutopatchers) == 0) {
            parsePatchInfoTagAutopatchers(curNode, info, regex);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagDeviceCheck) == 0) {
            parsePatchInfoTagDeviceCheck(curNode, info, regex);
        } else {
            LOGW("Unrecognized tag within <matches>: %s",
                 (char *) curNode->name);
        }
    }
}

void PatcherConfig::Impl::parsePatchInfoTagNotMatched(xmlNode *node,
                                                      PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagNotMatched) == 0);

    info->setHasNotMatched(true);

    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagNotMatched) == 0) {
            LOGW("Nested <not-matched> is not allowed");
        } else if (xmlStrcmp(curNode->name, PatchInfoTagHasBootImage) == 0) {
            parsePatchInfoTagHasBootImage(curNode, info, PatchInfo::NotMatched);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRamdisk) == 0) {
            parsePatchInfoTagRamdisk(curNode, info, PatchInfo::NotMatched);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagAutopatchers) == 0) {
            parsePatchInfoTagAutopatchers(curNode, info, PatchInfo::NotMatched);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagDeviceCheck) == 0) {
            parsePatchInfoTagDeviceCheck(curNode, info, PatchInfo::NotMatched);
        } else {
            LOGW("Unrecognized tag within <not-matched>: %s",
                 (char *) curNode->name);
        }
    }
}

void PatcherConfig::Impl::parsePatchInfoTagName(xmlNode *node,
                                                PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagName) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        if (info->name().empty()) {
            info->setName(xmlStringToStdString(curNode->content));
        } else {
            LOGW("Ignoring additional <name> elements");
        }
    }

    if (!hasText) {
        LOGW("<name> tag has no text");
    }
}

void PatcherConfig::Impl::parsePatchInfoTagRegex(xmlNode *node,
                                                 PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagRegex) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        auto regexes = info->regexes();
        regexes.push_back(xmlStringToStdString(curNode->content));
        info->setRegexes(std::move(regexes));
    }

    if (!hasText) {
        LOGW("<regex> tag has no text");
    }
}

void PatcherConfig::Impl::parsePatchInfoTagExcludeRegex(xmlNode *node,
                                                        PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagExcludeRegex) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        auto regexes = info->excludeRegexes();
        regexes.push_back(xmlStringToStdString(curNode->content));
        info->setExcludeRegexes(std::move(regexes));
    }

    if (!hasText) {
        LOGW("<exclude-regex> tag has no text");
    }
}

void PatcherConfig::Impl::parsePatchInfoTagRegexes(xmlNode *node,
                                                   PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagRegexes) == 0);

    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagRegexes) == 0) {
            LOGW("Nested <regexes> is not allowed");
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRegex) == 0) {
            parsePatchInfoTagRegex(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagExcludeRegex) == 0) {
            parsePatchInfoTagExcludeRegex(curNode, info);
        } else {
            LOGW("Unrecognized tag within <regexes>: %s",
                 (char *) curNode->name);
        }
    }
}

void PatcherConfig::Impl::parsePatchInfoTagHasBootImage(xmlNode *node,
                                                        PatchInfo * const info,
                                                        const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagHasBootImage) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        if (xmlStrcmp(curNode->content, XmlTextTrue) == 0) {
            info->setHasBootImage(type, true);
        } else if (xmlStrcmp(curNode->content, XmlTextFalse) == 0) {
            info->setHasBootImage(type, false);
        } else {
            LOGW("Unknown value for <has-boot-image>: %s",
                 (char *) curNode->content);
        }
    }

    if (!hasText) {
        LOGW("<has-boot-image> tag has no text");
    }
}

void PatcherConfig::Impl::parsePatchInfoTagRamdisk(xmlNode *node,
                                                   PatchInfo * const info,
                                                   const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagRamdisk) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        if (info->ramdisk(type).empty()) {
            info->setRamdisk(type, xmlStringToStdString(curNode->content));
        } else {
            LOGW("Ignoring additional <ramdisk> elements");
        }
    }

    if (!hasText) {
        LOGW("<ramdisk> tag has no text");
    }
}

void PatcherConfig::Impl::parsePatchInfoTagAutopatchers(xmlNode *node,
                                                        PatchInfo * const info,
                                                        const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagAutopatchers) == 0);

    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagAutopatchers) == 0) {
            LOGW("Nested <autopatchers> is not allowed");
        } else if (xmlStrcmp(curNode->name, PatchInfoTagAutopatcher) == 0) {
            parsePatchInfoTagAutopatcher(curNode, info, type);
        } else {
            LOGW("Unrecognized tag within <autopatchers>: %s",
                 (char *) curNode->name);
        }
    }
}

void PatcherConfig::Impl::parsePatchInfoTagAutopatcher(xmlNode *node,
                                                       PatchInfo * const info,
                                                       const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagAutopatcher) == 0);

    PatchInfo::AutoPatcherArgs args;

    for (xmlAttr *attr = node->properties; attr; attr = attr->next) {
        auto name = attr->name;
        auto value = xmlGetProp(node, name);

        args[xmlStringToStdString(name)] = xmlStringToStdString(value);

        xmlFree(value);
    }

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        info->addAutoPatcher(type, xmlStringToStdString(curNode->content),
                             std::move(args));
    }

    if (!hasText) {
        LOGW("<autopatcher> tag has no text");
    }
}

void PatcherConfig::Impl::parsePatchInfoTagDeviceCheck(xmlNode *node,
                                                       PatchInfo * const info,
                                                       const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagDeviceCheck) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        if (xmlStrcmp(curNode->content, XmlTextTrue) == 0) {
            info->setDeviceCheck(type, true);
        } else if (xmlStrcmp(curNode->content, XmlTextFalse) == 0) {
            info->setDeviceCheck(type, false);
        } else {
            LOGW("Unknown value for <device-check>: %s",
                 (char *) curNode->content);
        }
    }

    if (!hasText) {
        LOGW("<device-check> tag has no text");
    }
}

#endif
