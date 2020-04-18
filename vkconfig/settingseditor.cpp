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
 * Author: Richard S. Wright Jr. <richard@lunarg.com>
 */

#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QPushButton>
#include <QComboBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStandardItemModel>
#include <QFileDialog>

#include "settingseditor.h"

CSettingsEditor::CSettingsEditor()
    {
    pEditArea = nullptr;
    pApplyButton = nullptr;
    inputControls.reserve(100);
    prompts.reserve(100);
    }


///////////////////////////////////////////////////////////////////////////////////////////////
// Creates controls and sets up any signals
void CSettingsEditor::CreateGUI(QScrollArea *pDestination, QVector<TLayerSettings *>& layerSettings, bool bApplyButton)
    {
    int nRowHeight = 24;
    int nVerticalPad = 10;
    int nCurrRow = 15;
    int nLeftColumn = 10;
    int nSecondColumn = 90;
    int nEditFieldWidth = 220;

    pEditArea = new QWidget();
    pEditArea->setMinimumSize(QSize(450, 1024)); //(nRowHeight * (layerSettings.size()+2))));
    pDestination->setWidget(pEditArea);
    pEditArea->show();

//    typedef enum { LAYER_SETTINGS_STRING = 0, LAYER_SETTINGS_FILE, LAYER_SETTINGS_INT, LAYER_SETTINGS_FLOAT, LAYER_SETTINGS_BOOL,
//                    LAYER_SETTINGS_EXCLUSIVE_LIST, LAYER_SETTINGS_INCLUSIVE_LIST, LAYER_SETTINGS_RANGE_INT } TLayerSettingsType;

//    hashTable.insert("string", LAYER_SETTINGS_STRING);
//    hashTable.insert("save_file", LAYER_SETTINGS_FILE);
//    hashTable.insert("bool", LAYER_SETTINGS_BOOL);
//    hashTable.insert("enum", LAYER_SETTINGS_EXCLUSIVE_LIST);
//    hashTable.insert("multi_enum", LAYER_SETTINGS_INCLUSIVE_LIST);

    for(int iSetting = 0; iSetting < layerSettings.size(); iSetting++) {
        // Do not display read only settings.
        if(layerSettings[iSetting]->readOnly)
            continue;

        // Prompt doesn't matter what the data type is
        QLabel *pPromptLabel = new QLabel(pEditArea);
        pPromptLabel->setText(layerSettings[iSetting]->settingsPrompt);
        pPromptLabel->setToolTip(layerSettings[iSetting]->settingsDesc);
        pPromptLabel->setGeometry(nLeftColumn, nCurrRow, nSecondColumn, nRowHeight);
        pPromptLabel->show();
        prompts.push_back(pPromptLabel);

        switch(layerSettings[iSetting]->settingsType)
            {
            case LAYER_SETTINGS_STRING:{
                QLineEdit *pField = new QLineEdit(pEditArea);
                pField->setText(layerSettings[iSetting]->settingsValue);
                pField->setGeometry(nSecondColumn, nCurrRow, nEditFieldWidth, nRowHeight);
                pField->setToolTip(layerSettings[iSetting]->settingsDesc);
                pField->show();
                inputControls.push_back(pField);
                linkedSetting.push_back(layerSettings[iSetting]);
                break;
                }

            case LAYER_SETTINGS_BOOL: {
                QWidget *pContainer = new QWidget(pEditArea);
                pContainer->setGeometry(nSecondColumn, nCurrRow, 250, nRowHeight);

                QRadioButton *pTrue = new QRadioButton(pContainer);
                pTrue->setText("True");
                pTrue->setGeometry(0, 0, 100, nRowHeight);
                pTrue->setToolTip(layerSettings[iSetting]->settingsDesc);
                pTrue->show();
                inputControls.push_back(pTrue);
                linkedSetting.push_back(layerSettings[iSetting]);

                QRadioButton *pFalse = new QRadioButton(pContainer);
                pFalse->setText("False");
                pFalse->setGeometry(60, 0, 100, nRowHeight);
                pFalse->setToolTip(layerSettings[iSetting]->settingsDesc);
                pFalse->show();

                if(layerSettings[iSetting]->settingsValue == QString("true"))
                    pTrue->setChecked(true);
                else
                    pFalse->setChecked(true);

                pContainer->show();
                inputControls.push_back(pFalse);
                linkedSetting.push_back(layerSettings[iSetting]);
                break;
                }

        case LAYER_SETTINGS_FILE: {
                pButtonBuddy = new QLineEdit(pEditArea);
                pButtonBuddy->setText(layerSettings[iSetting]->settingsValue);
                pButtonBuddy->setGeometry(nSecondColumn, nCurrRow, nEditFieldWidth, nRowHeight);
                pButtonBuddy->setToolTip(layerSettings[iSetting]->settingsDesc);
                pButtonBuddy->show();
                inputControls.push_back(pButtonBuddy);
                linkedSetting.push_back(layerSettings[iSetting]);

                pBrowseButton = new QPushButton(pEditArea);
                pBrowseButton->setText("Browse...");
                pBrowseButton->setGeometry(nSecondColumn + nEditFieldWidth + 16, nCurrRow-2, 100, nRowHeight+1);
                connect(pBrowseButton, SIGNAL(pressed()), this, SLOT(browseButtonPressed()));
                pBrowseButton->show();
                inputControls.push_back(pBrowseButton);
                linkedSetting.push_back(layerSettings[iSetting]);
                break;
                }

           case  LAYER_SETTINGS_EXCLUSIVE_LIST: {
                QComboBox *pComboBox = new QComboBox(pEditArea);
                pComboBox->setGeometry(nSecondColumn, nCurrRow, nEditFieldWidth, nRowHeight);

                // Populate with the user readable values. The default needs to be found as well,
                // so search for it while we popluate the control. The default stored is the actual
                // value, not what is displayed to the user.
                int nFoundDefault = 0;
                pComboBox->addItem("<None>");
                for(int p = 0; p < layerSettings[iSetting]->settingsListExclusivePrompt.size(); p++) {
                    pComboBox->addItem(layerSettings[iSetting]->settingsListExclusivePrompt[p]);
                    if(layerSettings[iSetting]->settingsValue == layerSettings[iSetting]->settingsListExclusiveValue[p])
                        nFoundDefault = p+1;
                    }

                pComboBox->setCurrentIndex(nFoundDefault);
                pComboBox->setToolTip(layerSettings[iSetting]->settingsDesc);
                pComboBox->show();
                inputControls.push_back(pComboBox);
                linkedSetting.push_back(layerSettings[iSetting]);
                break;
                }

            case LAYER_SETTINGS_INCLUSIVE_LIST: {   // List widget with checkboxes
                QListWidget *pListBox = new QListWidget(pEditArea);

                for(int i = 0; i < layerSettings[iSetting]->settingsListInclusiveValue.length(); i++) {
                    QListWidgetItem *pItem = new QListWidgetItem();
                    pItem->setText(layerSettings[iSetting]->settingsListInclusivePrompt[i]);

                    // If this item is in the list, check it
                    if(layerSettings[iSetting]->settingsValue.contains(layerSettings[iSetting]->settingsListInclusiveValue[i]))
                        pItem->setCheckState(Qt::Checked);
                    else
                        pItem->setCheckState(Qt::Unchecked);

                    pListBox->addItem(pItem);
                    }

                int nElementHeight = /*pListBox->sizeHintForRow(0)*/ nRowHeight * layerSettings[iSetting]->settingsListInclusiveValue.length();
                pListBox->setGeometry(nSecondColumn, nCurrRow, nEditFieldWidth, nElementHeight);
                pListBox->setToolTip(layerSettings[iSetting]->settingsDesc);
                pListBox->show();

                inputControls.push_back(pListBox);
                linkedSetting.push_back(layerSettings[iSetting]);
                nCurrRow += nElementHeight;
                break;
                }


            default: {
                QLabel *pLabel = new QLabel(pEditArea);
                pLabel->setText(QString().sprintf("Unhandled setting type: %d", layerSettings[iSetting]->settingsType));
                pLabel->setGeometry(nSecondColumn, nCurrRow, 200, nRowHeight);
                pLabel->setToolTip(layerSettings[iSetting]->settingsDesc);
                pLabel->show();
                prompts.push_back(pLabel);
                }
            }

        nCurrRow += nRowHeight;
        nCurrRow += nVerticalPad;
        }

    if(bApplyButton) {
        pApplyButton = new QPushButton(pEditArea);
        pApplyButton->setText(tr("Apply Now"));
        pApplyButton->setGeometry(nLeftColumn, nCurrRow, nEditFieldWidth, nRowHeight);
        pApplyButton->show();
        }
    }


