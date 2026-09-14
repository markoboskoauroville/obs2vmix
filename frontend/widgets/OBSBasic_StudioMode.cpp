/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
                          Zachary Lund <admin@computerquip.com>
                          Philippe Groarke <philippe.groarke@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSBasic.hpp"
#include "OBSProjector.hpp"

#include <components/SceneStrip.hpp>
#include <components/FxRack.hpp>
#include <utility/SceneRecorder.hpp>
#include <utility/Obs2vmixUpdate.hpp>

#include <utility/display-helpers.hpp>
#include <utility/QuickTransition.hpp>

#include <qt-wrappers.hpp>
#include <slider-ignorewheel.hpp>

#include <QToolTip>
#include <QApplication>
#include <QShortcut>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>
#include <QAbstractButton>
#include <QSpinBox>
#include <QEvent>
#include <QTimer>
#include <QActionGroup>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSplitter>

#include <functional>

#include <algorithm>
#include <cmath>

void OBSBasic::CreateProgramDisplay()
{
	program = new OBSQTDisplay();

	program->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(program.data(), &QWidget::customContextMenuRequested, this, &OBSBasic::ProgramViewContextMenuRequested);

	auto displayResize = [this]() {
		struct obs_video_info ovi;

		if (obs_get_video_info(&ovi)) {
			ResizeProgram(ovi.base_width, ovi.base_height);
		}
	};

	connect(program.data(), &OBSQTDisplay::DisplayResized, this, displayResize);

	auto addDisplay = [this](OBSQTDisplay *window) {
		obs_display_add_draw_callback(window->GetDisplay(), OBSBasic::RenderProgram, this);

		struct obs_video_info ovi;
		if (obs_get_video_info(&ovi)) {
			ResizeProgram(ovi.base_width, ovi.base_height);
		}
	};

	connect(program.data(), &OBSQTDisplay::DisplayCreated, this, addDisplay);

	program->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

#define T_BAR_PRECISION 1024
#define T_BAR_PRECISION_F ((float)T_BAR_PRECISION)
#define T_BAR_CLAMP (T_BAR_PRECISION / 10)

/* a double-click on a monitor, for the single-monitor swap */
class MonitorDoubleClick : public QObject {
	std::function<void()> fn;

public:
	MonitorDoubleClick(QObject *parent, std::function<void()> f) : QObject(parent), fn(std::move(f)) {}

protected:
	bool eventFilter(QObject *obj, QEvent *event) override
	{
		if (event->type() == QEvent::MouseButtonDblClick)
			fn();
		return QObject::eventFilter(obj, event);
	}
};

void OBSBasic::CreateProgramOptions()
{
	/* obs2vmix: this column is kept for the quick transitions and the
	 * T-bar (other code adds its buttons here), but it stays hidden; the
	 * operator's controls live on the seam bar above the monitors. */
	programOptions = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout();
	layout->setSpacing(4);

	QHBoxLayout *quickTransitionsLayout = new QHBoxLayout();
	quickTransitionsLayout->setSpacing(2);

	QPushButton *addQuickTransition = new QPushButton();
	addQuickTransition->setProperty("class", "icon-plus");

	QLabel *quickTransitionsLabel = new QLabel(QTStr("QuickTransitions"));
	quickTransitionsLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	quickTransitionsLayout->addWidget(quickTransitionsLabel);
	quickTransitionsLayout->addWidget(addQuickTransition);

	tBar = new SliderIgnoreClick(Qt::Horizontal);
	tBar->setMinimum(0);
	tBar->setMaximum(T_BAR_PRECISION - 1);

	tBar->setProperty("class", "slider-tbar");

	connect(tBar, &QSlider::valueChanged, this, &OBSBasic::TBarChanged);
	connect(tBar, &QSlider::sliderReleased, this, &OBSBasic::TBarReleased);

	layout->addStretch(0);
	if (!switcherView) {
		/* the OBS view: the classic column with the Transition button */
		QHBoxLayout *mainButtonLayout = new QHBoxLayout();
		mainButtonLayout->setSpacing(2);

		transitionButton = new QPushButton(QTStr("Transition"));
		transitionButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

		QPushButton *configTransitions = new QPushButton();
		configTransitions->setProperty("class", "icon-dots-vert");

		mainButtonLayout->addWidget(transitionButton);
		mainButtonLayout->addWidget(configTransitions);
		layout->addLayout(mainButtonLayout);

		connect(transitionButton.data(), &QAbstractButton::clicked, this, &OBSBasic::TransitionClicked);
		connect(configTransitions, &QAbstractButton::clicked, this, &OBSBasic::ShowTransitionConfigMenu);
	}
	layout->addLayout(quickTransitionsLayout);
	layout->addWidget(tBar);
	layout->addStretch(0);

	programOptions->setLayout(layout);

	auto onAdd = [this]() {
		QScopedPointer<QMenu> menu(CreateTransitionMenu(this, nullptr));
		menu->exec(QCursor::pos());
	};

	connect(addQuickTransition, &QAbstractButton::clicked, this, onAdd);

	if (switcherView)
		programOptions->hide();
}

/* the Studio Mode options: OBS's "..." menu next to its Transition button,
 * a submenu of the Final monitor's menu in the vMix view */
void OBSBasic::FillTransitionConfigMenu(QMenu *menu)
{
	QAction *action;

	auto toggleEditProperties = [this]() {
		editPropertiesMode = !editPropertiesMode;

		OBSSource actualScene = OBSGetStrongRef(programScene);
		if (actualScene) {
			TransitionToScene(actualScene, true);
		}
	};

	auto toggleSwapScenesMode = [this]() {
		swapScenesMode = !swapScenesMode;
	};

	auto toggleSceneDuplication = [this]() {
		sceneDuplicationMode = !sceneDuplicationMode;

		OBSSource actualScene = OBSGetStrongRef(programScene);
		if (actualScene) {
			TransitionToScene(actualScene, true);
		}
	};

	auto showToolTip = [menu]() {
		QAction *act = menu->activeAction();
		if (act)
			QToolTip::showText(QCursor::pos(), act->toolTip(), menu, menu->actionGeometry(act));
	};

	action = menu->addAction(QTStr("QuickTransitions.DuplicateScene"));
	action->setToolTip(QTStr("QuickTransitions.DuplicateSceneTT"));
	action->setCheckable(true);
	action->setChecked(sceneDuplicationMode);
	connect(action, &QAction::triggered, this, toggleSceneDuplication);
	connect(action, &QAction::hovered, action, showToolTip);

	action = menu->addAction(QTStr("QuickTransitions.EditProperties"));
	action->setToolTip(QTStr("QuickTransitions.EditPropertiesTT"));
	action->setCheckable(true);
	action->setChecked(editPropertiesMode);
	action->setEnabled(sceneDuplicationMode);
	connect(action, &QAction::triggered, this, toggleEditProperties);
	connect(action, &QAction::hovered, action, showToolTip);

	action = menu->addAction(QTStr("QuickTransitions.SwapScenes"));
	action->setToolTip(QTStr("QuickTransitions.SwapScenesTT"));
	action->setCheckable(true);
	action->setChecked(swapScenesMode);
	connect(action, &QAction::triggered, this, toggleSwapScenesMode);
	connect(action, &QAction::hovered, action, showToolTip);
}

void OBSBasic::ShowTransitionConfigMenu()
{
	QMenu menu(this);
	FillTransitionConfigMenu(&menu);
	menu.exec(QCursor::pos());
}

/* Space = Take, Enter = Cut, 1-9 = load a scene into Source, Esc = close
 * the editor; only while no text field or button has the focus. The
 * shortcuts belong to a widget of the switcher, so they die with it. */
void OBSBasic::CreateSwitcherKeys(QWidget *owner)
{
	auto key = [this, owner](int k, std::function<void()> fn) {
		QShortcut *sc = new QShortcut(QKeySequence(k), owner, nullptr, nullptr, Qt::WindowShortcut);
		connect(sc, &QShortcut::activated, this, [this, fn]() {
			if (SeamKeyIsFree())
				fn();
		});
	};

	key(Qt::Key_Space, [this]() { SeamTake(); });
	key(Qt::Key_Return, [this]() { SeamCut(); });
	key(Qt::Key_Enter, [this]() { SeamCut(); });
	key(Qt::Key_Escape, [this]() {
		if (seamEditorOpen)
			CloseSceneEditor();
	});
	for (int i = 0; i < 9; i++)
		key(Qt::Key_1 + i, [this, i]() { SeamSelectScene(i); });
}

/* the tail of both monitor menus: one monitor, fullscreen, the OBS view */
void OBSBasic::AddSwitcherCommonActions(QMenu *menu)
{
	menu->addSeparator();

	QAction *one = menu->addAction(QTStr("obs2vmix.Menu.OneMonitor"), this, &OBSBasic::SeamToggleCollapse);
	one->setCheckable(true);
	one->setChecked(seamSingleMonitor);
	one->setEnabled(!seamEditorOpen);

	if (ui->actionFullscreenInterface)
		menu->addAction(ui->actionFullscreenInterface);

	menu->addSeparator();
	menu->addAction(QTStr("obs2vmix.Menu.OBS"), this, [this]() { SetSwitcherView(false); });
}

/* right-click on the Source monitor */
void OBSBasic::SourceViewContextMenu()
{
	QMenu popup(this);
	OBSSource scene = GetCurrentSceneSource();

	/* the first line says which window this is */
	QAction *title = popup.addAction(QString("%1  ·  %2").arg(QTStr("obs2vmix.Menu.SourceTitle"),
								   QT_UTF8(obs_source_get_name(scene))));
	title->setEnabled(false);
	popup.addSeparator();

	popup.addAction(QTStr("obs2vmix.Menu.Take"), this, &OBSBasic::SeamTake);
	popup.addAction(QTStr("obs2vmix.Menu.Cut"), this, &OBSBasic::SeamCut);
	popup.addSeparator();

	QAction *edit = popup.addAction(QTStr("obs2vmix.Menu.EditScene"), this,
					[this, scene]() { OpenSceneEditor(scene); });
	edit->setEnabled(!!scene);

	QAction *rec = popup.addAction(QTStr("obs2vmix.Menu.RecordScene"), this,
				       [this, scene]() { ToggleSceneRecording(scene); });
	rec->setCheckable(true);
	rec->setChecked(scene && FindSceneRecorder(scene));
	rec->setEnabled(!!scene);

	popup.addSeparator();

	QMenu *projector = new QMenu(QTStr("Projector.Open.Preview"), &popup);
	AddProjectorMenuMonitors(projector, this, &OBSBasic::OpenPreviewProjector);
	projector->addSeparator();
	projector->addAction(QTStr("Projector.Window"), this, &OBSBasic::OpenPreviewWindow);
	popup.addMenu(projector);
	popup.addAction(QTStr("Screenshot.Preview"), this, &OBSBasic::ScreenshotScene);

	AddSwitcherCommonActions(&popup);
	popup.exec(QCursor::pos());
}

/* right-click on a thumbnail in the strip */
void OBSBasic::SceneTileMenu(OBSSource scene, const QPoint &pos)
{
	if (!scene)
		return;

	QMenu popup(this);

	QAction *title = popup.addAction(QString("%1  ·  %2")
						 .arg(sceneStrip ? sceneStrip->IndexOf(scene) + 1 : 0)
						 .arg(QT_UTF8(obs_source_get_name(scene))));
	title->setEnabled(false);
	popup.addSeparator();

	popup.addAction(QTStr("obs2vmix.Menu.ToSource"), this, [this, scene]() { SetCurrentScene(scene, false); });
	popup.addAction(QTStr("obs2vmix.Menu.TakeScene"), this, [this, scene]() {
		SetCurrentScene(scene, false);
		SeamTake();
	});
	popup.addSeparator();

	if (seamEditorOpen)
		popup.addAction(QTStr("obs2vmix.Menu.CloseEditor"), this, &OBSBasic::CloseSceneEditor);
	else
		popup.addAction(QTStr("obs2vmix.Menu.EditScene"), this, [this, scene]() { OpenSceneEditor(scene); });

	QAction *rec = popup.addAction(QTStr("obs2vmix.Menu.RecordScene"), this,
				       [this, scene]() { ToggleSceneRecording(scene); });
	rec->setCheckable(true);
	rec->setChecked(FindSceneRecorder(scene) != nullptr);

	popup.exec(pos);
}

/* ---------------------------------------------------------------------- */
/* obs2vmix: monitors, collapse, the scene editor                          */

/* which monitors are shown: both, one (collapsed), or Source alone with
 * the sources dock while a scene is being edited */
void OBSBasic::ApplyMonitorLayout()
{
	if (!IsPreviewProgramMode() || !programWidget)
		return;

	bool showPreview = true, showProgram = true;
	if (seamEditorOpen) {
		showProgram = false;
	} else if (seamSingleMonitor) {
		showPreview = !seamSingleShowsProgram;
		showProgram = seamSingleShowsProgram;
	}

	ui->previewContainer->setVisible(showPreview);
	programWidget->setVisible(showProgram);
}

void OBSBasic::SeamToggleCollapse()
{
	seamSingleMonitor = !seamSingleMonitor;
	if (seamSingleMonitor)
		seamSingleShowsProgram = true;
	ApplyMonitorLayout();
}

/* double-click on the one monitor shown: swap Source <-> Record */
void OBSBasic::SeamSwapMonitor()
{
	if (!seamSingleMonitor || seamEditorOpen)
		return;
	seamSingleShowsProgram = !seamSingleShowsProgram;
	ApplyMonitorLayout();
}

void OBSBasic::OpenSceneEditor(OBSSource scene)
{
	if (!IsPreviewProgramMode() || !scene)
		return;

	SetCurrentScene(scene, false);

	if (!seamEditorOpen) {
		seamEditorOpen = true;
		seamSourcesDockWasVisible = ui->sourcesDock->isVisible();
		ui->sourcesDock->setVisible(true);
		ui->sourcesDock->raise();
	}
	if (sceneStrip)
		sceneStrip->SetEditing(scene);
	ApplyMonitorLayout();
}

void OBSBasic::CloseSceneEditor()
{
	if (!seamEditorOpen)
		return;
	seamEditorOpen = false;
	if (!seamSourcesDockWasVisible)
		ui->sourcesDock->setVisible(false);
	if (sceneStrip)
		sceneStrip->SetEditing(nullptr);
	ApplyMonitorLayout();
}

/* number keys: load scene N into Source */
void OBSBasic::SeamSelectScene(int index)
{
	if (!sceneStrip)
		return;
	OBSSource s = sceneStrip->SceneAt(index);
	if (!s)
		return;
	SetCurrentScene(s, false);
	sceneStrip->ScrollTo(index);
}

void OBSBasic::TogglePreviewProgramMode()
{
	/* obs2vmix: in the vMix view the switcher stays; the OBS view toggles as OBS does */
	SetPreviewProgramMode(switcherView ? true : !IsPreviewProgramMode());
}

/* ---------------------------------------------------------------------- */
/* obs2vmix: the transition                                                */

static const int SEAM_UNIT_FRAMES = 0;
static const int SEAM_UNIT_SECONDS = 1;
static const int SEAM_UNIT_MS = 2;

static double SeamFps()
{
	struct obs_video_info ovi;
	if (obs_get_video_info(&ovi) && ovi.fps_den > 0 && ovi.fps_num > 0)
		return (double)ovi.fps_num / (double)ovi.fps_den;
	return 30.0;
}

/* the engine works in milliseconds; the box shows frames, tenths of a
 * second or milliseconds */
static int SeamToMs(int value, int unit)
{
	if (unit == SEAM_UNIT_FRAMES)
		return (int)std::lround(value * 1000.0 / SeamFps());
	if (unit == SEAM_UNIT_SECONDS)
		return value * 100;
	return value;
}

static int SeamFromMs(int ms, int unit)
{
	int shown;
	if (unit == SEAM_UNIT_FRAMES)
		shown = (int)std::lround(ms * SeamFps() / 1000.0);
	else if (unit == SEAM_UNIT_SECONDS)
		shown = (int)std::lround(ms / 100.0);
	else
		shown = ms;
	return std::max(1, shown);
}

/* Transition… on the Final monitor: the transition, its length and the
 * unit; OK stores them and Space uses them from then on */
void OBSBasic::ShowTransitionDialog()
{
	if (!IsPreviewProgramMode())
		return;

	QDialog dialog(this);
	dialog.setWindowTitle(QTStr("obs2vmix.Transition.Title"));
	QFormLayout *form = new QFormLayout(&dialog);

	QComboBox *transition = new QComboBox();
	transition->setModel(ui->transitions->model());
	transition->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	int cur = transition->findData(QString::fromStdString(currentTransitionUuid));
	if (cur >= 0)
		transition->setCurrentIndex(cur);

	QSpinBox *duration = new QSpinBox();
	duration->setRange(1, 20000);
	duration->setAccelerated(true);

	QComboBox *unit = new QComboBox();
	unit->addItem(QTStr("obs2vmix.Unit.Frames"), SEAM_UNIT_FRAMES);
	unit->addItem(QTStr("obs2vmix.Unit.Seconds"), SEAM_UNIT_SECONDS);
	unit->addItem(QTStr("obs2vmix.Unit.Ms"), SEAM_UNIT_MS);
	config_t *cfg = App()->GetUserConfig();
	int savedUnit = config_has_user_value(cfg, "obs2vmix", "DurationUnit")
				? (int)config_get_int(cfg, "obs2vmix", "DurationUnit")
				: SEAM_UNIT_FRAMES;
	unit->setCurrentIndex(std::clamp(savedUnit, SEAM_UNIT_FRAMES, SEAM_UNIT_MS));

	QHBoxLayout *length = new QHBoxLayout();
	length->setSpacing(4);
	length->addWidget(duration, 1);
	length->addWidget(unit);

	form->addRow(QTStr("Transition"), transition);
	form->addRow(QTStr("Basic.TransitionDuration"), length);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	int ms = GetTransitionDuration();
	int shownUnit = unit->currentData().toInt();

	auto show = [&]() {
		QSignalBlocker sb(duration);
		duration->setSingleStep(shownUnit == SEAM_UNIT_MS ? 50 : 1);
		duration->setSuffix(shownUnit == SEAM_UNIT_SECONDS ? QStringLiteral(" /10")
				    : shownUnit == SEAM_UNIT_MS      ? QStringLiteral(" ms")
								     : QString());
		duration->setValue(SeamFromMs(ms, shownUnit));
	};
	auto fixedCheck = [&]() {
		auto it = transitions.find(transition->currentData().toString().toStdString());
		bool fixed = it != transitions.end() && it->second && obs_transition_fixed(it->second);
		duration->setEnabled(!fixed);
		unit->setEnabled(!fixed);
	};

	connect(unit, &QComboBox::currentIndexChanged, &dialog, [&]() {
		ms = SeamToMs(duration->value(), shownUnit);
		shownUnit = unit->currentData().toInt();
		show();
	});
	connect(transition, &QComboBox::currentIndexChanged, &dialog, [&]() { fixedCheck(); });

	show();
	fixedCheck();

	if (dialog.exec() != QDialog::Accepted)
		return;

	SetCurrentTransition(transition->currentData().toString());
	SetTransitionDuration(SeamToMs(duration->value(), shownUnit));
	config_set_int(cfg, "obs2vmix", "DurationUnit", shownUnit);
	config_save_safe(cfg, "tmp", nullptr);
}

void OBSBasic::SeamTake()
{
	TransitionClicked();
}

/* Cut: run the built-in cut transition once, then fall back to the chosen one */
void OBSBasic::SeamCut()
{
	if (!IsPreviewProgramMode())
		return;

	if (cutTransition && GetCurrentTransition().Get() != cutTransition) {
		OverrideTransition(cutTransition);
		overridingTransition = true;
	}

	TransitionToScene(GetCurrentSceneSource(), false, true, 0, false, false);
}

/* true when the keyboard focus is somewhere Space/Enter would not be typed */
bool OBSBasic::SeamKeyIsFree()
{
	QWidget *w = QApplication::focusWidget();
	if (!w)
		return true;
	if (qobject_cast<QLineEdit *>(w) || qobject_cast<QTextEdit *>(w) || qobject_cast<QPlainTextEdit *>(w) ||
	    qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QAbstractButton *>(w) || qobject_cast<QComboBox *>(w))
		return false;
	return true;
}

/* ---------------------------------------------------------------------- */

void OBSBasic::SetPreviewProgramMode(bool enabled)
{
	if (IsPreviewProgramMode() == enabled) {
		return;
	}

	os_atomic_set_bool(&previewProgramMode, enabled);
	emit PreviewProgramModeChanged(enabled);

	if (IsPreviewProgramMode()) {
		if (!previewEnabled) {
			EnablePreviewDisplay(true);
		}

		CreateProgramDisplay();
		CreateProgramOptions();

		OBSScene curScene = GetCurrentScene();

		OBSSceneAutoRelease dup;
		if (sceneDuplicationMode) {
			dup = obs_scene_duplicate(curScene, obs_source_get_name(obs_scene_get_source(curScene)),
						  editPropertiesMode ? OBS_SCENE_DUP_PRIVATE_COPY
								     : OBS_SCENE_DUP_PRIVATE_REFS);
		} else {
			dup = std::move(OBSScene(curScene));
		}

		OBSSourceAutoRelease transition = obs_get_output_source(0);
		obs_source_t *dup_source = obs_scene_get_source(dup);
		obs_transition_set(transition, dup_source);

		if (curScene) {
			obs_source_t *source = obs_scene_get_source(curScene);
			obs_source_inc_showing(source);
			lastScene = OBSGetWeakRef(source);
			programScene = OBSGetWeakRef(source);
		}

		RefreshQuickTransitions();

		programWidget = new QWidget();
		programLayout = new QVBoxLayout();
		programLayout->setContentsMargins(0, 0, 0, 0);
		programLayout->setSpacing(0);

		if (switcherView) {
			/* obs2vmix: Source and Final touch, no text anywhere. A two
			 * pixel tally line over each monitor, green and red, the
			 * strip below them, every action in a right-click menu. */
			auto tally = [](const char *color) {
				QWidget *line = new QWidget();
				line->setFixedHeight(2);
				line->setStyleSheet(QString("background:%1;").arg(QString::fromUtf8(color)));
				return line;
			};
			sourceTally = tally("#3ec26b");
			finalTally = tally("#e0413a");
			ui->previewTextLayout->insertWidget(0, sourceTally);
			programLayout->addWidget(finalTally);
			programLayout->addWidget(program);
			programWidget->setLayout(programLayout);

			/* the Source monitor fits its pane the way the Final does: no
			 * zoom, no scrollbars, no zoom bar under it (Marko, 14.9.2026:
			 * "I lost my preview window") */
			ui->preview->LockFit(true);
			setPreviewScalingWindow();
			ui->previewXContainer->hide();
			ui->previewYScrollBar->hide();
			/* the theme paints every OBSQTDisplay grey through a qproperty;
			 * a stylesheet on the widget itself wins, so both sit on black */
			ui->preview->setStyleSheet("OBSQTDisplay{qproperty-displayBackgroundColor:#000000;}");
			program->setStyleSheet("OBSQTDisplay{qproperty-displayBackgroundColor:#000000;}");

			QWidget *canvas = ui->previewLayout->parentWidget();
			programOptions->setParent(canvas);
			programOptions->hide();

			/* three panes with lines the mouse can drag, as OBS's docks:
			 * Source | Final above, the strip below */
			const char *handleStyle = "QSplitter::handle{background:#2c303a;}"
						  "QSplitter::handle:hover{background:#7aa2ff;}";
			monitorSplitter = new QSplitter(Qt::Horizontal);
			monitorSplitter->setObjectName("obs2vmixMonitorSplit");
			monitorSplitter->setChildrenCollapsible(false);
			monitorSplitter->setHandleWidth(5);
			monitorSplitter->setStyleSheet(handleStyle);
			ui->previewLayout->removeWidget(ui->previewContainer);
			monitorSplitter->addWidget(ui->previewContainer);
			monitorSplitter->addWidget(programWidget);
			monitorSplitter->setStretchFactor(0, 1);
			monitorSplitter->setStretchFactor(1, 1);
			ui->previewLayout->setSpacing(0);
			ui->previewLayout->addWidget(monitorSplitter);
			RestoreSplitter(monitorSplitter, "MonitorSplit");
			connect(monitorSplitter.data(), &QSplitter::splitterMoved, this,
				[this]() { SaveSplitter(monitorSplitter, "MonitorSplit"); });

			/* the FX rack is a floating window, opened from the Final menu */
			fxRack = new FxRack(this);

			sceneStrip = new SceneStrip();
			paneSplitter = new QSplitter(Qt::Vertical);
			paneSplitter->setObjectName("obs2vmixPaneSplit");
			paneSplitter->setChildrenCollapsible(false);
			paneSplitter->setHandleWidth(5);
			paneSplitter->setStyleSheet(handleStyle);
			ui->verticalLayout->removeWidget(canvas);
			paneSplitter->addWidget(canvas);
			paneSplitter->addWidget(sceneStrip);
			paneSplitter->setStretchFactor(0, 1);
			paneSplitter->setStretchFactor(1, 0);
			ui->verticalLayout->insertWidget(0, paneSplitter);
			RestoreSplitter(paneSplitter, "PaneSplit");
			connect(paneSplitter.data(), &QSplitter::splitterMoved, this,
				[this]() { SaveSplitter(paneSplitter, "PaneSplit"); });

			connect(sceneStrip.data(), &SceneStrip::SceneClicked, this,
				[this](OBSSource scene) { SetCurrentScene(scene, false); });
			connect(sceneStrip.data(), &SceneStrip::SceneDoubleClicked, this, &OBSBasic::OpenSceneEditor);
			connect(sceneStrip.data(), &SceneStrip::SceneMenuRequested, this, &OBSBasic::SceneTileMenu);
			connect(sceneStrip.data(), &SceneStrip::TilesChanged, this,
				&OBSBasic::UpdateSceneRecordingStatus);

			CreateSwitcherKeys(sceneStrip);

			program->installEventFilter(new MonitorDoubleClick(program, [this]() { SeamSwapMonitor(); }));
			ui->preview->installEventFilter(
				new MonitorDoubleClick(sceneStrip, [this]() { SeamSwapMonitor(); }));

			seamSingleMonitor = false;
			seamEditorOpen = false;
			ApplyMonitorLayout();
		} else {
			/* the OBS view: Studio Mode as OBS lays it out */
			programLabel = new QLabel(QTStr("StudioMode.ProgramSceneLabel"), this);
			programLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
			programLabel->setProperty("class", "label-preview-title");
			programLayout->addWidget(programLabel);
			programLayout->addWidget(program);
			programWidget->setLayout(programLayout);

			ui->previewLayout->setSpacing(2);
			ui->previewLayout->addWidget(programOptions);
			ui->previewLayout->addWidget(programWidget);
			ui->previewLayout->setAlignment(programOptions, Qt::AlignCenter);
		}

		/* the observer keeps OBS's two monitors the same size; in the vMix
		 * view the splitter does that, and the operator may drag it */
		if (!switcherView)
			sizeObserver = new PreviewProgramSizeObserver(ui->preview, program, this);

		OnEvent(OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED);

		blog(LOG_INFO, "Switched to Preview/Program mode");
		blog(LOG_INFO, "-----------------------------"
			       "-------------------");
	} else {
		OBSSource actualProgramScene = OBSGetStrongRef(programScene);
		if (!actualProgramScene) {
			actualProgramScene = GetCurrentSceneSource();
		} else {
			SetCurrentScene(actualProgramScene, true);
		}
		TransitionToScene(actualProgramScene, true);

		StopAllSceneRecordings();
		seamEditorOpen = false;
		seamSingleMonitor = false;

		/* the splitters give OBS its widgets back before they go */
		if (monitorSplitter) {
			QWidget *canvas = ui->previewLayout->parentWidget();
			ui->previewContainer->setParent(canvas);
			ui->previewLayout->insertWidget(0, ui->previewContainer);
			ui->previewContainer->show();
			ui->previewXContainer->show();
			ui->previewYScrollBar->show();
			ui->preview->LockFit(false);
			ui->preview->setStyleSheet(QString());
			delete monitorSplitter;
		}
		if (paneSplitter) {
			QWidget *canvas = ui->previewLayout->parentWidget();
			QWidget *central = ui->verticalLayout->parentWidget();
			canvas->setParent(central);
			ui->verticalLayout->insertWidget(0, canvas);
			canvas->show();
			delete paneSplitter;
		}

		ui->previewContainer->setVisible(true);
		ui->previewLayout->setSpacing(2);
		delete sceneStrip;
		delete fxRack;
		delete sourceTally;
		delete programOptions;
		delete program;
		delete programLabel;
		delete programWidget;
		if (sizeObserver)
			sizeObserver->deleteLater();

		if (lastScene) {
			OBSSource actualLastScene = OBSGetStrongRef(lastScene);
			if (actualLastScene) {
				obs_source_dec_showing(actualLastScene);
			}
			lastScene = nullptr;
		}

		programScene = nullptr;
		swapScene = nullptr;
		prevFTBSource = nullptr;

		for (QuickTransition &qt : quickTransitions) {
			qt.button = nullptr;
		}

		if (!previewEnabled) {
			EnablePreviewDisplay(false);
		}

		ui->transitions->setEnabled(true);
		tBarActive = false;

		OnEvent(OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED);

		blog(LOG_INFO, "Switched to regular Preview mode");
		blog(LOG_INFO, "-----------------------------"
			       "-------------------");
	}

	ResetUI();
	UpdateTitleBar();
}

/* a splitter has no size before the window is shown, so a first-time split
 * set then goes wrong (0.4.2 gave the strip three quarters); the first real
 * resize sets the shares, then the filter removes itself */
class SplitOnFirstResize : public QObject {
	QSplitter *splitter;
	double firstShare;

public:
	SplitOnFirstResize(QSplitter *s, double share) : QObject(s), splitter(s), firstShare(share) {}

protected:
	bool eventFilter(QObject *obj, QEvent *event) override
	{
		if (event->type() == QEvent::Resize) {
			int total = splitter->orientation() == Qt::Horizontal ? splitter->width() : splitter->height();
			if (total > 200) {
				int a = (int)(total * firstShare);
				splitter->setSizes({a, total - a});
				splitter->removeEventFilter(this);
				deleteLater();
			}
		}
		return QObject::eventFilter(obj, event);
	}
};

/* the pane sizes survive a restart: obs2vmix/MonitorSplit, obs2vmix/PaneSplit */
void OBSBasic::SaveSplitter(QSplitter *splitter, const char *key)
{
	if (!splitter)
		return;
	config_set_string(App()->GetUserConfig(), "obs2vmix", key, splitter->saveState().toBase64().constData());
	config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
}

void OBSBasic::RestoreSplitter(QSplitter *splitter, const char *key)
{
	if (!splitter)
		return;
	const char *state = config_get_string(App()->GetUserConfig(), "obs2vmix", key);
	if (state && *state && splitter->restoreState(QByteArray::fromBase64(QByteArray(state))))
		return;
	/* first time: the monitors share the width; the strip gets a quarter of the height */
	splitter->installEventFilter(
		new SplitOnFirstResize(splitter, splitter->orientation() == Qt::Horizontal ? 0.5 : 0.75));
}

void OBSBasic::RenderProgram(void *data, uint32_t, uint32_t)
{
	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "RenderProgram");

	OBSBasic *window = static_cast<OBSBasic *>(data);
	obs_video_info ovi;

	obs_get_video_info(&ovi);

	window->programCX = int(window->programScale * float(ovi.base_width));
	window->programCY = int(window->programScale * float(ovi.base_height));

	gs_viewport_push();
	gs_projection_push();

	/* --------------------------------------- */

	gs_ortho(0.0f, float(ovi.base_width), 0.0f, float(ovi.base_height), -100.0f, 100.0f);
	gs_set_viewport(window->programX, window->programY, window->programCX, window->programCY);

	obs_render_main_texture_src_color_only();
	gs_load_vertexbuffer(nullptr);

	/* --------------------------------------- */

	gs_projection_pop();
	gs_viewport_pop();

	GS_DEBUG_MARKER_END();
}

