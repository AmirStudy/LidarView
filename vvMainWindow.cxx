// Copyright 2013 Velodyne Acoustics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// Copyright 2013 Velodyne Acoustics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "vvMainWindow.h"
#include "ui_vvMainWindow.h"
#include "vvLoadDataReaction.h"
#include "vvToggleSpreadSheetReaction.h"

#include <vtkSMProxyManager.h>
#include <vtkSMSessionProxyManager.h>

#include "pqObjectPickingBehavior.h"
#include <pqActiveObjects.h>
#include <pqApplicationCore.h>
#include <pqAutoLoadPluginXMLBehavior.h>
#include <pqCommandLineOptionsBehavior.h>
#include <pqCrashRecoveryBehavior.h>
#include <pqDataTimeStepBehavior.h>
#include <pqDefaultViewBehavior.h>
#include <pqApplyBehavior.h>
#include <pqInterfaceTracker.h>
#include <pqObjectBuilder.h>
#include <pqPersistentMainWindowStateBehavior.h>
#include <pqPythonShellReaction.h>
#include <pqQtMessageHandlerBehavior.h>
#include <pqRenderView.h>
#include <pqRenderViewSelectionReaction.h>
#include <pqDeleteReaction.h>
#include <pqHelpReaction.h>
#include <pqServer.h>
#include <pqSettings.h>
#include <pqSpreadSheetView.h>
#include <pqSpreadSheetViewDecorator.h>
#include <pqSpreadSheetVisibilityBehavior.h>
#include <pqStandardPropertyWidgetInterface.h>
#include <pqStandardViewFrameActionsImplementation.h>
#include <pqVelodyneManager.h>
#include <pqParaViewMenuBuilders.h>
#include <pqTabbedMultiViewWidget.h>
#include <vtkPVPlugin.h>
#include <vtkSMPropertyHelper.h>

#include <QLabel>
#include <QSplitter>
#include <QToolBar>
#include <QShortcut>
#include <QDockWidget>
#include <QMenuBar>
#include <QMenu>

#include <cassert>
#include <iostream>
#include <sstream>

#include "vvConfig.h"

// Declare the plugin to load.
PV_PLUGIN_IMPORT_INIT(VelodyneHDLPlugin);
PV_PLUGIN_IMPORT_INIT(PythonQtPlugin);

