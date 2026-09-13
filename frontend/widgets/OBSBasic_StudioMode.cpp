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
#include <utility/SceneRecorder.hpp>

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
#include <QToolButton>
#include <QEvent>
#include <QTimer>

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
	layout->addLayout(quickTransitionsLayout);
	layout->addWidget(tBar);
	layout->addStretch(0);

	programOptions->setLayout(layout);
	programOptions->hide();

	auto onAdd = [this]() {
		QScopedPointer<QMenu> menu(CreateTransitionMenu(this, nullptr));
		menu->exec(QCursor::pos());
	};

	connect(addQuickTransition, &QAbstractButton::clicked, this, onAdd);

	CreateSeamBar();
}

/* the bar on the seam: Source ⧉ | transition, length, unit, CUT, TAKE ⋮ | Record */
void OBSBasic::CreateSeamBar()
{
	seamBar = new QWidget();
	seamBar->setObjectName("obs2vmixSeamBar");
	QHBoxLayout *bar = new QHBoxLayout(seamBar);
	bar->setContentsMargins(8, 3, 8, 3);
	bar->setSpacing(6);

	QLabel *sourceTag = new QLabel(QTStr("obs2vmix.Source"));
	sourceTag->setStyleSheet("color:#3ec26b;font-weight:600;letter-spacing:1px;");

	seamCollapseButton = new QToolButton();
	seamCollapseButton->setText(QStringLiteral("⧉"));
	seamCollapseButton->setToolTip(QTStr("obs2vmix.Collapse"));
	seamCollapseButton->setAutoRaise(true);
	connect(seamCollapseButton.data(), &QToolButton::clicked, this, &OBSBasic::SeamToggleCollapse);

	bar->addWidget(sourceTag);
	bar->addWidget(seamCollapseButton);
	bar->addStretch(1);

	CreateSeamControls(bar);

	transitionButton = new QPushButton(QTStr("obs2vmix.Take"));
	transitionButton->setProperty("class", "obs2vmix-take");
	transitionButton->setToolTip(QTStr("obs2vmix.TakeTT"));
	transitionButton->setStyleSheet("QPushButton{background:#e0413a;color:#fff;font-weight:600;letter-spacing:1px;"
					"padding:3px 14px;border:1px solid #e0413a;border-radius:3px;}"
					"QPushButton:hover{background:#f04f47;}QPushButton:pressed{background:#b8332d;}");
	connect(transitionButton.data(), &QAbstractButton::clicked, this, &OBSBasic::TransitionClicked);
	bar->addWidget(transitionButton);

	QPushButton *configTransitions = new QPushButton();
	configTransitions->setProperty("class", "icon-dots-vert");
	configTransitions->setToolTip(QTStr("QuickTransitions"));
	bar->addWidget(configTransitions);

	bar->addStretch(1);

	seamEditorDone = new QPushButton(QTStr("obs2vmix.CloseEditor"));
	seamEditorDone->setToolTip(QTStr("obs2vmix.CloseEditorTT"));
	seamEditorDone->hide();
	connect(seamEditorDone.data(), &QAbstractButton::clicked, this, &OBSBasic::CloseSceneEditor);
	bar->addWidget(seamEditorDone);

	QLabel *recordTag = new QLabel(QTStr("obs2vmix.Record"));
	recordTag->setStyleSheet("color:#e0413a;font-weight:600;letter-spacing:1px;");
	bar->addWidget(recordTag);

	/* Space = Take, Enter = Cut, 1-9 = load a scene into Source, Esc =
	 * close the editor; only while no text field or button has the focus.
	 * Parented to the bar so the shortcuts die with studio mode. */
	QShortcut *takeKey = new QShortcut(QKeySequence(Qt::Key_Space), seamBar, nullptr, nullptr, Qt::WindowShortcut);
	connect(takeKey, &QShortcut::activated, this, [this]() {
		if (SeamKeyIsFree())
			SeamTake();
	});
	QShortcut *cutKey = new QShortcut(QKeySequence(Qt::Key_Return), seamBar, nullptr, nullptr, Qt::WindowShortcut);
	connect(cutKey, &QShortcut::activated, this, [this]() {
		if (SeamKeyIsFree())
			SeamCut();
	});
	QShortcut *escKey = new QShortcut(QKeySequence(Qt::Key_Escape), seamBar, nullptr, nullptr, Qt::WindowShortcut);
	connect(escKey, &QShortcut::activated, this, [this]() {
		if (seamEditorOpen && SeamKeyIsFree())
			CloseSceneEditor();
	});
	for (int i = 0; i < 9; i++) {
		QShortcut *key = new QShortcut(QKeySequence(Qt::Key_1 + i), seamBar, nullptr, nullptr,
					       Qt::WindowShortcut);
		connect(key, &QShortcut::activated, this, [this, i]() {
			if (SeamKeyIsFree())
				SeamSelectScene(i);
		});
	}

	auto onConfig = [this]() {
		QMenu menu(this);
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

		auto showToolTip = [&]() {
			QAction *act = menu.activeAction();
			QToolTip::showText(QCursor::pos(), act->toolTip(), &menu, menu.actionGeometry(act));
		};

		action = menu.addAction(QTStr("QuickTransitions.DuplicateScene"));
		action->setToolTip(QTStr("QuickTransitions.DuplicateSceneTT"));
		action->setCheckable(true);
		action->setChecked(sceneDuplicationMode);
		connect(action, &QAction::triggered, this, toggleSceneDuplication);
		connect(action, &QAction::hovered, action, showToolTip);

		action = menu.addAction(QTStr("QuickTransitions.EditProperties"));
		action->setToolTip(QTStr("QuickTransitions.EditPropertiesTT"));
		action->setCheckable(true);
		action->setChecked(editPropertiesMode);
		action->setEnabled(sceneDuplicationMode);
		connect(action, &QAction::triggered, this, toggleEditProperties);
		connect(action, &QAction::hovered, action, showToolTip);

		action = menu.addAction(QTStr("QuickTransitions.SwapScenes"));
		action->setToolTip(QTStr("QuickTransitions.SwapScenesTT"));
		action->setCheckable(true);
		action->setChecked(swapScenesMode);
		connect(action, &QAction::triggered, this, toggleSwapScenesMode);
		connect(action, &QAction::hovered, action, showToolTip);

		menu.exec(QCursor::pos());
	};

	connect(configTransitions, &QAbstractButton::clicked, this, onConfig);
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

	if (seamCollapseButton) {
		seamCollapseButton->setText(seamSingleMonitor ? QStringLiteral("⧈") : QStringLiteral("⧉"));
		seamCollapseButton->setToolTip(seamSingleMonitor ? QTStr("obs2vmix.Expand") : QTStr("obs2vmix.Collapse"));
		seamCollapseButton->setEnabled(!seamEditorOpen);
	}
	if (seamEditorDone)
		seamEditorDone->setVisible(seamEditorOpen);
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
	SetPreviewProgramMode(!IsPreviewProgramMode());
}