void OBSBasic::ResizeProgram(uint32_t cx, uint32_t cy)
{
	QSize targetSize;

	/* resize program panel to fix to the top section of the window */
	targetSize = GetPixelSize(program);
	GetScaleAndCenterPos(int(cx), int(cy), targetSize.width() - PREVIEW_EDGE_SIZE * 2,
			     targetSize.height() - PREVIEW_EDGE_SIZE * 2, programX, programY, programScale);

	programX += float(PREVIEW_EDGE_SIZE);
	programY += float(PREVIEW_EDGE_SIZE);
}

void OBSBasic::UpdatePreviewProgramIndicators()
{
	/* obs2vmix: no text in the vMix view; the OBS view keeps OBS's
	 * optional "Preview: x" labels */
	bool labels = previewProgramMode && !switcherView &&
		      config_get_bool(App()->GetUserConfig(), "BasicWindow", "StudioModeLabels");

	ui->previewLabel->setVisible(labels);

	if (programLabel) {
		programLabel->setVisible(labels);
	}

	if (!labels) {
		return;
	}

	QString preview = QTStr("StudioMode.PreviewSceneName").arg(QT_UTF8(obs_source_get_name(GetCurrentSceneSource())));
	QString program = QTStr("StudioMode.ProgramSceneName").arg(QT_UTF8(obs_source_get_name(GetProgramSource())));

	if (ui->previewLabel->text() != preview) {
		ui->previewLabel->setText(preview);
	}

	if (programLabel && programLabel->text() != program) {
		programLabel->setText(program);
	}
}