class vvMainWindow::pqInternals
{
public:
  pqInternals(vvMainWindow* window)
    : Ui()
    , MainView(0)
  {
    this->Ui.setupUi(window);
    this->paraviewInit(window);
    this->setupUi(window);

    QActionGroup* dualReturnFilterActions = new QActionGroup(window);
    dualReturnFilterActions->addAction(this->Ui.actionDualReturnModeDual);
    dualReturnFilterActions->addAction(this->Ui.actionDualReturnDistanceNear);
    dualReturnFilterActions->addAction(this->Ui.actionDualReturnDistanceFar);
    dualReturnFilterActions->addAction(this->Ui.actionDualReturnIntensityHigh);
    dualReturnFilterActions->addAction(this->Ui.actionDualReturnIntensityLow);

    window->show();
    window->raise();
    window->activateWindow();
  }
  Ui::vvMainWindow Ui;
  pqRenderView* MainView;

private:
  void paraviewInit(vvMainWindow* window)
  {
    pqApplicationCore* core = pqApplicationCore::instance();

    // Register ParaView interfaces.
    pqInterfaceTracker* pgm = core->interfaceTracker();
    //    pgm->addInterface(new pqStandardViewModules(pgm));
    pgm->addInterface(new pqStandardPropertyWidgetInterface(pgm));
    pgm->addInterface(new pqStandardViewFrameActionsImplementation(pgm));

    // Define application behaviors.
    new pqQtMessageHandlerBehavior(window);
    new pqDataTimeStepBehavior(window);
    new pqSpreadSheetVisibilityBehavior(window);
    new pqObjectPickingBehavior(window);
    //    new pqDefaultViewBehavior(window);
    new pqCrashRecoveryBehavior(window);
    new pqAutoLoadPluginXMLBehavior(window);
    new pqCommandLineOptionsBehavior(window);
    pqApplyBehavior* applyBehaviors = new pqApplyBehavior(window);

    // Check if the settings are well formed i.e. if an OriginalMainWindow
    // state was previously saved. If not, we don't want to automatically
    // restore the settings state nor save it on quitting VeloView.
    // An OriginalMainWindow state will be force saved once the UI is completly
    // set up.
    pqSettings* const settings = pqApplicationCore::instance()->settings();
    bool shouldClearSettings = false;
    QStringList keys = settings->allKeys();

    if (keys.size() == 0)
    {
      // There were no settings before, let's save the current state as
      // OriginalMainWindow state
      shouldClearSettings = true;
    }
    else
    {
      // Checks if the existing settings are well formed and if not, clear them.
      // An original MainWindow state will be force saved later once the UI is
      // entirely set up
      for (int keyIndex = 0; keyIndex < keys.size(); ++keyIndex)
      {
        if (keys[keyIndex].contains("OriginalMainWindow"))
        {
          shouldClearSettings = true;
          break;
        }
      }
    }

    if (shouldClearSettings)
    {
      new pqPersistentMainWindowStateBehavior(window);
    }
    else
    {
      if (keys.size() > 0)
      {
        vtkGenericWarningMacro("Settings weren't set correctly. Clearing settings.")
      }

      // As pqPersistentMainWindowStateBehavior is not created right now,
      // we can clear the settings as the current bad state won't be saved on
      // closing VeloView
      settings->clear();
    }

    // Connect to builtin server.
    pqObjectBuilder* builder = core->getObjectBuilder();
    pqServer* server = builder->createServer(pqServerResource("builtin:"));
    pqActiveObjects::instance().setActiveServer(server);

    // Set default render view settings
    vtkSMSessionProxyManager* pxm =
      vtkSMProxyManager::GetProxyManager()->GetActiveSessionProxyManager();
    vtkSMProxy* renderviewsettings = pxm->GetProxy("RenderViewSettings");
    assert(renderviewsettings);

    vtkSMPropertyHelper(renderviewsettings, "ResolveCoincidentTopology").Set(0);

    // Create a overhead view
    pqView* overheadView = builder->createView(pqRenderView::renderViewType(), server);
    overheadView->getProxy()->UpdateVTKObjects();
    this->Ui.overheadViewDock->setWidget(overheadView->widget());
    new vvToggleSpreadSheetReaction(this->Ui.actionOverheadView, overheadView);

    // create SpreadSheet
    pqSpreadSheetView* spreadsheetView = qobject_cast<pqSpreadSheetView*>
        (builder->createView(pqSpreadSheetView::spreadsheetViewType(), server));
    assert(spreadsheetView);
    this->Ui.spreadSheetDock->setWidget(spreadsheetView->widget());
    spreadsheetView->getProxy()->UpdateVTKObjects();
    new vvToggleSpreadSheetReaction(this->Ui.actionSpreadsheet, spreadsheetView);
    pqSpreadSheetViewDecorator* dec = new pqSpreadSheetViewDecorator(spreadsheetView);
    dec->setPrecision(3);
    dec->setFixedRepresentation(true);

    // Create a default view.
    // Due to our old version of paraview, it's not possible to create a view in
    // detachedFromLayout mode. This feature was introduce by paraview 5.2.
    // So we need to create the pqTabbedMultiViewWidget after creating all other view but
    // before creating the main view, in order to have the main view in the pqTabbedMultiViewWidget.
    // This is because all view created are register by the pqTabbedMultiViewWidget.
    if (ENABLE_DEV_MODE_UI_VAR)
    {
      pqTabbedMultiViewWidget* mv = new pqTabbedMultiViewWidget;
      window->setCentralWidget(mv);
    }
    pqRenderView* view =
      qobject_cast<pqRenderView*>(builder->createView(pqRenderView::renderViewType(), server));
    assert(view);
    this->MainView = view;

    vtkSMPropertyHelper(view->getProxy(), "CenterAxesVisibility").Set(0);
    double bgcolor[3] = { 0, 0, 0 };
    vtkSMPropertyHelper(view->getProxy(), "Background").Set(bgcolor, 3);
    // MultiSamples doesn't work, we need to set that up before registering the proxy.
    // vtkSMPropertyHelper(view->getProxy(),"MultiSamples").Set(1);
    view->getProxy()->UpdateVTKObjects();
   if (!ENABLE_DEV_MODE_UI_VAR)
   {
     window->setCentralWidget(view->widget());
   }

    // properties panel
    // connect apply button
    applyBehaviors->registerPanel(this->Ui.propertiesPanel);
    // Enable help from the properties panel.
    QObject::connect(this->Ui.propertiesPanelDock->widget(),
      SIGNAL(helpRequested(const QString&, const QString&)),
      window, SLOT(showHelpForProxy(const QString&, const QString&)));

    /// hook delete to pqDeleteReaction.
    QAction* tempDeleteAction = new QAction(window);
    pqDeleteReaction* handler = new pqDeleteReaction(tempDeleteAction);
    handler->connect(this->Ui.propertiesPanelDock->widget(),
      SIGNAL(deleteRequested(pqPipelineSource*)),
      SLOT(deleteSource(pqPipelineSource*)));

    // specify how corner are occupied by the dockable widget
    window->setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    window->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    window->setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    window->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // organize dockable widget in tab
    window->setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
    window->tabifyDockWidget(this->Ui.propertiesPanelDock, this->Ui.colorMapEditorDock);
    window->tabifyDockWidget(this->Ui.spreadSheetDock, this->Ui.informationDock);
    window->tabifyDockWidget(this->Ui.spreadSheetDock, this->Ui.memoryInspectorDock);

    // hide docker by default
    this->Ui.pipelineBrowserDock->hide();
    this->Ui.propertiesPanelDock->hide();
    this->Ui.colorMapEditorDock->hide();
    this->Ui.spreadSheetDock->hide();
    this->Ui.informationDock->hide();
    this->Ui.memoryInspectorDock->hide();

    // Setup the View menu. This must be setup after all toolbars and dockwidgets
    // have been created.
    pqParaViewMenuBuilders::buildViewMenu(*this->Ui.menuViews, *window);

    if (ENABLE_DEV_MODE_UI_VAR)
    {
      /// If you want to automatically add toolbars for sources as requested in the
      /// configuration pass in a non-null main window.
      QMenu* sourceMenu = window->menuBar()->addMenu(tr("&Sources"));
      pqParaViewMenuBuilders::buildSourcesMenu(*sourceMenu, nullptr);

      /// If you want to automatically add toolbars for filters as requested in the
      /// configuration pass in a non-null main window.
      QMenu* filterMenu = window->menuBar()->addMenu(tr("&Filters"));
      pqParaViewMenuBuilders:: buildFiltersMenu(*filterMenu, nullptr);
    }

      // add 'ctrl+space' shortcut for quickLaunch
      QShortcut *ctrlSpace = new QShortcut(Qt::CTRL + Qt::Key_Space, window);
      QObject::connect(ctrlSpace, SIGNAL(activated()), pqApplicationCore::instance(), SLOT(quickLaunch()));

    pqActiveObjects::instance().setActiveView(view);
  }

