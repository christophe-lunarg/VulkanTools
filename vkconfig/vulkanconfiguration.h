/*
 * Copyright (c) 2020 Valve Corporation
 * Copyright (c) 2020 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * The vkConfig2 program monitors and adjusts the Vulkan configuration
 * environment. These settings are wrapped in this class, which serves
 * as the "model" of the system.
 *
 * Author: Richard S. Wright Jr. <richard@lunarg.com>
 */
#ifndef CVULKANCONFIGURATION_H
#define CVULKANCONFIGURATION_H

#ifdef _WIN32
#include <windows.h>
#include <winreg.h>
#include <Cfgmgr32.h>
#define WIN_BUFFER_SIZE 1024
#endif

#include <QString>
#include <QVector>
#include <QSettings>
#include <QDir>

#include <layerfile.h>
#include "profiledef.h"


//////////////////////////////////////////////////////////////////////////
// Canned/fixed profiles
// Compiler warning says this is not used... it is.
// Don't beleive me? Go ahead... delete it... ;)
static const char *szCannedProfiles[10] = {
"Standard Validation",          ":/resourcefiles/Standard Validation.json",
"Best Practices Validation",    ":/resourcefiles/Best Practices Validation.json",
"Shader Based Validation",      ":/resourcefiles/Shader Based Validation.json",
"Low Overhead Validation",  ":/resourcefiles/Low Overhead Validation.json",
};


////////////////////////////////////////////////////////////////////////////////
/// \brief The CPathFinder class
/// Going back and forth between the Windows registry and looking for files
/// in specific folders is just a mess. This class consolidates all that into
/// one single abstraction that knows whether to look in the registry or in
/// a folder with QDir.
/// This is a little weird because generally QSettings is for going back
/// and forth between the Registry or .ini files. Here, I'm going from
/// the registry to directory entries.
class CPathFinder
{
public:
#ifdef _WIN32
    CPathFinder(const QString& qsPath, bool bForceFileSystem = false);
#else
    CPathFinder(const QString& qsPath, bool bForceFileSystem = true);
#endif
    int FileCount(void) { return fileList.size(); }
    QString GetFileName(int iIndex) { return fileList[iIndex]; }
protected:
    QStringList fileList;
};


#define VKCONFIG_NAME       "Vulkan Configurator"

// Saved settings for the application
#define VKCONFIG_KEY_LAUNCHAPP      "launchApp"
#define VKCONFIG_KEY_LAUNCHAPP_CWD  "launchAppCWD"
#define VKCONFIG_KEY_LAUNCHAPP_ARGS "launchAppARGS"
#define VKCONFIG_KEY_LOGFILE        "logFileName"
#define VKCONFIG_KEY_ACTIVEPROFILE  "activeProfile"
#define VKCONFIG_KEY_APPLIST        "applicationList"
#define VKCONFIG_KEY_CUSTOM_PATHS   "customPaths"

// This is a master list of layer settings. All the settings
// for what layers can have user modified settings. It contains
// the names of the layers and the properties of the settings.
// THIS IS TO BE READ ONLY, as it is copied from frequently
// to reset or initialize the a full layer definition for the
// profiles.
class LayerSettingsDefaults {
public:
    QString                 layerName;                  // Name of layer
    QVector<TLayerSettings *>defaultSettings;           // Default settings for this layer
};




class CVulkanConfiguration
{
public:
    static CVulkanConfiguration* getVulkanConfig(void) {
        if(pMe == nullptr)
            pMe = new CVulkanConfiguration();

        return pMe;
        }

    ~CVulkanConfiguration();

    // Just local app settings
    void loadAppSettings(void);
    void saveAppSettings(void);
    QString qsLaunchApplicationWPath;
    QString qsLaunchApplicatinArgs;
    QString qsLaunchApplicationWorkingDir;
    QString qsLogFileWPath;

    /////////////////////////////////////////////////////////////////////////
    // Additional places to look for layers
    void loadAdditionalSearchPaths(void);
    void saveAdditionalSearchPaths(void);
    QStringList additionalSearchPaths;

    /////////////////////////////////////////////////////////////////////////
    // The list of applications affected
    QStringList appList;
    void loadAppList(void);
    void saveAppList(void);


    ////////////////////////////////////////////////////////////////////////
    // A readonly list of layer names with the associated settings
    // and their default values. This is for reference by individual profile
    // objects.
    QVector <LayerSettingsDefaults *> defaultLayerSettings;
    void loadDefaultLayerSettings(void);
    const LayerSettingsDefaults* findSettingsFor(QString layerName);
    void LoadDefaultSettings(CLayerFile* pBlankLayer);

    ////////////////////////////////////////////////////////////////////////
    // Look for all installed layers. This contains their path, version info, etc.
    // but does not contain settings information. The default settings are stored
    // in the above (defaultLayerSettings). The binding of a layer with it's
    // particular settings is done in the profile (CProfileDef - in profile list).
    // This includes all found implicit, explicit, or layers found in custom folders
    QVector <CLayerFile*>   allLayers;          // All the found layers, lumped together
    void findAllInstalledLayers(void);
    void loadLayersFromPath(const QString &qsPath, QVector<CLayerFile *>& layerList, TLayerType type);
    const CLayerFile* findLayerNamed(QString qsLayerName, const char* location = nullptr);



    QVector <CProfileDef *>  profileList;       // List and details about current profiles

    // We need to push and pop a temporary environment.
    // The stack is only one deep...
    CProfileDef*             pSavedProfile;

    void pushProfile(CProfileDef *pNew);
    void popProfile(void);


    CProfileDef* CreateEmptyProfile(void);
    CProfileDef* FindProfile(QString profileName);
    CProfileDef* LoadProfile(QString pathToProfile);        // Load .profile descriptor
    void loadAllProfiles(void);                             // Load all the .profile files found
    void SaveProfile(CProfileDef *pProfile);                // Write .profile descriptor

    // Set this as the current override profile
    void SetCurrentActiveProfile(CProfileDef *pProfile);
    CProfileDef* GetCurrentActiveProfile(void) { return pActiveProfile; }
    void RefreshProfile(void) {
                if(pActiveProfile)
                    SetCurrentActiveProfile(pActiveProfile);
                }

    QString getProfilePath(void) { return qsProfileFilesPath; }

protected:
    CVulkanConfiguration();
    static CVulkanConfiguration*    pMe;

    // Currently active profile
    CProfileDef*                    pActiveProfile;

    void clearLayerLists(void);

    QString qsProfileFilesPath;         // Where config working files live
    QString qsOverrideSettingsPath;     // Where settings go when profile is active
    QString qsOverrideJsonPath;         // Where json goes when profile is active

#ifdef WIN32
    void LoadDeviceRegistry(DEVINST id, const QString& entry, QVector<CLayerFile *>& layerList, TLayerType type);
    void LoadRegistryLayers(const QString &path, QVector<CLayerFile *>& layerList, TLayerType type);
#endif
};

#endif // CVULKANCONFIGURATION_H