OBSSource OBSBasic::GetProgramSource()
{
	return OBSGetStrongRef(programScene);
}

/* right-click on the Final monitor */
void OBSBasic::ProgramViewContextMenuRequested()
{
	QMenu popup(this);
	QPointer<QMenu> studioProgramProjector;

	if (switcherView) {
		QAction *title = popup.addAction(QString("%1  ·  %2").arg(QTStr("obs2vmix.Menu.FinalTitle"),
									   QT_UTF8(obs_source_get_name(GetProgramSource()))));
		title->setEnabled(false);
		popup.addSeparator();

		popup.addAction(QTStr("obs2vmix.Menu.Transition"), this, &OBSBasic::ShowTransitionDialog);
		popup.addAction(QTStr("obs2vmix.Menu.AudioFx"), this, [this]() {
			if (fxRack)
				fxRack->Open();
		});
		FillTransitionConfigMenu(popup.addMenu(QTStr("obs2vmix.Menu.TransitionOptions")));
		popup.addSeparator();

		/* the main outputs: what the Final monitor shows goes to disk and to the stream */
		QAction *record = popup.addAction(QTStr("obs2vmix.Menu.Record"), this, &OBSBasic::RecordActionTriggered);
		record->setCheckable(true);
		record->setChecked(RecordingActive());
		QAction *stream = popup.addAction(QTStr("obs2vmix.Menu.Stream"), this, &OBSBasic::StreamActionTriggered);
		stream->setCheckable(true);
		stream->setChecked(StreamingActive());
		popup.addSeparator();
	}

	studioProgramProjector = new QMenu(QTStr("Projector.Open.Program"), &popup);
	AddProjectorMenuMonitors(studioProgramProjector, this, &OBSBasic::OpenStudioProgramProjector);
	studioProgramProjector->addSeparator();
	studioProgramProjector->addAction(QTStr("Projector.Window"), this, &OBSBasic::OpenStudioProgramWindow);

	popup.addMenu(studioProgramProjector);

	popup.addSeparator();
	popup.addAction(QTStr("Screenshot.StudioProgram"), this, &OBSBasic::ScreenshotProgram);

	if (switcherView)
		AddSwitcherCommonActions(&popup);

	popup.exec(QCursor::pos());
}