  //-----------------------------------------------------------------------------
  void setupUi(vvMainWindow* window)
  {
    new pqRenderViewSelectionReaction(this->Ui.actionSelect_Visible_Points, this->MainView,
      pqRenderViewSelectionReaction::SELECT_SURFACE_POINTS);
    new pqRenderViewSelectionReaction(this->Ui.actionSelect_All_Points, this->MainView,
      pqRenderViewSelectionReaction::SELECT_FRUSTUM_POINTS);

    new pqPythonShellReaction(this->Ui.actionPython_Console);

    pqVelodyneManager::instance()->setup();

    pqSettings* const settings = pqApplicationCore::instance()->settings();
    const QVariant& gridVisible =
      settings->value("VelodyneHDLPlugin/MeasurementGrid/Visibility", true);
    this->Ui.actionMeasurement_Grid->setChecked(gridVisible.toBool());

    this->Ui.actionEnableCrashAnalysis->setChecked(
      settings
        ->value("VelodyneHDLPlugin/MainWindow/EnableCrashAnalysis",
          this->Ui.actionEnableCrashAnalysis->isChecked())
        .toBool());

    new vvLoadDataReaction(this->Ui.actionOpenPcap, false);
    new vvLoadDataReaction(this->Ui.actionOpenApplanix, true);

    connect(this->Ui.actionOpen_Sensor_Stream, SIGNAL(triggered()), pqVelodyneManager::instance(),
      SLOT(onOpenSensor()));

    connect(this->Ui.actionMeasurement_Grid, SIGNAL(toggled(bool)), pqVelodyneManager::instance(),
      SLOT(onMeasurementGrid(bool)));

    connect(this->Ui.actionEnableCrashAnalysis, SIGNAL(toggled(bool)),
      pqVelodyneManager::instance(), SLOT(onEnableCrashAnalysis(bool)));

    connect(this->Ui.actionResetConfigurationFile, SIGNAL(triggered()),
      pqVelodyneManager::instance(), SLOT(onResetCalibrationFile()));

    connect(this->Ui.actionShowErrorDialog, SIGNAL(triggered()), pqApplicationCore::instance(),
      SLOT(showOutputWindow()));
  }
};