/* ---------------------------------------------------------------------- */
/* obs2vmix: the seam                                                      */

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

void OBSBasic::CreateSeamControls(QBoxLayout *layout)
{
	seamTransitions = new QComboBox();
	seamTransitions->setModel(ui->transitions->model());
	seamTransitions->setToolTip(QTStr("Transition"));
	seamTransitions->setSizeAdjustPolicy(QComboBox::AdjustToContents);

	QHBoxLayout *lengthLayout = new QHBoxLayout();
	lengthLayout->setSpacing(2);

	seamDuration = new QSpinBox();
	seamDuration->setRange(1, 20000);
	seamDuration->setAccelerated(true);
	seamDuration->setToolTip(QTStr("Basic.TransitionDuration"));

	seamUnit = new QComboBox();
	seamUnit->addItem(QTStr("obs2vmix.Unit.Frames"), SEAM_UNIT_FRAMES);
	seamUnit->addItem(QTStr("obs2vmix.Unit.Seconds"), SEAM_UNIT_SECONDS);
	seamUnit->addItem(QTStr("obs2vmix.Unit.Ms"), SEAM_UNIT_MS);
	seamUnit->setCurrentIndex(SEAM_UNIT_FRAMES);

	lengthLayout->addWidget(seamDuration);
	lengthLayout->addWidget(seamUnit);

	QPushButton *cutButton = new QPushButton(QTStr("obs2vmix.Cut"));
	cutButton->setToolTip(QTStr("obs2vmix.CutTT"));
	cutButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	layout->addWidget(seamTransitions);
	layout->addLayout(lengthLayout);
	layout->addWidget(cutButton);

	/* the dock's combo and this one share a model; keep the selection in step */
	connect(seamTransitions, &QComboBox::currentIndexChanged, this, [this](int idx) {
		if (idx < 0 || !seamTransitions)
			return;
		SetCurrentTransition(seamTransitions->itemData(idx).toString());
	});
	connect(this, &OBSBasic::CurrentTransitionChanged, this, &OBSBasic::SeamSyncTransition);
	connect(this, &OBSBasic::TransitionDurationChanged, this, &OBSBasic::SeamSyncDuration);
	connect(seamDuration, &QSpinBox::valueChanged, this, &OBSBasic::SeamDurationEdited);
	connect(seamUnit, &QComboBox::currentIndexChanged, this, &OBSBasic::SeamSyncDuration);
	connect(cutButton, &QAbstractButton::clicked, this, &OBSBasic::SeamCut);

	SeamSyncTransition();
	SeamSyncDuration();
}

