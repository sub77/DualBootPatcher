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

#include <memory>

#include "cpiofile.h"
#include "patcherconfig.h"
#include "patcherinterface.h"


class CoreRamdiskPatcher : public RamdiskPatcher
{
public:
    explicit CoreRamdiskPatcher(const PatcherConfig * const pc,
                                const FileInfo * const info,
                                CpioFile * const cpio);
    ~CoreRamdiskPatcher();

    static const std::string FstabRegex;
#if 0
    static const std::string SyncdaemonService;
#endif

    virtual PatcherError error() const override;

    virtual std::string id() const override;

    virtual bool patchRamdisk() override;

#if 0
    bool addSyncdaemon();
#endif
    bool fixDataMediaContext();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
