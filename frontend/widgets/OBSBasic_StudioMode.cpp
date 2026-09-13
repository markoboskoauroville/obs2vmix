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

void OBSBasic::CreateProgramOptions()
{
	programOptions = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout();
	layout->setSpacing(4);

	QPushButton *configTransitions = new QPushButton();
	configTransitions->setProperty("class", "icon-dots-vert");

	QHBoxLayout *mainButtonLayout = new QHBoxLayout();
	mainButtonLayout->setSpacing(2);

	transitionButton = new QPushButton(QTStr("obs2vmix.Take"));
	transitionButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	transitionButton->setProperty("class", "obs2vmix-take");
	transitionButton->setToolTip(QTStr("obs2vmix.TakeTT"));

	QHBoxLayout *quickTransitionsLayout = new QHBoxLayout();
	quickTransitionsLayout->setSpacing(2);

	QPushButton *addQuickTransition = new QPushButton();
	addQuickTransition->setProperty("class", "icon-plus");

	QLabel *quickTransitionsLabel = new QLabel(QTStr("QuickTransitions"));
	quickTransitionsLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	quickTransitionsLayout->addWidget(quickTransitionsLabel);
	quickTransitionsLayout->addWidget(addQuickTransition);

	mainButtonLayout->addWidget(transitionButton);
	mainButtonLayout->addWidget(configTransitions);

	tBar = new SliderIgnoreClick(Qt::Horizontal);
	tBar->setMinimum(0);
	tBar->setMaximum(T_BAR_PRECISION - 1);

	tBar->setProperty("class", "slider-tbar");

	connect(tBar, &QSlider::valueChanged, this, &OBSBasic::TBarChanged);
	connect(tBar, &QSlider::sliderReleased, this, &OBSBasic::TBarReleased);

	layout->addStretch(0);
	CreateSeamControls(layout);
	layout->addLayout(mainButtonLayout);
	layout->addLayout(quickTransitionsLayout);
	layout->addWidget(tBar);
	layout->addStretch(0);

	programOptions->setLayout(layout);

	/* Space = Take, Enter = Cut, while the main window is active and no
	 * text field or button has the focus. Parented to programOptions so
	 * the shortcuts die with studio mode. */
	QShortcut *takeKey = new QShortcut(QKeySequence(Qt::Key_Space), programOptions, nullptr, nullptr,
					   Qt::WindowShortcut);
	connect(takeKey, &QShortcut::activated, this, [this]() {
		if (SeamKeyIsFree())
			SeamTake();
	});
	QShortcut *cutKey = new QShortcut(QKeySequence(Qt::Key_Return), programOptions, nullptr, nullptr,
					  Qt::WindowShortcut);
	connect(cutKey, &QShortcut::activated, this, [this]() {
		if (SeamKeyIsFree())
			SeamCut();
	});

	auto onAdd = [this]() {
		QScopedPointer<QMenu> menu(CreateTransitionMenu(this, nullptr));
		menu->exec(QCursor::pos());
	};

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

	connect(transitionButton.data(), &QAbstractButton::clicked, this, &OBSBasic::TransitionClicked);
	connect(addQuickTransition, &QAbstractButton::clicked, this, onAdd);
	connect(configTransitions, &QAbstractButton::clicked, this, onConfig);
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

		ui->previewLayout->addWidget(programOptions);
		ui->previewLayout->addWidget(programWidget);
		ui->previewLayout->setAlignment(programOptions, Qt::AlignCenter);

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
	bool labels = previewProgramMode ? config_get_bool(App()->GetUserConfig(), "BasicWindow", "StudioModeLabels")
					 : false;

	ui->previewLabel->setVisible(labels);

	if (programLabel) {
		programLabel->setVisible(labels);
	}

	if (!labels) {
		return;
	}

	QString preview =
		QTStr("StudioMode.PreviewSceneName").arg(QT_UTF8(obs_source_get_name(GetCurrentSceneSource())));

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