void OBSBasic::SeamSyncTransition()
{
	if (!seamTransitions)
		return;

	int idx = seamTransitions->findData(QString::fromStdString(currentTransitionUuid));
	if (idx != -1 && idx != seamTransitions->currentIndex()) {
		QSignalBlocker sb(seamTransitions);
		seamTransitions->setCurrentIndex(idx);
	}

	OBSSource tr = GetCurrentTransition();
	bool fixed = tr ? obs_transition_fixed(tr) : false;
	if (seamDuration)
		seamDuration->setEnabled(!fixed);
	if (seamUnit)
		seamUnit->setEnabled(!fixed);
}

/* engine → box: show the engine's milliseconds in the chosen unit */
void OBSBasic::SeamSyncDuration()
{
	if (!seamDuration || !seamUnit)
		return;

	int ms = GetTransitionDuration();
	int unit = seamUnit->currentData().toInt();
	int shown;

	if (unit == SEAM_UNIT_FRAMES) {
		shown = (int)std::lround(ms * SeamFps() / 1000.0);
		seamDuration->setSingleStep(1);
		seamDuration->setSuffix("");
	} else if (unit == SEAM_UNIT_SECONDS) {
		/* the box is an integer box: seconds are shown in tenths */
		shown = (int)std::lround(ms / 100.0);
		seamDuration->setSingleStep(1);
		seamDuration->setSuffix(QStringLiteral(" /10"));
	} else {
		shown = ms;
		seamDuration->setSingleStep(50);
		seamDuration->setSuffix(QStringLiteral(" ms"));
	}

	QSignalBlocker sb(seamDuration);
	seamDuration->setValue(std::max(1, shown));
}