void OBSBasic::EnablePreviewProgram()
{
	SetPreviewProgramMode(true);
}

void OBSBasic::DisablePreviewProgram()
{
	if (!switcherView)
		SetPreviewProgramMode(false);
}

void OBSBasic::OpenStudioProgramProjector()
{
	int monitor = sender()->property("monitor").toInt();
	OpenProjector(nullptr, monitor, ProjectorType::StudioProgram);
}

void OBSBasic::OpenStudioProgramWindow()
{
	OpenProjector(nullptr, -1, ProjectorType::StudioProgram);
}

/* ---------------------------------------------------------------------- */
/* obs2vmix: record a scene                                                */

SceneRecorder *OBSBasic::FindSceneRecorder(obs_source_t *scene)
{
	for (auto &rec : sceneRecorders) {
		OBSSource s = rec->Scene();
		if (s && s.Get() == scene)
			return rec.get();
	}
	return nullptr;
}

/* the red circle next to a scene's name */
void OBSBasic::ToggleSceneRecording(OBSSource scene)
{
	if (!scene)
		return;

	SceneRecorder *existing = FindSceneRecorder(scene);
	if (existing) {
		existing->Stop();
		UpdateSceneRecordingStatus();
		return;
	}

	auto rec = std::make_unique<SceneRecorder>(scene, this);
	connect(rec.get(), &SceneRecorder::Stopped, this, &OBSBasic::SceneRecordingStopped);
	if (!rec->Start()) {
		blog(LOG_WARNING, "[obs2vmix] scene recording failed: %s", rec->Error().c_str());
		OBSMessageBox::warning(this, QTStr("obs2vmix.RecordFailed"), QT_UTF8(rec->Error().c_str()));
		UpdateSceneRecordingStatus();
		return;
	}
	sceneRecorders.push_back(std::move(rec));

	if (!sceneRecordTimer) {
		sceneRecordTimer = new QTimer(this);
		sceneRecordTimer->setInterval(250);
		connect(sceneRecordTimer.data(), &QTimer::timeout, this, &OBSBasic::UpdateSceneRecordingStatus);
	}
	if (!sceneRecordTimer->isActive())
		sceneRecordTimer->start();

	UpdateSceneRecordingStatus();
}

