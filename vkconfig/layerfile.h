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
 * Authors:
 * - Richard S. Wright Jr. <richard@lunarg.com>
 * - Christophe Riccio <christophe@lunarg.com>
 */

#pragma once

#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QString>
#include <QVector>
#include <QVariant>

typedef enum { LAYER_TYPE_EXPLICIT = 0, LAYER_TYPE_IMPLICIT, LAYER_TYPE_CUSTOM } LayerType;

typedef enum {
    LAYER_SETTINGS_STRING = 0,
    LAYER_SETTINGS_FILE,
    LAYER_SETTINGS_SAVE_FOLDER,
    LAYER_SETTINGS_BOOL,
    LAYER_SETTINGS_BOOL_NUMERIC,
    LAYER_SETTINGS_EXCLUSIVE_LIST,
    LAYER_SETTINGS_INCLUSIVE_LIST,
    LAYER_SETTINGS_RANGE_INT,
    LAYER_SETTINGS_VUID_FILTER,
} LayerSettingsType;

// This structure is copied by assignment elsewhere, so do not add
// any pointers to it please...
struct LayerSettings {
    QString settingsName;                     // Name of the setting the layer looks for (programatic variable name)
    QString settingsPrompt;                   // Short name to prompt end user
    QString settingsDesc;                     // Human version, describes the setting
    LayerSettingsType settingsType;          // The data type
    QVariant settingsMaxValue;                // For range based
    QVariant settingsMinValue;                // For range based
    QStringList settingsListExclusiveValue;   // List of exclusive items
    QStringList settingsListExclusivePrompt;  // List of exclusive item prompts
    QStringList settingsListInclusiveValue;   // List of non-exclusive items (more than one item can be selected)
    QStringList settingsListInclusivePrompt;  // List of non-exclusive item prompts (more than one item can be selected)
    QString settingsValue;                    // Default value as a string
};

void RemoveString(QString& delimitedString, QString value);
void AddString(QString& delimitedString, QString value);

class LayerFile : public QObject {
    Q_OBJECT
   public:
    // Standard pieces of a layer
    QString file_format_version;
    QString name;
    QString type;
    QString library_path;  // This is a relative path, straight out of the json
    QString api_version;
    QString implementation_version;
    QString description;

    QString layer_path;  // Actual path to the folder that contains the layer (this is important!)
    LayerType layer_type;

    // This layers settings. This will be used to build the editor
    // as well as create settings files. This CAN be empty if the
    // layer doens't have any settings.
    QVector<LayerSettings*> layer_settings;

    bool enabled;    // When used in a profile, is this one active?
    bool disabled;  // When used in a profile, is this one disabled?
    int rank;       // When used in a profile, what is the rank? (0 being first layer)

   public:
    LayerFile();
    ~LayerFile();

    // No.. I do not like operator overloading. It's a bad idea.
    // Inlined here just so I can see all the variables that need to be copied.
    void CopyLayer(LayerFile* destinationLayer) const {
        destinationLayer->file_format_version = file_format_version;
        destinationLayer->name = name;
        destinationLayer->type = type;
        destinationLayer->library_path = library_path;
        destinationLayer->api_version = api_version;
        destinationLayer->implementation_version = implementation_version;
        destinationLayer->description = description;
        destinationLayer->layer_type = layer_type;
        destinationLayer->enabled = enabled;
        destinationLayer->rank = rank;
        destinationLayer->disabled = disabled;
        destinationLayer->layer_path = layer_path;

        for (int i = 0; i < layer_settings.length(); i++) {
            LayerSettings* pSettings = new LayerSettings();
            *pSettings = *layer_settings[i];
            destinationLayer->layer_settings.push_back(pSettings);
        }
    }

    // File based layers
    bool ReadLayerFile(QString full_path_to_file, LayerType layer_type);

    // Utility, may move outside this class....
    static void LoadSettingsFromJson(QJsonObject& layer_settings_descriptors, QVector<LayerSettings*>& layers);
};