/* box → engine: the engine works in milliseconds */
void OBSBasic::SeamDurationEdited()
{
	if (!seamDuration || !seamUnit)
		return;

	int v = seamDuration->value();
	int unit = seamUnit->currentData().toInt();
	int ms;

	if (unit == SEAM_UNIT_FRAMES)
		ms = (int)std::lround(v * 1000.0 / SeamFps());
	else if (unit == SEAM_UNIT_SECONDS)
		ms = v * 100;
	else
		ms = v;

	SetTransitionDuration(ms);
	/* SetTransitionDuration clamps to 50..20000 and stays silent when the
	 * value did not change; show what the engine really has */
	SeamSyncDuration();
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

		programLabel = new QLabel(QTStr("StudioMode.ProgramSceneLabel"), this);
		programLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
		programLabel->setProperty("class", "label-preview-title");

		programWidget = new QWidget();
		programLayout = new QVBoxLayout();
		programLayout->setContentsMargins(0, 0, 0, 0);
		programLayout->setSpacing(0);

		programLayout->addWidget(programLabel);
		programLayout->addWidget(program);

		programWidget->setLayout(programLayout);

		/* obs2vmix: Source and Record touch; the controls sit on the
		 * seam bar above them and the scene strip below them */
		ui->previewLayout->setSpacing(0);
		ui->previewLayout->addWidget(programWidget);
		programOptions->setParent(ui->previewLayout->parentWidget());
		programOptions->hide();

		ui->verticalLayout->insertWidget(0, seamBar);

		sceneStrip = new SceneStrip();
		ui->verticalLayout->insertWidget(2, sceneStrip);
		connect(sceneStrip.data(), &SceneStrip::SceneClicked, this,
			[this](OBSSource scene) { SetCurrentScene(scene, false); });
		connect(sceneStrip.data(), &SceneStrip::SceneDoubleClicked, this, &OBSBasic::OpenSceneEditor);
		connect(sceneStrip.data(), &SceneStrip::RecordClicked, this, &OBSBasic::ToggleSceneRecording);
		connect(sceneStrip.data(), &SceneStrip::TilesChanged, this, &OBSBasic::UpdateSceneRecordingStatus);

		program->installEventFilter(new MonitorDoubleClick(program, [this]() { SeamSwapMonitor(); }));
		ui->preview->installEventFilter(new MonitorDoubleClick(seamBar, [this]() { SeamSwapMonitor(); }));

		seamSingleMonitor = false;
		seamEditorOpen = false;
		ApplyMonitorLayout();

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
		ui->previewContainer->setVisible(true);
		ui->previewLayout->setSpacing(2);
		delete sceneStrip;
		delete seamBar;
		delete programOptions;
		delete program;
		delete programLabel;
		delete programWidget;
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
	/* obs2vmix: the tally tags are always on in studio mode */
	bool labels = previewProgramMode;

	ui->previewLabel->setVisible(labels);

	if (programLabel) {
		programLabel->setVisible(labels);
	}

	if (!labels) {
		return;
	}

	auto tag = [](const QString &kind, const char *color, const char *name) {
		return QString("<span style='color:%1;font-weight:600;letter-spacing:1px;'>%2</span>"
			       "&nbsp;&nbsp;<span style='color:#d8dbe2;'>%3</span>")
			.arg(QString::fromUtf8(color), kind.toHtmlEscaped(), QT_UTF8(name).toHtmlEscaped());
	};

	QString preview = tag(QTStr("obs2vmix.Source"), "#3ec26b", obs_source_get_name(GetCurrentSceneSource()));
	QString program = tag(QTStr("obs2vmix.Record"), "#e0413a", obs_source_get_name(GetProgramSource()));

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

void OBSBasic::ProgramViewContextMenuRequested()
{
	QMenu popup(this);
	QPointer<QMenu> studioProgramProjector;

	studioProgramProjector = new QMenu(QTStr("Projector.Open.Program"));
	AddProjectorMenuMonitors(studioProgramProjector, this, &OBSBasic::OpenStudioProgramProjector);
	studioProgramProjector->addSeparator();
	studioProgramProjector->addAction(QTStr("Projector.Window"), this, &OBSBasic::OpenStudioProgramWindow);

	popup.addMenu(studioProgramProjector);

	popup.addSeparator();
	popup.addAction(QTStr("Screenshot.StudioProgram"), this, &OBSBasic::ScreenshotProgram);

	popup.exec(QCursor::pos());
}

void OBSBasic::EnablePreviewProgram()
{
	SetPreviewProgramMode(true);
}

void OBSBasic::DisablePreviewProgram()
{
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
			sceneStrip->SetStatus(i, QString(), 0);
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
		sceneStrip->SetStatus(i, QString("%1 (%2)").arg(formatElapsed(st.elapsedMs), parts.join(" · ")), level);
	}

	/* a recorder whose scene left the collection stops itself */
	for (auto &rec : sceneRecorders) {
		OBSSource s = rec->Scene();
		if (!s || sceneStrip->IndexOf(s) < 0)
			rec->Stop(true);
	}
}