void OBSBasic::SceneRecordingStopped(SceneRecorder *recorder)
{
	for (auto it = sceneRecorders.begin(); it != sceneRecorders.end(); ++it) {
		if (it->get() == recorder) {
			blog(LOG_INFO, "[obs2vmix] scene recording finished: %s", recorder->Path().c_str());
			sceneRecorders.erase(it);
			break;
		}
	}
	if (sceneRecorders.empty() && sceneRecordTimer)
		sceneRecordTimer->stop();
	UpdateSceneRecordingStatus();
}

void OBSBasic::StopAllSceneRecordings()
{
	if (sceneRecordTimer)
		sceneRecordTimer->stop();
	/* the destructors force-stop what is still running */
	sceneRecorders.clear();
}

static QString formatElapsed(uint64_t ms)
{
	uint64_t s = ms / 1000;
	return QString("%1:%2:%3")
		.arg(s / 3600, 2, 10, QChar('0'))
		.arg((s / 60) % 60, 2, 10, QChar('0'))
		.arg(s % 60, 2, 10, QChar('0'));
}

static QString formatBytes(uint64_t b)
{
	const double GB = 1024.0 * 1024.0 * 1024.0;
	if (b >= 1000 * GB)
		return QString("%1 TB").arg(b / (1024.0 * GB), 0, 'f', 2);
	if (b >= GB)
		return QString("%1 GB").arg((qulonglong)(b / GB));
	return QString("%1 MB").arg((qulonglong)(b / (1024.0 * 1024.0)));
}

