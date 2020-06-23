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

#include <QProcess>
#include <QDir>
#include <QMessageBox>
#include <QFile>
#include <QFrame>
#include <QComboBox>
#include <QVariant>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QLineEdit>
#include <QDesktopServices>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "dlgabout.h"
#include "dlgvulkananalysis.h"
#include "dlgvulkaninfo.h"
#include "dlgprofileeditor.h"
#include "dlgcreateassociation.h"
#include "dlgcustompaths.h"
#include "profiledef.h"

// This is what happens when programmers can touch type....
bool bBeenWarnedAboutOldLoader = false;

#define EDITOR_CAPTION_EMPTY "Configuration Layer Settings"

static const int LAUNCH_COLUMN0_SIZE = 240;
static const int LAUNCH_COLUMN2_SIZE = 32;
static const int LAUNCH_SPACING_SIZE = 2;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    ui->launchTree->installEventFilter(this);
    ui->profileTree->installEventFilter(this);

    pLastSelectedProfileItem = nullptr;
    pVKVia = nullptr;
    pVulkanInfo = nullptr;
    pTestEnv = nullptr;
    pDlgHelp = nullptr;
    pVulkanApp = nullptr;
    pLogFile = nullptr;
    pLaunchAppsCombo = nullptr;
    pLaunchArguments = nullptr;

    // This loads all the layer information and current settings.
    pVulkanConfig = CVulkanConfiguration::getVulkanConfig();

    ///////////////////////////////////////////////

    // We need to resetup the new profile for consistency sake.
    QSettings settings;
    QString lastProfile = settings.value(VKCONFIG_KEY_ACTIVEPROFILE, QString("Validation - Standard")).toString();
    CProfileDef *pCurrentProfile = pVulkanConfig->FindProfile(lastProfile);
    if (pVulkanConfig->bOverrideActive) ChangeActiveProfile(pCurrentProfile);

    LoadProfileList();
    SetupLaunchTree();

    connect(ui->actionAbout, SIGNAL(triggered(bool)), this, SLOT(aboutVkConfig(bool)));
    connect(ui->actionVulkan_Info, SIGNAL(triggered(bool)), this, SLOT(toolsVulkanInfo(bool)));
    connect(ui->actionHelp, SIGNAL(triggered(bool)), this, SLOT(helpShowHelp(bool)));

    connect(ui->actionCustom_Layer_Paths, SIGNAL(triggered(bool)), this, SLOT(toolsSetCustomPaths(bool)));

    connect(ui->actionVulkan_Installation, SIGNAL(triggered(bool)), this, SLOT(toolsVulkanInstallation(bool)));
    connect(ui->actionSet_Test_Application, SIGNAL(triggered(bool)), this, SLOT(toolsVulkanTestApp(bool)));
    connect(ui->actionLog_API_Dump, SIGNAL(triggered(bool)), this, SLOT(toolsVulkanAPIDump(bool)));
    connect(ui->actionRestore_Default_Configurations, SIGNAL(triggered(bool)), this, SLOT(toolsResetToDefault(bool)));

    connect(ui->profileTree, SIGNAL(itemChanged(QTreeWidgetItem *, int)), this, SLOT(profileItemChanged(QTreeWidgetItem *, int)));
    connect(ui->profileTree, SIGNAL(currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *)), this,
            SLOT(profileTreeChanged(QTreeWidgetItem *, QTreeWidgetItem *)));

    connect(ui->layerSettingsTree, SIGNAL(itemExpanded(QTreeWidgetItem *)), this, SLOT(editorExpanded(QTreeWidgetItem *)));

    connect(ui->launchTree, SIGNAL(itemCollapsed(QTreeWidgetItem *)), this, SLOT(launchItemCollapsed(QTreeWidgetItem *)));
    connect(ui->launchTree, SIGNAL(itemExpanded(QTreeWidgetItem *)), this, SLOT(launchItemExpanded(QTreeWidgetItem *)));

    if (pVulkanConfig->bOverrideActive) {
        ui->radioOverride->setChecked(true);
        ui->checkBoxApplyList->setEnabled(true);
        ui->checkBoxPersistent->setEnabled(true);
        ui->checkBoxApplyAll->setEnabled(true);
    } else {
        ui->radioFully->setChecked(true);
        ui->checkBoxApplyList->setEnabled(false);
        ui->checkBoxPersistent->setEnabled(false);
        ui->pushButtonAppList->setEnabled(false);
        ui->checkBoxApplyAll->setEnabled(false);
    }

    ui->pushButtonAppList->setEnabled(pVulkanConfig->bApplyOnlyToList);
    ui->checkBoxApplyList->setChecked(pVulkanConfig->bApplyOnlyToList);
    ui->checkBoxPersistent->setChecked(pVulkanConfig->bKeepActiveOnExit);

    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());

    // All else is done, highlight and activeate the current profile on startup
    CProfileDef *pActive = pVulkanConfig->GetCurrentActiveProfile();
    if (pActive != nullptr) {
        for (int i = 0; i < ui->profileTree->topLevelItemCount(); i++) {
            CProfileListItem *pItem = dynamic_cast<CProfileListItem *>(ui->profileTree->topLevelItem(i));
            if (pItem != nullptr)
                if (pItem->pProfilePointer == pActive) {  // Ding ding ding... we have a winner
                    ui->profileTree->setCurrentItem(pItem);
                }
        }
    }
}

MainWindow::~MainWindow() {
    delete pTestEnv;
    delete ui;
}