//-----------------------------------------------------------------------------
vvMainWindow::vvMainWindow()
  : Internals(new vvMainWindow::pqInternals(this))
{
  pqApplicationCore::instance()->registerManager(
    "COLOR_EDITOR_PANEL", this->Internals->Ui.colorMapEditorDock);
  this->Internals->Ui.colorMapEditorDock->hide();

  PV_PLUGIN_IMPORT(VelodyneHDLPlugin);
  PV_PLUGIN_IMPORT(PythonQtPlugin);

  // Branding
  std::stringstream ss;
  ss << "Reset " << SOFTWARE_NAME << " settings";
  QString text = QString(ss.str().c_str());
  this->Internals->Ui.actionResetConfigurationFile->setText(text);
  ss.str("");
  ss.clear();

  ss << "This will reset all " << SOFTWARE_NAME << " settings by default";
  text = QString(ss.str().c_str());
  this->Internals->Ui.actionResetConfigurationFile->setIconText(text);
  ss.str("");
  ss.clear();

  ss << "About " << SOFTWARE_NAME;
  text = QString(ss.str().c_str());
  this->Internals->Ui.actionAbout_VeloView->setText(text);
  ss.str("");
  ss.clear();

  ss << SOFTWARE_NAME << " Developer Guide";
  text = QString(ss.str().c_str());
  this->Internals->Ui.actionVeloViewDeveloperGuide->setText(text);
  ss.str("");
  ss.clear();

  ss << SOFTWARE_NAME << " User Guide";
  text = QString(ss.str().c_str());
  this->Internals->Ui.actionVeloViewUserGuide->setText(text);
}

//-----------------------------------------------------------------------------
vvMainWindow::~vvMainWindow()
{
  delete this->Internals;
  this->Internals = NULL;
}

//-----------------------------------------------------------------------------
void vvMainWindow::showHelpForProxy(const QString& groupname, const
  QString& proxyname)
{
  pqHelpReaction::showProxyHelp(groupname, proxyname);
}