/* the status line under every thumbnail, 4 Hz while anything records */
void OBSBasic::UpdateSceneRecordingStatus()
{
	if (!sceneStrip)
		return;

	int count = sceneStrip->Count();
	for (int i = 0; i < count; i++) {
		OBSSource s = sceneStrip->SceneAt(i);
		SceneRecorder *rec = s ? FindSceneRecorder(s) : nullptr;
		if (!rec) {
			sceneStrip->SetRecording(i, false);
			sceneStrip->SetStatus(i, 0, QString(), 0);
			continue;
		}

		SceneRecorder::Stats st = rec->Poll();
		QStringList parts;
		parts << formatBytes(st.freeBytes);
		parts << QString("%1 fps").arg((int)(st.fps + 0.5));
		if (st.dropped)
			parts << QString("%1 dropped").arg(st.dropped);

		int level = st.lowDisk ? 2 : st.dropped ? 1 : 0;
		sceneStrip->SetRecording(i, true);
		sceneStrip->SetStatus(i, st.elapsedMs, QString("%1 (%2)").arg(formatElapsed(st.elapsedMs), parts.join(" · ")),
				      level);
	}

	/* a recorder whose scene left the collection stops itself */
	for (auto &rec : sceneRecorders) {
		OBSSource s = rec->Scene();
		if (!s || sceneStrip->IndexOf(s) < 0)
			rec->Stop(true);
	}
}