///////////////////////////////////////////////////////////////////////////////
/// \brief CSettingsEditor::CollectSettings
/// \return
/// Harvest the values from the edit controls and move them to the stored
/// pointers to the settings data. Returns false if no settings were actually
/// changed.
bool CSettingsEditor::CollectSettings()
    {
    bool bDirty = false;    // Any single field change flips this

    // Step through all the edit controls and settings in parallel
    for(int iSetting = 0; iSetting < linkedSetting.size(); iSetting++)
        {
        switch(linkedSetting[iSetting]->settingsType)
            {
            case LAYER_SETTINGS_STRING: { // These are all edit controls
                QLineEdit *pEdit = dynamic_cast<QLineEdit*>(inputControls[iSetting]);
                Q_ASSERT(pEdit != nullptr);

                if(linkedSetting[iSetting]->settingsValue != pEdit->text()) {
                    linkedSetting[iSetting]->settingsValue = pEdit->text();
                    bDirty = true;
                   }
                }
            break;

            case LAYER_SETTINGS_FILE: { // Edit control followed by a button
                QLineEdit *pEdit = dynamic_cast<QLineEdit *>(inputControls[iSetting]);
                Q_ASSERT(pEdit != nullptr);

                if(linkedSetting[iSetting]->settingsValue != pEdit->text()) {
                    linkedSetting[iSetting]->settingsValue = pEdit->text();
                    bDirty = true;
                   }
                iSetting++; // Skip the button control which is next
                }
            break;

            case LAYER_SETTINGS_EXCLUSIVE_LIST: {  // Combobox. One selection
                QComboBox *pCombo = dynamic_cast<QComboBox *>(inputControls[iSetting]);
                Q_ASSERT(pCombo != nullptr);
                int nSelected = pCombo->currentIndex();
                QString newValue;
                if(nSelected == 0)
                    newValue = "";
                else
                    newValue = linkedSetting[iSetting]->settingsListExclusiveValue[nSelected-1];

                if(linkedSetting[iSetting]->settingsValue != newValue) {
                    linkedSetting[iSetting]->settingsValue = newValue;
                    bDirty = true;
                    }
                }
            break;


            case LAYER_SETTINGS_INCLUSIVE_LIST:     // Listwidget with checked items
                {
                QListWidget *pList = dynamic_cast<QListWidget *>(inputControls[iSetting]);
                Q_ASSERT(pList != nullptr);
                QString newSetting;
                for(int i = 0; i < pList->count(); i++) {
                    QListWidgetItem *pItem = pList->item(i);
                    if(pItem->checkState() == Qt::Checked) { // Add the item
                        if(!newSetting.isEmpty())
                            newSetting += ",";

                        newSetting += linkedSetting[iSetting]->settingsListInclusiveValue[i];
                        }
                    }

                if(newSetting != linkedSetting[iSetting]->settingsValue) {
                    linkedSetting[iSetting]->settingsValue = newSetting;
                    bDirty = true;
                    }
                }
            break;

            case LAYER_SETTINGS_BOOL: { // True or false
                QRadioButton *pTrueButton = dynamic_cast<QRadioButton *>(inputControls[iSetting]);
                Q_ASSERT(pTrueButton != nullptr);
                QString curVal = (pTrueButton->isChecked()) ? QString("true") : QString("false");

                if(curVal != linkedSetting[iSetting]->settingsValue) {
                    linkedSetting[iSetting]->settingsValue = curVal;
                    bDirty = true;
                    }
                iSetting++; // Two controls, skip the second redundant one
                }
            break;

            default: // Integer range, TBD...
            break;
            }
        }


    return bDirty;
    }

///////////////////////////////////////////////////////////////////////////////
// Okay, remove the control, disconnect any signals and free the memory up.
void CSettingsEditor::CleanupGUI(void)
    {
    // Don't delete the controls, they are parented by pEditArea
    inputControls.clear();
    prompts.clear();
    linkedSetting.clear();

    delete pEditArea;
    pEditArea = nullptr;
    pApplyButton = nullptr;
    }

///////////////////////////////////////////////////////////////////////////////
/// \brief CSettingsEditor::browseButtonPressed
/// A field (there is only one currently) that allows browsing has been pressed.
/// Allow the user to set a file, and pop that into the associated
/// edit field.
void CSettingsEditor::browseButtonPressed()
    {
    QString file = QFileDialog::getSaveFileName(pEditArea,
        tr("Auto Save Output To..."),
        ".", tr("Log text(*.txt)"));

    if(!file.isEmpty())
        pButtonBuddy->setText(file);
    }

