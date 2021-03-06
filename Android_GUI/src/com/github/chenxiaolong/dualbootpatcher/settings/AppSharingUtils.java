/*
 * Copyright (C) 2014  Andrew Gunnerson <andrewgunnerson@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

package com.github.chenxiaolong.dualbootpatcher.settings;

import android.content.Context;
import android.os.Build;

import com.github.chenxiaolong.dualbootpatcher.CommandUtils;
import com.github.chenxiaolong.dualbootpatcher.RomUtils;
import com.github.chenxiaolong.dualbootpatcher.RomUtils.RomInformation;
import com.github.chenxiaolong.dualbootpatcher.RootFile;
import com.github.chenxiaolong.dualbootpatcher.patcher.PatcherUtils;
import com.github.chenxiaolong.dualbootpatcher.switcher.SwitcherUtils;
import com.github.chenxiaolong.multibootpatcher.nativelib.LibMbp.BootImage;
import com.github.chenxiaolong.multibootpatcher.nativelib.LibMbp.CpioFile;
import com.github.chenxiaolong.multibootpatcher.nativelib.LibMbp.PatcherError;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;

public class AppSharingUtils {
    public static final String TAG = AppSharingUtils.class.getSimpleName();

    private static final String SHARE_APPS_PATH = "/data/patcher.share-app";
    private static final String SHARE_PAID_APPS_PATH = "/data/patcher.share-app-asec";

    public static boolean setShareAppsEnabled(boolean enable) {
        if (enable) {
            CommandUtils.runRootCommand("touch " + SHARE_APPS_PATH);
            return isShareAppsEnabled();
        } else {
            CommandUtils.runRootCommand("rm -f " + SHARE_APPS_PATH);
            return !isShareAppsEnabled();
        }
    }

    public static boolean setSharePaidAppsEnabled(boolean enable) {
        if (enable) {
            CommandUtils.runRootCommand("touch " + SHARE_PAID_APPS_PATH);
            return isSharePaidAppsEnabled();
        } else {
            CommandUtils.runRootCommand("rm -f " + SHARE_PAID_APPS_PATH);
            return !isSharePaidAppsEnabled();
        }
    }

    public static boolean isShareAppsEnabled() {
        return new RootFile(SHARE_APPS_PATH).isFile();
    }

    public static boolean isSharePaidAppsEnabled() {
        return new RootFile(SHARE_PAID_APPS_PATH).isFile();
    }

    public static HashMap<RomInformation, ArrayList<String>> getAllApks() {
        RomInformation[] roms = RomUtils.getRoms();

        HashMap<RomInformation, ArrayList<String>> apksMap = new HashMap<>();

        for (RomInformation rom : roms) {
            ArrayList<String> apks = new ArrayList<>();
            String[] filenames = new RootFile(rom.data + File.separator + "app").list();

            if (filenames == null || filenames.length == 0) {
                continue;
            }

            for (String filename : filenames) {
                if (filename.endsWith(".apk")) {
                    apks.add(rom.data + File.separator + "app" + File.separator + filename);
                }
            }

            if (apks.size() > 0) {
                apksMap.put(rom, apks);
            }
        }

        return apksMap;
    }

    public static void updateRamdisk(Context context) throws Exception {
        PatcherUtils.extractPatcher(context);

        RomInformation romInfo = RomUtils.getCurrentRom(context);
        if (romInfo == null) {
            throw new Exception("Could not determine current ROM");
        }

        String bootImage = String.format(SwitcherUtils.KERNEL_PATH_ROOT, romInfo.id);
        RootFile bootImageFile = new RootFile(bootImage);

        if (!bootImageFile.isFile()) {
            SwitcherUtils.backupKernel(romInfo.id);
        }

        String tmpKernel = context.getCacheDir() + File.separator + "boot.img";
        RootFile tmpKernelFile = new RootFile(tmpKernel);
        bootImageFile.copyTo(tmpKernelFile);
        tmpKernelFile.chmod(0777);

        BootImage bi = new BootImage();
        if (!bi.load(tmpKernel)) {
            PatcherError error = bi.getError();
            throw new Exception("Error code: " + error.getErrorCode());
        }

        CpioFile cpio = new CpioFile();
        if (!cpio.load(bi.getRamdiskImage())) {
            PatcherError error = cpio.getError();
            throw new Exception("Error code: " + error.getErrorCode());
        }

        if (cpio.isExists("mbtool")) {
            cpio.remove("mbtool");
        }

        if (!cpio.addFile(PatcherUtils.getTargetDirectory(context)
                + "/binaries/android/" + Build.CPU_ABI + "/mbtool", "mbtool", 0755)) {
            PatcherError error = cpio.getError();
            throw new Exception("Error code: " + error.getErrorCode());
        }

        bi.setRamdiskImage(cpio.createData());

        if (!bi.createFile(tmpKernel)) {
            PatcherError error = bi.getError();
            throw new Exception("Error code: " + error.getErrorCode());
        }

        if (bi.isLoki()) {
            throw new Exception("loki support currently disabled");
            //String aboot = context.getCacheDir() + File.separator + "aboot.img";
            //SwitcherUtils.dd(SwitcherUtils.ABOOT_PARTITION, aboot);
            //new RootFile(aboot).chmod(0666);

            //String lokiKernel = context.getCacheDir() + File.separator + "kernel.lok";

            //if (lokiPatch("boot", aboot, tmpKernel, lokiKernel) != 0) {
            //    throw new Exception("Failed to loki patch new boot image");
            //}

            //new File(lokiKernel).delete();

            //org.apache.commons.io.FileUtils.moveFile(
            //        new File(lokiKernel), new File(tmpKernel));
        }

        // Copy to target
        new RootFile(tmpKernel).copyTo(bootImageFile);
        bootImageFile.chmod(0755);

        SwitcherUtils.writeKernel(romInfo.id);
    }
}