/* ---------------------------------------------------------------------- */
/* obs2vmix: the two views                                                 */

void OBSBasic::CreateViewMenu()
{
	QMenu *menu = new QMenu(QTStr("obs2vmix.Menu.View"), this);
	QActionGroup *group = new QActionGroup(menu);
	group->setExclusive(true);

	viewVmixAction = menu->addAction(QTStr("obs2vmix.Menu.VMix"));
	viewVmixAction->setCheckable(true);
	viewVmixAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
	viewVmixAction->setToolTip(QTStr("obs2vmix.Menu.VMixTT"));
	group->addAction(viewVmixAction);

	viewObsAction = menu->addAction(QTStr("obs2vmix.Menu.OBS"));
	viewObsAction->setCheckable(true);
	viewObsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
	viewObsAction->setToolTip(QTStr("obs2vmix.Menu.OBSTT"));
	group->addAction(viewObsAction);

	if (ui->actionFullscreenInterface) {
		menu->addSeparator();
		menu->addAction(ui->actionFullscreenInterface);
	}

	connect(viewVmixAction.data(), &QAction::triggered, this, [this]() { SetSwitcherView(true); });
	connect(viewObsAction.data(), &QAction::triggered, this, [this]() { SetSwitcherView(false); });

	viewVmixAction->setChecked(switcherView);
	viewObsAction->setChecked(!switcherView);

	ui->menubar->insertMenu(ui->menuDocks->menuAction(), menu);

	/* Help: update the app, and how */
	QAction *update = new QAction(QTStr("obs2vmix.Update.Menu"), this);
	connect(update, &QAction::triggered, this, [this]() {
		if (!obs2vmix::RunUpdaterInTerminal())
			obs2vmix::ShowUpdateHelp(this);
	});
	QAction *check = new QAction(QTStr("obs2vmix.Update.CheckMenu"), this);
	connect(check, &QAction::triggered, this, [this]() { obs2vmix::CheckForUpdate(this, false); });
	QAction *howto = new QAction(QTStr("obs2vmix.Update.Help"), this);
	connect(howto, &QAction::triggered, this, [this]() { obs2vmix::ShowUpdateHelp(this); });

	QList<QAction *> helpActions = ui->menuBasic_MainMenu_Help->actions();
	QAction *first = helpActions.isEmpty() ? nullptr : helpActions.first();
	ui->menuBasic_MainMenu_Help->insertAction(first, update);
	ui->menuBasic_MainMenu_Help->insertAction(first, check);
	ui->menuBasic_MainMenu_Help->insertAction(first, howto);
	ui->menuBasic_MainMenu_Help->insertSeparator(first);
}