///////////////////////////////////////////////////////////////////////////////
// Load or refresh the list of profiles
void MainWindow::LoadProfileList(void) {
    // There are lots of ways into this, and in none of them
    // can we have an active editor running.
    settingsTreeManager.CleanupGUI();
    ui->profileTree->blockSignals(true);  // No signals firing off while we do this
    ui->profileTree->clear();

    // Default profiles need the VK_LAYER_KHRONOS_validation layer.
    // If it's not found, we need to disable it.
    bool bKhronosAvailable = (nullptr != pVulkanConfig->FindLayerNamed("VK_LAYER_KHRONOS_validation"));

    // Who is the currently active profile?
    QSettings settings;
    QString activeProfileName = settings.value(VKCONFIG_KEY_ACTIVEPROFILE).toString();

    for (int i = 0; i < pVulkanConfig->profileList.size(); i++) {
        // Add to list
        CProfileListItem *pItem = new CProfileListItem();
        pItem->pProfilePointer = pVulkanConfig->profileList[i];
        ui->profileTree->addTopLevelItem(pItem);
        pItem->setText(1, pVulkanConfig->profileList[i]->qsProfileName);
        pItem->setToolTip(1, pVulkanConfig->profileList[i]->qsDescription);
        pItem->pRadioButton = new QRadioButton();
        pItem->pRadioButton->setText("");
        if (activeProfileName == pVulkanConfig->profileList[i]->qsProfileName) pItem->pRadioButton->setChecked(true);

        if (!bKhronosAvailable) pItem->setFlags(pItem->flags() & ~Qt::ItemIsEnabled);

        pItem->setFlags(pItem->flags() | Qt::ItemIsEditable);
        ui->profileTree->setItemWidget(pItem, 0, pItem->pRadioButton);
        // ui->profileTree->setColumnWidth(0, 50);
        connect(pItem->pRadioButton, SIGNAL(clicked(bool)), this, SLOT(profileItemClicked(bool)));
    }

    ui->profileTree->blockSignals(false);
    ChangeActiveProfile(pVulkanConfig->GetCurrentActiveProfile());
    ui->profileTree->setColumnWidth(0, 24);
    ui->profileTree->resizeColumnToContents(1);
}

//////////////////////////////////////////////////////////
/// \brief MainWindow::on_radioFully_clicked
// No override at all, fully controlled by the application
void MainWindow::on_radioFully_clicked(void) {
    ui->checkBoxApplyList->setEnabled(false);
    ui->checkBoxPersistent->setEnabled(false);
    ui->checkBoxApplyAll->setEnabled(false);
    pVulkanConfig->bOverrideActive = false;

    ui->pushButtonAppList->setEnabled(false);

    pVulkanConfig->SaveAppSettings();
    ChangeActiveProfile(nullptr);
}

//////////////////////////////////////////////////////////
/// \brief MainWindow::GetCheckedItem
/// \return
/// Okay, because we are using custom controls, some of
/// the signaling is not happening as expected. So, we cannot
/// always get an accurate answer to the currently selected
/// item, but we do often need to know what has been checked
/// when an event occurs. This unambigously answers that question.
CProfileListItem *MainWindow::GetCheckedItem(void) {
    // Just go through all the top level items
    for (int i = 0; i < ui->profileTree->topLevelItemCount(); i++) {
        CProfileListItem *pItem = dynamic_cast<CProfileListItem *>(ui->profileTree->topLevelItem(i));

        if (pItem != nullptr)
            if (pItem->pRadioButton->isChecked()) return pItem;
    }

    return nullptr;
}

//////////////////////////////////////////////////////////
/// \brief MainWindow::on_radioOverride_clicked
/// Use the active profile as the override
void MainWindow::on_radioOverride_clicked(void) {
    bool bUse = (!pVulkanConfig->bHasOldLoader || !bBeenWarnedAboutOldLoader);
    ui->checkBoxApplyList->setEnabled(bUse);
    ui->pushButtonAppList->setEnabled(bUse && pVulkanConfig->bApplyOnlyToList);

    ui->checkBoxApplyAll->setEnabled(true);
    ui->checkBoxPersistent->setEnabled(true);
    pVulkanConfig->bOverrideActive = true;
    pVulkanConfig->SaveAppSettings();

    // This just doesn't work. Make a function to look for the radio button checked.
    CProfileListItem *pProfileItem = GetCheckedItem();
    if (pProfileItem == nullptr)
        ChangeActiveProfile(nullptr);
    else
        ChangeActiveProfile(pProfileItem->pProfilePointer);
}