/* vMix: every dock, the context bar and the status bar go away; OBS: they
 * come back the way OBS had them */
void OBSBasic::ApplySwitcherImmersion()
{
	if (switcherView) {
		if (!config_has_user_value(App()->GetUserConfig(), "obs2vmix", "OBSDockState")) {
			config_set_string(App()->GetUserConfig(), "obs2vmix", "OBSDockState",
					  saveState().toBase64().constData());
		}
		for (QDockWidget *dock : findChildren<QDockWidget *>())
			dock->hide();
		ui->contextContainer->hide();
		ui->statusbar->hide();
	} else {
		const char *obsState = config_get_string(App()->GetUserConfig(), "obs2vmix", "OBSDockState");
		bool restored = false;
		if (obsState && *obsState)
			restored = restoreState(QByteArray::fromBase64(QByteArray(obsState)));
		if (!restored)
			on_resetDocks_triggered(true);
		ui->contextContainer->setVisible(ui->toggleContextBar->isChecked());
		ui->statusbar->setVisible(ui->toggleStatusBar->isChecked());
	}

	if (viewVmixAction)
		viewVmixAction->setChecked(switcherView);
	if (viewObsAction)
		viewObsAction->setChecked(!switcherView);
}

void OBSBasic::SetSwitcherView(bool vmix)
{
	if (vmix == switcherView) {
		ApplySwitcherImmersion();
		return;
	}

	if (seamEditorOpen)
		CloseSceneEditor();

	if (!switcherView) {
		/* leaving the OBS view: remember its docks and its Studio Mode flag */
		config_set_string(App()->GetUserConfig(), "obs2vmix", "OBSDockState", saveState().toBase64().constData());
		obsViewStudioMode = IsPreviewProgramMode();
	}

	/* tear down whichever layout is up, then build the other one */
	SetPreviewProgramMode(false);
	switcherView = vmix;
	config_set_string(App()->GetUserConfig(), "obs2vmix", "View", vmix ? "vmix" : "obs");
	config_save_safe(App()->GetUserConfig(), "tmp", nullptr);

	SetPreviewProgramMode(vmix ? true : obsViewStudioMode);
	ApplySwitcherImmersion();

	blog(LOG_INFO, "[obs2vmix] view: %s", vmix ? "vMix" : "OBS");
}