///////////////////////////////////////////////////////////////////////
// We want to apply to just the app list... hang on there. Doe we have
// the new loader?
void MainWindow::on_checkBoxApplyList_clicked(void) {
    if (pVulkanConfig->bHasOldLoader && !bBeenWarnedAboutOldLoader) {
        uint32_t version = pVulkanConfig->vulkanInstanceVersion;
        QString message;
        message = QString().asprintf(
            "The detected Vulkan Loader version is %d.%d.%d but version 1.2.141 or newer is required in order to apply layers "
            "override to only a selected list of Vulkan applications.\n\n<br><br>"
            "Get the latest Vulkan Runtime from <a href='https://vulkan.lunarg.com/sdk/home'>HERE.</a> to use this feature.",
            VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
        QMessageBox alert(this);
        alert.setTextFormat(Qt::RichText);
        alert.setText(message);
        alert.setIcon(QMessageBox::Warning);
        alert.setWindowTitle(tr("Layers override of a selected list of Vulkan Applications is not available"));
        alert.exec();

        ui->pushButtonAppList->setEnabled(false);
        ui->checkBoxApplyList->setEnabled(false);
        ui->checkBoxApplyList->setChecked(false);
        QString messageToolTip;
        messageToolTip = QString().asprintf(
            "The detected Vulkan loader version is %d.%d.%d but version 1.2.141 or newer is required",
            VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
        ui->checkBoxApplyList->setToolTip(messageToolTip);
        ui->pushButtonAppList->setToolTip(messageToolTip);
        bBeenWarnedAboutOldLoader = true;
    }

    pVulkanConfig->bApplyOnlyToList = ui->checkBoxApplyList->isChecked();
    pVulkanConfig->SaveAppSettings();
    ui->pushButtonAppList->setEnabled(pVulkanConfig->bApplyOnlyToList);
}

//////////////////////////////////////////////////////////
void MainWindow::on_checkBoxPersistent_clicked(void) {
    pVulkanConfig->bKeepActiveOnExit = ui->checkBoxPersistent->isChecked();
    pVulkanConfig->SaveAppSettings();
}

//////////////////////////////////////////////////////////
void MainWindow::toolsResetToDefault(bool bChecked) {
    (void)bChecked;

    // Let make sure...
    QMessageBox msg;
    msg.setIcon(QMessageBox::Warning);
    msg.setWindowTitle(tr("Restoring and Resetting all Layers Configurations to default"));
    msg.setText(
        tr("You are about to delete all the user-defined configurations and resetting all default configurations to their default "
           "state.\n\n"
           "Are you sure you want to continue?"));
    msg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msg.setDefaultButton(QMessageBox::Yes);
    if (msg.exec() == QMessageBox::No) return;

    // Clear the current profile as we may be about to remove it.
    pVulkanConfig->SetCurrentActiveProfile(nullptr);

    // Delete all the *.json files in the storage folder
    QDir dir(pVulkanConfig->qsProfileFilesPath);
    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    dir.setNameFilters(QStringList() << "*.json");
    QFileInfoList profileFiles = dir.entryInfoList();

    // Loop through all the profiles found and remove them
    for (int iProfile = 0; iProfile < profileFiles.size(); iProfile++) {
        QFileInfo info = profileFiles.at(iProfile);
        if (info.absoluteFilePath().contains("applist.json")) continue;
        remove(info.filePath().toUtf8().constData());
    }

    // Reset to recopy from resource file
    QSettings settings;
    settings.setValue(VKCONFIG_KEY_FIRST_RUN, true);

    // Now we need to kind of restart everything
    settingsTreeManager.CleanupGUI();
    pVulkanConfig->LoadAllProfiles();

    // Find the Standard Validation and make it current if we are active
    CProfileDef *pNewActiveProfile = pVulkanConfig->FindProfile(QString("Validation - Standard"));
    if (pVulkanConfig->bOverrideActive) ChangeActiveProfile(pNewActiveProfile);

    LoadProfileList();

    // Active or not, set it in the tree so we can see the settings.
    for (int i = 0; i < ui->profileTree->topLevelItemCount(); i++) {
        CProfileListItem *pItem = dynamic_cast<CProfileListItem *>(ui->profileTree->topLevelItem(i));
        if (pItem != nullptr)
            if (pItem->pProfilePointer == pNewActiveProfile)
                ui->profileTree->setCurrentItem(pItem);
            }

    pVulkanConfig->FindVkCube();
    ResetLaunchOptions();
}

////////////////////////////////////////////////////////////////////////////
/// \brief MainWindow::profileItemClicked
/// \param bChecked
/// Thist signal actually comes from the radio button
void MainWindow::profileItemClicked(bool bChecked) {
    (void)bChecked;
    // Someone just got checked, they are now the current profile
    // This pointer will only be valid if it's one of the elements with
    // the radio button
    CProfileListItem *pProfileItem = GetCheckedItem();
    if (pProfileItem == nullptr) return;

    // Do we go ahead and activate it?
    if (pVulkanConfig->bOverrideActive) ChangeActiveProfile(pProfileItem->pProfilePointer);
}

/////////////////////////////////////////////////////////////////////////////
/// An item has been changed. Check for edit of the items name (profile name)
void MainWindow::profileItemChanged(QTreeWidgetItem *pItem, int nCol) {
    // This pointer will only be valid if it's one of the elements with
    // the radio button
    CProfileListItem *pProfileItem = dynamic_cast<CProfileListItem *>(pItem);
    if (pProfileItem == nullptr) return;

    if (nCol == 1) {  // Profile name
        // We are renaming the file. Just delete the old one and save this
        QString completePath = pVulkanConfig->GetProfilePath();
        completePath += "/";
        completePath += pProfileItem->pProfilePointer->qsFileName;
        remove(completePath.toUtf8().constData());

        pProfileItem->pProfilePointer->qsProfileName = pProfileItem->text(1);
        pProfileItem->pProfilePointer->qsFileName = pProfileItem->text(1) + QString(".json");
        pVulkanConfig->SaveProfile(pProfileItem->pProfilePointer);
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
/// \brief MainWindow::profileTreeChanged
/// \param pCurrent
/// \param pPrevious
/// This gets called with keyboard selections and clicks that do not necessarily
/// result in a radio button change (but it may). So we need to do two checks here, one
/// for the radio button, and one to change the editor/information at lower right.
void MainWindow::profileTreeChanged(QTreeWidgetItem *pCurrent, QTreeWidgetItem *pPrevious) {
    (void)pPrevious;

    // This pointer will only be valid if it's one of the elements with
    // the radio button
    CProfileListItem *pProfileItem = dynamic_cast<CProfileListItem *>(pCurrent);
    if (pProfileItem == nullptr) return;

    settingsTreeManager.CreateGUI(ui->layerSettingsTree, pProfileItem->pProfilePointer);
    QString title = pProfileItem->pProfilePointer->qsProfileName;
    title += " Settings";
    ui->groupBoxEditor->setTitle(title);
    ui->layerSettingsTree->resizeColumnToContents(0);
}

////////////////////////////////////////////////////
// Unused flag, just display the about Qt dialog
void MainWindow::aboutVkConfig(bool bChecked) {
    (void)bChecked;
    dlgAbout dlg(this);
    dlg.exec();
}

//////////////////////////////////////////////////////////
/// \brief MainWindow::toolsVulkanInfo
/// \param bChecked
/// Create the VulkanInfo dialog if it doesn't already
/// exits & show it.
void MainWindow::toolsVulkanInfo(bool bChecked) {
    (void)bChecked;

    if (pVulkanInfo == nullptr) pVulkanInfo = new dlgVulkanInfo(this);

    pVulkanInfo->RunTool();
}

//////////////////////////////////////////////////////////
/// \brief MainWindow::toolsVulkanInstallation
/// \param bChecked
/// Create the VulkanTools dialog if it doesn't already
/// exist & show it.
void MainWindow::toolsVulkanInstallation(bool bChecked) {
    (void)bChecked;
    if (pVKVia == nullptr) pVKVia = new dlgVulkanAnalysis(this);

    pVKVia->RunTool();
}

/////////////////////////////////////////////////////////////
void MainWindow::toolsVulkanTestApp(bool bChecked) {
    (void)bChecked;
    if (pTestEnv == nullptr) pTestEnv = new dlgLayerOutput(nullptr);

    pTestEnv->show();
    pTestEnv->raise();
    pTestEnv->setFocus();
}

//////////////////////////////////////////////////////////////
void MainWindow::toolsVulkanAPIDump(bool bChecked) {
    (void)bChecked;

    dlgLayerOutput apiDump(this);

    CProfileDef *pAPIDump = pVulkanConfig->LoadProfile(":/resourcefiles/API dump.json");
    pVulkanConfig->pushProfile(pAPIDump);
    apiDump.bAPIDump = true;

    apiDump.exec();
    pVulkanConfig->popProfile();
}

////////////////////////////////////////////////////////////////
/// \brief MainWindow::helpShowHelp
/// \param bChecked
/// Show help, which is just a rich text file
void MainWindow::helpShowHelp(bool bChecked) {
    (void)bChecked;
    if (pDlgHelp == nullptr) pDlgHelp = new dlgHelp(nullptr);

    pDlgHelp->show();
}

////////////////////////////////////////////////////////////////
/// \brief MainWindow::closeEvent
/// \param event
/// The only thing we need to do here is clear the profile if
/// the user does not want it active.
void MainWindow::closeEvent(QCloseEvent *event) {
    if (!pVulkanConfig->bKeepActiveOnExit) pVulkanConfig->SetCurrentActiveProfile(nullptr);

    pVulkanConfig->SaveAppList();
    pVulkanConfig->SaveAppSettings();

    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    QMainWindow::closeEvent(event);
}

////////////////////////////////////////////////////////////////
/// \brief MainWindow::resizeEvent
/// \param pEvent
/// Resizing needs a little help. Yes please, there has to be
/// a better way of doing this.
void MainWindow::resizeEvent(QResizeEvent *pEvent) {
    if (pEvent != nullptr) pEvent->accept();
}

/////////////////////////////////////////////////////////////
void MainWindow::showEvent(QShowEvent *event) {
    //  resizeEvent(nullptr); // Fake to get controls to do the right thing

    event->accept();
}

///////////////////////////////////////////////////////////////////////////////
/// \brief MainWindow::on_pushButtonAppList_clicked
/// Edit the list of apps that can be filtered.
void MainWindow::on_pushButtonAppList_clicked(void) {
    dlgCreateAssociation dlg(this);
    dlg.exec();
    pVulkanConfig->SaveAppList();
    ResetLaunchOptions();
}

///////////////////////////////////////////////////////////////////////////////
/// \brief MainWindow::on_pushButtonEditProfile_clicked
/// Just resave the list anytime we go into the editor
void MainWindow::on_pushButtonEditProfile_clicked() {
    // Who is selected?
    CProfileListItem *pItem = dynamic_cast<CProfileListItem *>(ui->profileTree->currentItem());
    if (pItem == nullptr) return;

    // Save current state before we go in
    settingsTreeManager.CleanupGUI();

    dlgProfileEditor dlg(this, pItem->pProfilePointer);
    dlg.exec();
    // pItem will be invalid after LoadProfileList, but I still
    // need the pointer to the profile
    QString editedProfileName = pItem->pProfilePointer->qsProfileName;
    pVulkanConfig->LoadAllProfiles();
    LoadProfileList();

    // Reset the current item
    for (int i = 0; i < ui->profileTree->topLevelItemCount(); i++) {
        pItem = dynamic_cast<CProfileListItem *>(ui->profileTree->topLevelItem(i));
        if (pItem != nullptr)
            if (pItem->pProfilePointer->qsProfileName == editedProfileName) {
                ui->profileTree->setCurrentItem(pItem);
                //                settingsTreeManager.CreateGUI(ui->layerSettingsTree, pProfile);
                break;
            }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Create a new blank profile
void MainWindow::NewClicked() {
    CProfileDef *pNewProfile = pVulkanConfig->CreateEmptyProfile();
    dlgProfileEditor dlg(this, pNewProfile);
    if (QDialog::Accepted == dlg.exec()) {
        pVulkanConfig->LoadAllProfiles();
        LoadProfileList();
    }
}

///////////////////////////////////////////////////////////////////////////////
/// \brief MainWindow::addCustomPaths
/// Allow addition or removal of custom layer paths. Afterwards reset the list
/// of loaded layers, but only if something was changed.
void MainWindow::addCustomPaths() {
    dlgCustomPaths dlg(this);
    dlg.exec();
    LoadProfileList();  // Force a reload
}

//////////////////////////////////////////////////////////////////////////////
/// Remove the currently selected user defined profile.
void MainWindow::RemoveClicked(CProfileListItem *pItem) {
    // Let make sure...
    QMessageBox msg;
    msg.setInformativeText(pItem->pProfilePointer->qsProfileName);
    msg.setText(tr("Are you sure you want to remove this configuration?"));
    msg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msg.setDefaultButton(QMessageBox::Yes);
    if (msg.exec() == QMessageBox::No) return;

    // What if this is the active profile? We will go boom boom soon...
    if (pVulkanConfig->GetCurrentActiveProfile() == pItem->pProfilePointer) pVulkanConfig->SetCurrentActiveProfile(nullptr);

    // Delete the file
    QString completePath = pVulkanConfig->GetProfilePath();
    completePath += "/";
    completePath += pItem->pProfilePointer->qsFileName;
    remove(completePath.toUtf8().constData());

    // Reload profiles
    pVulkanConfig->LoadAllProfiles();
    LoadProfileList();
}

/////////////////////////////////////////////////////////////////////////////
void MainWindow::RenameClicked(CProfileListItem *pItem) { ui->profileTree->editItem(pItem, 1); }

/////////////////////////////////////////////////////////////////////////////
// Copy the current configuration
void MainWindow::DuplicateClicked(CProfileListItem *pItem) {
    QString qsNewName = pItem->pProfilePointer->qsProfileName;
    qsNewName += "2";
    pItem->pProfilePointer->qsProfileName = qsNewName;
    pVulkanConfig->SaveProfile(pItem->pProfilePointer);
    pVulkanConfig->LoadAllProfiles();
    settingsTreeManager.CleanupGUI();
    LoadProfileList();

    // Good enough? Nope, I want to select it and edit the name.
    // Find it.
    for (int i = 0; i < ui->profileTree->topLevelItemCount(); i++) {
        CProfileListItem *pItem = dynamic_cast<CProfileListItem *>(ui->profileTree->topLevelItem(i));
        if (pItem->pProfilePointer->qsProfileName == qsNewName) {
            ui->profileTree->editItem(pItem, 1);
            return;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// Import a configuration file. File copy followed by a reload.
void MainWindow::ImportClicked(CProfileListItem *pItem) {
    (void)pItem;  // We don't need this
    QString qsGetIt = QFileDialog::getOpenFileName(this, "Import Layers Configuration File", QDir::homePath(), "*.json");
    if (qsGetIt.isEmpty()) return;
    settingsTreeManager.CleanupGUI();
    pVulkanConfig->ImportProfile(qsGetIt);
    LoadProfileList();
}

/////////////////////////////////////////////////////////////////////////////
// Export a configuration file. Basically just a file copy
void MainWindow::ExportClicked(CProfileListItem *pItem) {
    // Where to put it and what to call it
    QString qsSaveIt = QFileDialog::getSaveFileName(this, "Export Layers Configuration File", QDir::homePath(), "*.json");
    if (qsSaveIt.isEmpty()) return;

    // Copy away
    QString fullSourceName = pVulkanConfig->qsProfileFilesPath + "/";
    fullSourceName += pItem->pProfilePointer->qsFileName;
    QFile file(fullSourceName);
    file.copy(qsSaveIt);
}

void MainWindow::toolsSetCustomPaths(bool bChecked) {
    (void)bChecked;
    addCustomPaths();
    LoadProfileList();  // Force a reload
}

/////////////////////////////////////////////////////////////////////////////
/// \brief MainWindow::UpdateActiveDecorations
/// Update "decorations": window caption, (Active) status in list
void MainWindow::ChangeActiveProfile(CProfileDef *pNewProfile) {
    if (pNewProfile == nullptr || !pVulkanConfig->bOverrideActive) {
        pVulkanConfig->SetCurrentActiveProfile(nullptr);
        setWindowTitle("Vulkan Configurator <VULKAN APPLICATION CONTROLLED>");
        return;
    }

    QString newCaption = pNewProfile->qsProfileName;
    newCaption += " - Vulkan Configurator ";
    pVulkanConfig->SetCurrentActiveProfile(pNewProfile);
    newCaption += "<VULKAN APPLICATIONS OVERRIDDEN>";

    this->setWindowTitle(newCaption);
}

void MainWindow::editorExpanded(QTreeWidgetItem *pItem) {
    (void)pItem;
    ui->layerSettingsTree->resizeColumnToContents(0);
}

void MainWindow::profileItemExpanded(QTreeWidgetItem *pItem) {
    (void)pItem;
    ui->layerSettingsTree->resizeColumnToContents(0);
    ui->layerSettingsTree->resizeColumnToContents(1);
}

///////////////////////////////////////////////////////////////////
/// \brief MainWindow::ResetLaunchOptions
/// Reload controls for launch control
void MainWindow::ResetLaunchOptions(void) {
    ui->pushButtonLaunch->setEnabled(pVulkanConfig->appList.size() > 0);

    // Reload launch apps selections
    int nFoundLast = -1;
    pLaunchAppsCombo->blockSignals(true);
    pLaunchAppsCombo->clear();
    for (int i = 0; i < pVulkanConfig->appList.size(); i++) {
        pLaunchAppsCombo->addItem(pVulkanConfig->appList[i]->qsAppNameWithPath);
        if (pVulkanConfig->appList[i]->qsAppNameWithPath == pVulkanConfig->qsLaunchApplicationWPath) nFoundLast = i;
    }

    if (nFoundLast < 0) nFoundLast = 0;

    if (pVulkanConfig->appList.size() == 0) {
        pLaunchArguments->setText("");
        pLaunchWorkingFolder->setText("");
        return;
    }

    pLaunchAppsCombo->setCurrentIndex(nFoundLast);

    // Reset working folder and command line choices
    pLaunchArguments->setText(pVulkanConfig->appList[nFoundLast]->qsArguments);
    pLaunchWorkingFolder->setText(pVulkanConfig->appList[nFoundLast]->qsWorkingFolder);
    pLaunchAppsCombo->blockSignals(false);
}

///////////////////////////////////////////////////////////////////
/// \brief MainWindow::SetupLaunchTree
/// Launch and log area
void MainWindow::SetupLaunchTree(void) {
    /////////////////////////////////////////////////////////////////
    // Executable
    QTreeWidgetItem *pLauncherParent = new QTreeWidgetItem();
    pLauncherParent->setText(0, "Executable Path");
    ui->launchTree->addTopLevelItem(pLauncherParent);

    pLaunchAppsCombo = new QComboBox();
    ui->launchTree->setItemWidget(pLauncherParent, 1, pLaunchAppsCombo);

    pLuanchAppBrowseButton = new QPushButton();
    pLuanchAppBrowseButton->setText("...");
    pLuanchAppBrowseButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    pLuanchAppBrowseButton->setMaximumWidth(LAUNCH_COLUMN2_SIZE);
    ui->launchTree->setItemWidget(pLauncherParent, 2, pLuanchAppBrowseButton);
    connect(pLaunchAppsCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(launchItemChanged(int)));
    connect(pLuanchAppBrowseButton, SIGNAL(clicked()), this, SLOT(launchAddProgram()));

    //////////////////////////////////////////////////////////////////
    // Working folder
    QTreeWidgetItem *pLauncherFolder = new QTreeWidgetItem();
    pLauncherFolder->setText(0, "Working Directory");
    pLauncherParent->addChild(pLauncherFolder);

    pLaunchWorkingFolder = new QLineEdit();
    ui->launchTree->setItemWidget(pLauncherFolder, 1, pLaunchWorkingFolder);
    pLaunchWorkingFolder->setReadOnly(false);

    // Comming soon
    //    pLaunchWorkingFolderButton = new QPushButton();
    //    pLaunchWorkingFolderButton->setText("...");
    //    pLaunchWorkingFolderButton->setMinimumWidth(32);
    //    ui->launchTree->setItemWidget(pLauncherFolder, 2, pLaunchWorkingFolderButton);

    //////////////////////////////////////////////////////////////////
    // Command line arguments
    QTreeWidgetItem *pLauncherCMD = new QTreeWidgetItem();
    pLauncherCMD->setText(0, "Command-line Arguments");
    pLauncherParent->addChild(pLauncherCMD);

    pLaunchArguments = new QLineEdit();
    ui->launchTree->setItemWidget(pLauncherCMD, 1, pLaunchArguments);
    connect(pLaunchArguments, SIGNAL(textEdited(const QString &)), this, SLOT(launchArgsEdited(const QString &)));

    // Comming soon
    //    pButton = new QPushButton();
    //    pButton->setText("...");
    //    ui->launchTree->setItemWidget(pLauncherCMD, 2, pButton);

    //////////////////////////////////////////////////////////////////
    // LOG FILE
    QTreeWidgetItem *pLauncherLogFile = new QTreeWidgetItem();
    pLauncherLogFile->setText(0, "Output Log");
    pLauncherParent->addChild(pLauncherLogFile);

    pLaunchLogFile = new QLineEdit();
    ui->launchTree->setItemWidget(pLauncherLogFile, 1, pLaunchLogFile);
    pLaunchLogFile->setText(pVulkanConfig->qsLogFileWPath);

    pLaunchLogFilebutton = new QPushButton();
    pLaunchLogFilebutton->setText("...");
    pLaunchLogFilebutton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    pLaunchLogFilebutton->setMaximumWidth(LAUNCH_COLUMN2_SIZE);
    ui->launchTree->setItemWidget(pLauncherLogFile, 2, pLaunchLogFilebutton);
    connect(pLaunchLogFilebutton, SIGNAL(clicked()), this, SLOT(launchSetLogFile()));

    //////////////////////////////////////////////////////////////////
    QRect rect = ui->launchTree->visualItemRect(pLauncherParent);
    ui->launchTree->setMinimumHeight(rect.height() * 4);
    ui->launchTree->setMaximumHeight(rect.height() * 4);

    ui->launchTree->setColumnWidth(0, LAUNCH_COLUMN0_SIZE);
    ui->launchTree->setColumnWidth(1, ui->launchTree->rect().width() - LAUNCH_COLUMN0_SIZE - LAUNCH_COLUMN2_SIZE - LAUNCH_SPACING_SIZE);
    ui->launchTree->setColumnWidth(2, LAUNCH_COLUMN2_SIZE);

    ui->launchTree->expandItem(pLauncherParent);
    ui->launchTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->launchTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    ResetLaunchOptions();
}

////////////////////////////////////////////////////////////////////
// Expanding the tree also grows the tree to match
void MainWindow::launchItemExpanded(QTreeWidgetItem *pItem) {
    QRect rect = ui->launchTree->visualItemRect(pItem);
    ui->launchTree->setMinimumHeight((rect.height() * 4));
    ui->launchTree->setMaximumHeight((rect.height() * 4));
}

////////////////////////////////////////////////////////////////////
// Collapsing the tree also shrinks the tree to match and show only
// the first line
void MainWindow::launchItemCollapsed(QTreeWidgetItem *pItem) {
    QRect rect = ui->launchTree->visualItemRect(pItem);
    ui->launchTree->setMinimumHeight(rect.height());
    ui->launchTree->setMaximumHeight(rect.height());
}

/////////////////////////////////////////////////////////////////////
void MainWindow::launchAddProgram(void) {
    dlgCreateAssociation dlg(this);
    dlg.exec();
    pVulkanConfig->SaveAppList();

    ResetLaunchOptions();
}

////////////////////////////////////////////////////////////////////
void MainWindow::launchSetLogFile(void) {
    pVulkanConfig->qsLogFileWPath = QFileDialog::getSaveFileName(this, tr("Set Log File To..."), ".", tr("Log text(*.txt)"));

    pVulkanConfig->qsLogFileWPath = QDir::toNativeSeparators(pVulkanConfig->qsLogFileWPath);

    if (pVulkanConfig->qsLogFileWPath.isEmpty())
        pLaunchLogFile->setText("");
    else
        pLaunchLogFile->setText(pVulkanConfig->qsLogFileWPath);

    pVulkanConfig->SaveAppSettings();
}

////////////////////////////////////////////////////////////////////
/// Launch app change
void MainWindow::launchItemChanged(int nIndex) {
    if (nIndex >= pVulkanConfig->appList.size()) return;

    if (nIndex < 0) return;

    pLaunchArguments->setText(pVulkanConfig->appList[nIndex]->qsArguments);
    pLaunchWorkingFolder->setText(pVulkanConfig->appList[nIndex]->qsWorkingFolder);

    // Update last used settings too. I'm saving this way instead of an index, because
    // in the future there will be more than one set of working directories and argument lists
    pVulkanConfig->qsLaunchApplicationWPath = pVulkanConfig->appList[nIndex]->qsAppNameWithPath;
    pVulkanConfig->qsLaunchApplicationArgs = pVulkanConfig->appList[nIndex]->qsArguments;
    pVulkanConfig->qsLaunchApplicationWorkingDir = pVulkanConfig->appList[nIndex]->qsWorkingFolder;
    pVulkanConfig->SaveAppSettings();
}

/////////////////////////////////////////////////////////////////////
/// \brief MainWindow::launchArgsEdited
/// \param newText
/// New command line arguments. Update them.
void MainWindow::launchArgsEdited(const QString &newText) {
    int nIndex = pLaunchAppsCombo->currentIndex();
    if (nIndex < 0) return;

    pVulkanConfig->qsLaunchApplicationArgs = newText;
    pVulkanConfig->appList[nIndex]->qsArguments = newText;
}

//////////////////////////////////////////////////////////////////////
// Clear the browser window
void MainWindow::on_pushButtonClearLog_clicked(void) { ui->logBrowser->clear(); }

//////////////////////////////////////////////////////////////////////
// Open an existing log file and display it's contents
/*
void MainWindow::on_pushButtonOpenLog_clicked(void) {
    QDesktopServices::openUrl(pVulkanConfig->qsLogFileWPath);
    //    QFile file(pVulkanConfig->qsLogFileWPath);
    //    file.open(QIODevice::ReadOnly | QIODevice::Text);
    //    QString string = file.readAll();
    //    file.close();
    //    ui->logBrowser->setPlainText(string);
}
*/

//////////////////////////////////////////////////////////////////////
bool MainWindow::eventFilter(QObject *target, QEvent *event) {
    // Launch tree does some fancy resizing and since it's down in
    // layouts and splitters, we can't just relay on the resize method
    // of this window.
    if (target == ui->launchTree) {
        if (event->type() == QEvent::Resize) {
            QRect rect = ui->launchTree->rect();
            ui->launchTree->setColumnWidth(0, LAUNCH_COLUMN0_SIZE);
            ui->launchTree->setColumnWidth(1, rect.width() - LAUNCH_COLUMN0_SIZE - LAUNCH_COLUMN2_SIZE - LAUNCH_SPACING_SIZE);
            ui->launchTree->setColumnWidth(2, LAUNCH_COLUMN2_SIZE);
        }
    }

    // Context menus for layer configuration files
    if (target == ui->profileTree) {
        QContextMenuEvent *pRightClick = dynamic_cast<QContextMenuEvent *>(event);
        if (pRightClick && event->type() == QEvent::ContextMenu) {
            // Which item were we over?
            QTreeWidgetItem *pProfileItem = ui->profileTree->itemAt(pRightClick->pos());
            CProfileListItem *pItem = dynamic_cast<CProfileListItem *>(pProfileItem);
            if (pItem != nullptr) {
                // Create context menu here
                QMenu menu(ui->profileTree);
                QAction *pNewAction = new QAction("New Layers Configuration...");
                menu.addAction(pNewAction);

                QAction *pRemoveAction = new QAction("Remove the Layers Configuration");
                menu.addAction(pRemoveAction);

                QAction *pDuplicateAction = new QAction("Duplicate the Layers Configuration");
                menu.addAction(pDuplicateAction);

                menu.addSeparator();

                QAction *pRenameAction = new QAction("Rename the Layers Configuration");
                menu.addAction(pRenameAction);
                menu.addSeparator();

                QAction *pExportAction = new QAction("Export the Layers Configuration...");
                menu.addAction(pExportAction);

                QAction *pImportAction = new QAction("Import a Layers Configuration...");
                menu.addAction(pImportAction);

                QPoint point(pRightClick->globalX(), pRightClick->globalY());
                QAction *pAction = menu.exec(point);

                // Pointer compares made me throw up in my mouth at least a little
                // less than doing a full string compare. Setting up signal/slot for
                // all of these just seemed ridiculous. Every problem is not a nail,
                // put the hammer away....
                // New Profile...
                if (pAction == pNewAction) {
                    settingsTreeManager.CleanupGUI();
                    NewClicked();
                    ui->groupBoxEditor->setTitle(tr(EDITOR_CAPTION_EMPTY));
                    return true;
                }

                // Duplicate
                if (pAction == pDuplicateAction) {
                    settingsTreeManager.CleanupGUI();
                    DuplicateClicked(pItem);
                    settingsTreeManager.CleanupGUI();
                    ui->groupBoxEditor->setTitle(tr(EDITOR_CAPTION_EMPTY));
                    return true;
                }

                // Remove this profile....
                if (pAction == pRemoveAction) {
                    settingsTreeManager.CleanupGUI();
                    RemoveClicked(pItem);
                    ui->groupBoxEditor->setTitle(tr(EDITOR_CAPTION_EMPTY));
                    return true;
                }

                // Rename this profile...
                if (pAction == pRenameAction) {
                    RenameClicked(pItem);
                    settingsTreeManager.CleanupGUI();
                    ui->groupBoxEditor->setTitle(tr(EDITOR_CAPTION_EMPTY));
                    return true;
                }

                // Export this profile (copy the .json)
                if (pAction == pExportAction) {
                    settingsTreeManager.CleanupGUI();
                    ExportClicked(pItem);
                    ui->groupBoxEditor->setTitle(tr(EDITOR_CAPTION_EMPTY));
                    return true;
                }

                // Import a profile (copy a json)
                if (pAction == pImportAction) {
                    settingsTreeManager.CleanupGUI();
                    ImportClicked(pItem);
                    ui->groupBoxEditor->setTitle(tr(EDITOR_CAPTION_EMPTY));
                    return true;
                }
            }

            // Do not pass on
            return true;
        }
    }

    // Pass it on
    return false;
}

///////////////////////////////////////////////////////////////////////////////////
// TBD... Toggles writing from HKEY_CURRENT_USER to HKEY_LOCAL_MACHINE
void MainWindow::on_checkBoxApplyAll_clicked(void) {
    QMessageBox alert(this);
    alert.setText("This will toggle having overrides apply to all users, or just the current user.");
    alert.setIcon(QMessageBox::Warning);
    alert.setWindowTitle(tr("TBD: To Be Done..."));
    alert.exec();
}

///////////////////////////////////////////////////////////////////////////////////
/// Launch the app and monitor it's stdout to get layer output.
/// stdout is buffered by default, so in order to see realtime output it must
/// be flushed. Either of the following in the other app will do.
/// > fflush(stdout);    // Flush now
/// setlinebuf(stdout);  // always flush at the end of a line
///
/// The layers are automtically flushed, so they should show up as they
/// generated. One note... any other stdout generated by the monitored
/// application will also be captured.
///
/// If logging is enbabled (by setting a logging file), then the log file
/// is also opened.
void MainWindow::on_pushButtonLaunch_clicked(void) {
    // Are we already monitoring a running app? If so, terminate it
    if (pVulkanApp != nullptr) {
        pVulkanApp->terminate();
        pVulkanApp->deleteLater();
        pVulkanApp = nullptr;
        ui->pushButtonLaunch->setText(tr("Launch"));
        return;
    }

    // Is there an app selected?
    int nIndex = pLaunchAppsCombo->currentIndex();

    // Launch the test application
    pVulkanApp = new QProcess(this);
    connect(pVulkanApp, SIGNAL(readyReadStandardOutput()), this, SLOT(standardOutputAvailable()));

    connect(pVulkanApp, SIGNAL(readyReadStandardError()), this, SLOT(errorOutputAvailable()));

    connect(pVulkanApp, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processClosed(int, QProcess::ExitStatus)));

    pVulkanApp->setProgram(pVulkanConfig->appList[nIndex]->qsAppNameWithPath);
    pVulkanConfig->qsLaunchApplicationWPath = pVulkanConfig->appList[nIndex]->qsAppNameWithPath;
    pVulkanConfig->qsLaunchApplicationWorkingDir = pVulkanConfig->appList[nIndex]->qsWorkingFolder;
    pVulkanApp->setWorkingDirectory(pVulkanConfig->appList[nIndex]->qsWorkingFolder);

    if (!pVulkanConfig->appList[nIndex]->qsArguments.isEmpty()) {
        pVulkanApp->setArguments(QStringList() << pVulkanConfig->appList[nIndex]->qsArguments);
        pVulkanConfig->qsLaunchApplicationArgs = pVulkanConfig->appList[nIndex]->qsArguments;
    }

    // Some of these may have changed
    pVulkanConfig->SaveAppSettings();

    pVulkanApp->start(QIODevice::ReadOnly | QIODevice::Unbuffered);
    pVulkanApp->setProcessChannelMode(QProcess::MergedChannels);
    pVulkanApp->closeWriteChannel();

    // Display warning for configuration changes
    QSettings settings;
    if (!settings.value("VKCONFIG_HIDE_RESTART_WARNING").toBool()) {
        QMessageBox alert(this);
        alert.setText(
            "Vulkan Layers are fully configured when creating a Vulkan Instance which typically happens at Vulkan Application start.\n\n"
            "For changes to take effect, running Vulkan Applications should be restarted.");
        QCheckBox *pCheckBox = new QCheckBox();
        pCheckBox->setText(DONT_SHOW_AGAIN_MESSAGE);
        alert.setWindowTitle("Any change requires Vulkan Applications restart");
        alert.setCheckBox(pCheckBox);
        alert.setIcon(QMessageBox::Warning);
        alert.exec();
        if (pCheckBox->isChecked()) settings.setValue("VKCONFIG_HIDE_RESTART_WARNING", true);
    }

    // We are logging, let's add that we've launched a new application
    QString outApplication =
        QString().asprintf("Starting Vulkan Application: %s\n", pVulkanConfig->qsLaunchApplicationWPath.toUtf8().constData());
    QString outFolder =
        QString().asprintf("Working folder: %s\n", pVulkanConfig->qsLaunchApplicationWorkingDir.toUtf8().constData());
    QString outArgs =
        QString().asprintf("Command line arguments: %s\n", pVulkanConfig->qsLaunchApplicationArgs.toUtf8().constData());

    if (!pVulkanConfig->qsLogFileWPath.isEmpty()) {
        // This should never happen... but things that should never happen do in
        // fact happen... so just a sanity check.
        if (pLogFile != nullptr) {
            pLogFile->close();
            pLogFile = nullptr;
        }

        if (ui->checkBoxClearOnLaunch->isChecked()) ui->logBrowser->clear();

        // Start logging
        pLogFile = new QFile(pVulkanConfig->qsLogFileWPath);

        // Open and append, or open and truncate?
        QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Text;
        if (!ui->checkBoxClearOnLaunch->isChecked()) mode |= QIODevice::Append;

        if (!pVulkanConfig->qsLogFileWPath.isEmpty()) {
            if (!pLogFile->open(mode)) {
                QMessageBox err;
                err.setText(tr("Warning: Cannot open log file"));
                err.exec();
                delete pLogFile;
                pLogFile = nullptr;
            }
        }

        pLogFile->write(outApplication.toUtf8().constData(), outApplication.length());
        pLogFile->write(outFolder.toUtf8().constData(), outFolder.length());
        pLogFile->write(outArgs.toUtf8().constData(), outArgs.length());
    }

    ui->logBrowser->append(outApplication);
    ui->logBrowser->append(outFolder);
    ui->logBrowser->append(outArgs);

    // Wait... did we start? Give it 4 seconds, more than enough time
    if (!pVulkanApp->waitForStarted(4000)) {
        pVulkanApp->waitForStarted();
        pVulkanApp->deleteLater();
        pVulkanApp = nullptr;

        QString outFailed =
        QString().asprintf("Failed to launch %s!\n", pVulkanConfig->qsLaunchApplicationWPath.toUtf8().constData());
        
        ui->logBrowser->append(outFailed);
        pLogFile->write(outFailed.toUtf8().constData(), outFailed.length());

        return;
    }

    // We are off to the races....
    ui->pushButtonLaunch->setText(tr("Terminate"));

    // No log file is set, just bail
    if (pLaunchLogFile->text() != pVulkanConfig->qsLogFileWPath) {
        pVulkanConfig->qsLogFileWPath = pLaunchLogFile->text();
        pVulkanConfig->SaveAppSettings();
    }
}

/////////////////////////////////////////////////////////////////////////////
/// \brief dlgLayerOutput::processClosed
/// \param exitCode
/// \param status
/// The process we are following is closed. We don't actually care about the
/// exit status/code, we just need to know to destroy the QProcess object
/// and set it back to nullptr so that we know we can launch a new app.
/// Also, if we are logging, it's time to close the log file.
void MainWindow::processClosed(int exitCode, QProcess::ExitStatus status) {
    (void)exitCode;
    (void)status;

    // Not likely, but better to be sure...
    if (pVulkanApp == nullptr) return;

    disconnect(pVulkanApp, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processClosed(int, QProcess::ExitStatus)));

    disconnect(pVulkanApp, SIGNAL(readyReadStandardError()), this, SLOT(errorOutputAvailable()));

    disconnect(pVulkanApp, SIGNAL(readyReadStandardOutput()), this, SLOT(standardOutputAvailable()));

    ui->pushButtonLaunch->setText(tr("Launch"));

    if (pLogFile) {
        pLogFile->close();
        delete pLogFile;
        pLogFile = nullptr;
    }

    delete pVulkanApp;
    pVulkanApp = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// \brief dlgLayerOutput::standardOutputAvailable
/// This signal get's raised whenever the spawned Vulkan appliction writes
/// to stdout and there is data to be read. The layers flush after all stdout
/// writes, so we should see layer output here in realtime, as we just read
/// the string and append it to the text browser.
/// If a log file is open, we also write the output to the log.
void MainWindow::standardOutputAvailable(void) {
    if (pVulkanApp == nullptr) return;

    QString inFromApp = pVulkanApp->readAllStandardOutput();
    ui->logBrowser->append(inFromApp);

    // Are we logging?
    if (pLogFile) pLogFile->write(inFromApp.toUtf8().constData(), inFromApp.length());
}

///////////////////////////////////////////////////////////////////////////////
void MainWindow::errorOutputAvailable(void) {
    if (pVulkanApp == nullptr) return;

    QString inFromApp = pVulkanApp->readAllStandardError();
    ui->logBrowser->append(inFromApp);

    // Are we logging?
    if (pLogFile) pLogFile->write(inFromApp.toUtf8().constData(), inFromApp.length());
}