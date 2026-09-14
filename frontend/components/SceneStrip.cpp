/******************************************************************************
    obs2vmix: the scene strip

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "SceneStrip.hpp"

#include <utility/display-helpers.hpp>
#include <widgets/OBSBasic.hpp>
#include <widgets/OBSQTDisplay.hpp>

#include <qt-wrappers.hpp>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QFontDatabase>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QResizeEvent>

#include <algorithm>

/* ---------------------------------------------------------------------- */
/* colours (ARGB, the format gs_effect_set_color takes)                    */

static const uint32_t COLOR_PANEL = 0xFF1D2026;
static const uint32_t COLOR_BLACK = 0xFF000000;
static const uint32_t COLOR_FRAME = 0xFF3A3F4B;
static const uint32_t COLOR_EDITING = 0xFF8B91A0;
static const uint32_t COLOR_PREVIEW = 0xFF3EC26B;
static const uint32_t COLOR_PROGRAM = 0xFFE0413A;

static const int TILE_PAD = 4; /* logical pixels around every thumbnail */

static inline void regionStart(int vX, int vY, int vCX, int vCY, float oL, float oR, float oT, float oB)
{
	gs_projection_push();
	gs_viewport_push();
	gs_set_viewport(vX, vY, vCX, vCY);
	gs_ortho(oL, oR, oT, oB, -100.0f, 100.0f);
}

static inline void regionEnd()
{
	gs_viewport_pop();
	gs_projection_pop();
}

/* ---------------------------------------------------------------------- */
/* the display: five thumbnails, click / double-click / wheel              */

class SceneStripDisplay : public OBSQTDisplay {
public:
	SceneStripDisplay(SceneStrip *strip_) : OBSQTDisplay(strip_), strip(strip_)
	{
		/* the pane above decides the height (a splitter line the operator
		 * drags); the thumbnails fit whatever height they get */
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumHeight(60);
		SetDisplayBackgroundColor(QColor(0x1d, 0x20, 0x26));
	}

	QSize sizeHint() const override
	{
		int w = width() > 0 ? width() : 800;
		int tile = w / SceneStrip::VISIBLE - 2 * TILE_PAD;
		if (tile < 16)
			tile = 16;
		return QSize(w, tile * 9 / 16 + 2 * TILE_PAD);
	}

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		OBSQTDisplay::mousePressEvent(event);
		int idx = strip->TileAt(event->pos());
		if (idx < 0)
			return;
		OBSSource s = strip->SceneAt(idx);
		if (!s)
			return;
		if (event->button() == Qt::LeftButton)
			emit strip->SceneClicked(s);
		else if (event->button() == Qt::RightButton)
			emit strip->SceneMenuRequested(s, event->globalPosition().toPoint());
	}

	/* the name and the recording line live in the tooltip */
	bool event(QEvent *e) override
	{
		if (e->type() == QEvent::ToolTip) {
			QHelpEvent *he = static_cast<QHelpEvent *>(e);
			int idx = strip->TileAt(he->pos());
			QString text = idx >= 0 ? strip->TileToolTip(idx) : QString();
			if (text.isEmpty())
				QToolTip::hideText();
			else
				QToolTip::showText(he->globalPos(), text, this);
			return true;
		}
		return OBSQTDisplay::event(e);
	}

	void mouseDoubleClickEvent(QMouseEvent *event) override
	{
		OBSQTDisplay::mouseDoubleClickEvent(event);
		if (event->button() != Qt::LeftButton)
			return;
		int idx = strip->TileAt(event->pos());
		if (idx >= 0) {
			OBSSource s = strip->SceneAt(idx);
			if (s)
				emit strip->SceneDoubleClicked(s);
		}
	}

	void wheelEvent(QWheelEvent *event) override
	{
		QPoint d = event->angleDelta();
		int steps = 0;
		if (d.x() != 0)
			steps = d.x() > 0 ? -1 : 1;
		else if (d.y() != 0)
			steps = d.y() > 0 ? -1 : 1;
		if (steps) {
			strip->SetOffset(strip->offset + steps);
			event->accept();
		}
	}

private:
	SceneStrip *strip;
};

/* ---------------------------------------------------------------------- */

SceneStrip::SceneStrip(QWidget *parent) : QWidget(parent)
{
	setObjectName("obs2vmixSceneStrip");

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	/* the thumbnails */
	display = new SceneStripDisplay(this);
	layout->addWidget(display, 1);

	connect(display, &OBSQTDisplay::DisplayCreated, this, [this](OBSQTDisplay *window) {
		obs_display_add_draw_callback(window->GetDisplay(), SceneStrip::Render, this);
	});

	/* one reserved line under them: empty, or the recording light and the
	 * time recorded (Marko, 14.9.2026: "a round circle indicator, and
	 * next to it the time of recording") */
	QWidget *lights = new QWidget(this);
	lights->setFixedHeight(18);
	lights->setStyleSheet("background:#1d2026;");
	QHBoxLayout *row = new QHBoxLayout(lights);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(0);
	QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	mono.setPointSizeF(mono.pointSizeF() * 0.9);
	for (int i = 0; i < VISIBLE; i++) {
		QWidget *cell = new QWidget();
		cell->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		QHBoxLayout *c = new QHBoxLayout(cell);
		c->setContentsMargins(0, 0, 0, 0);
		c->setSpacing(6);
		tiles[i].light = new QLabel();
		tiles[i].light->setFixedSize(10, 10);
		tiles[i].time = new QLabel();
		tiles[i].time->setFont(mono);
		c->addStretch(1);
		c->addWidget(tiles[i].light);
		c->addWidget(tiles[i].time);
		c->addStretch(1);
		tiles[i].light->hide();
		tiles[i].time->hide();
		row->addWidget(cell, 1);
	}
	layout->addWidget(lights);

	blinkTimer = new QTimer(this);
	blinkTimer->setInterval(500);
	connect(blinkTimer, &QTimer::timeout, this, [this]() {
		blinkOn = !blinkOn;
		UpdateLights();
	});

	/* scrolling, shown only when there are more scenes than tiles */
	scrollBar = new QScrollBar(Qt::Horizontal);
	scrollBar->setRange(0, 0);
	scrollBar->setPageStep(1);
	scrollBar->hide();
	connect(scrollBar, &QScrollBar::valueChanged, this, [this](int v) { SetOffset(v); });
	layout->addWidget(scrollBar);

	obs_frontend_add_event_callback(SceneStrip::FrontendEvent, this);

	Refresh();
}

SceneStrip::~SceneStrip()
{
	obs_frontend_remove_event_callback(SceneStrip::FrontendEvent, this);

	if (display && display->GetDisplay())
		obs_display_remove_draw_callback(display->GetDisplay(), SceneStrip::Render, this);

	ClearScenes();
}

/* ---------------------------------------------------------------------- */
/* the scene list                                                          */

void SceneStrip::ClearScenes()
{
	std::vector<OBSWeakSource> old;
	{
		std::lock_guard<std::mutex> lock(mutex);
		old.swap(scenes);
		editing = nullptr;
	}
	renameSignals.clear();
	for (OBSWeakSource &weak : old) {
		OBSSource src = OBSGetStrongRef(weak);
		if (src)
			obs_source_dec_showing(src);
	}
}

void SceneStrip::ConnectRename(obs_source_t *scene)
{
	renameSignals.emplace_back(std::make_unique<OBSSignal>(obs_source_get_signal_handler(scene), "rename",
							       SceneStrip::SceneRenamed, this));
}

void SceneStrip::Refresh()
{
	struct obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);

	std::vector<OBSWeakSource> updated;
	renameSignals.clear();
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *src = list.sources.array[i];
		updated.emplace_back(OBSGetWeakRef(src));
		obs_source_inc_showing(src);
		ConnectRename(src);
	}
	obs_frontend_source_list_free(&list);

	std::vector<OBSWeakSource> old;
	{
		std::lock_guard<std::mutex> lock(mutex);
		old.swap(scenes);
		scenes = std::move(updated);
	}

	for (OBSWeakSource &weak : old) {
		OBSSource src = OBSGetStrongRef(weak);
		if (src)
			obs_source_dec_showing(src);
	}

	int count = Count();
	int maxOffset = std::max(0, count - VISIBLE);
	{
		QSignalBlocker sb(scrollBar);
		scrollBar->setRange(0, maxOffset);
	}
	scrollBar->setVisible(maxOffset > 0);
	SetOffset(std::min(offset, maxOffset));
}

int SceneStrip::Count() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return (int)scenes.size();
}

OBSSource SceneStrip::SceneAt(int index) const
{
	std::lock_guard<std::mutex> lock(mutex);
	if (index < 0 || index >= (int)scenes.size())
		return nullptr;
	return OBSGetStrongRef(scenes[index]);
}

int SceneStrip::IndexOf(obs_source_t *scene) const
{
	std::lock_guard<std::mutex> lock(mutex);
	for (size_t i = 0; i < scenes.size(); i++) {
		OBSSource s = OBSGetStrongRef(scenes[i]);
		if (s.Get() == scene)
			return (int)i;
	}
	return -1;
}

void SceneStrip::SetOffset(int newOffset)
{
	int maxOffset = std::max(0, Count() - VISIBLE);
	newOffset = std::clamp(newOffset, 0, maxOffset);
	{
		std::lock_guard<std::mutex> lock(mutex);
		offset = newOffset;
	}
	if (scrollBar->value() != newOffset) {
		QSignalBlocker sb(scrollBar);
		scrollBar->setValue(newOffset);
	}
	UpdateTiles();
}

void SceneStrip::ScrollTo(int index)
{
	if (index < offset)
		SetOffset(index);
	else if (index >= offset + VISIBLE)
		SetOffset(index - VISIBLE + 1);
}

void SceneStrip::SetEditing(obs_source_t *scene)
{
	std::lock_guard<std::mutex> lock(mutex);
	editing = scene ? OBSGetWeakRef(scene) : nullptr;
}

void SceneStrip::SetRecording(int index, bool on)
{
	int i = index - offset;
	if (i < 0 || i >= VISIBLE)
		return;
	{
		std::lock_guard<std::mutex> lock(mutex);
		tiles[i].recording = on;
	}
	UpdateLights();
}

void SceneStrip::SetStatus(int index, uint64_t elapsedMs, const QString &detail, int level)
{
	int i = index - offset;
	if (i < 0 || i >= VISIBLE)
		return;
	{
		std::lock_guard<std::mutex> lock(mutex);
		tiles[i].status = detail;
		tiles[i].level = level;
	}
	uint64_t sec = elapsedMs / 1000;
	QString t = sec >= 3600 ? QString("%1:%2:%3")
					  .arg(sec / 3600)
					  .arg((sec / 60) % 60, 2, 10, QChar('0'))
					  .arg(sec % 60, 2, 10, QChar('0'))
				: QString("%1:%2").arg(sec / 60, 2, 10, QChar('0')).arg(sec % 60, 2, 10, QChar('0'));
	tiles[i].time->setText(t);
	UpdateLights();
}

/* the light: red and blinking while all is well, amber when frames drop or
 * the disk runs low; the time next to it. The timer runs only while
 * something records. */
void SceneStrip::UpdateLights()
{
	bool any = false;
	for (int i = 0; i < VISIBLE; i++) {
		bool rec;
		int level;
		{
			std::lock_guard<std::mutex> lock(mutex);
			rec = tiles[i].recording;
			level = tiles[i].level;
		}
		QLabel *light = tiles[i].light;
		QLabel *time = tiles[i].time;
		if (!light || !time)
			continue;
		light->setVisible(rec);
		time->setVisible(rec);
		if (!rec)
			continue;
		any = true;
		const char *color = level >= 1 ? "#f0b429" : "#e0413a";
		bool lit = blinkOn || level == 1;
		light->setStyleSheet(QString("border-radius:5px;background:%1;").arg(lit ? color : "#3a3f4b"));
		time->setStyleSheet(QString("color:%1;").arg(level >= 1 ? "#f0b429" : "#d8dbe2"));
	}
	if (any && !blinkTimer->isActive())
		blinkTimer->start();
	else if (!any && blinkTimer->isActive())
		blinkTimer->stop();
}

QString SceneStrip::TileToolTip(int index) const
{
	OBSSource s = SceneAt(index);
	if (!s)
		return QString();
	QString text = QString("%1 · %2").arg(index + 1).arg(QT_UTF8(obs_source_get_name(s)));
	int i = index - offset;
	if (i >= 0 && i < VISIBLE) {
		std::lock_guard<std::mutex> lock(mutex);
		if (tiles[i].recording && !tiles[i].status.isEmpty())
			text += "\n" + tiles[i].status;
	}
	return text;
}

int SceneStrip::TileAt(const QPoint &pos) const
{
	int w = display->width();
	if (w <= 0)
		return -1;
	int i = pos.x() * VISIBLE / w;
	if (i < 0 || i >= VISIBLE)
		return -1;
	int idx = offset + i;
	return idx < Count() ? idx : -1;
}

void SceneStrip::UpdateTiles()
{
	/* the recording marks belong to scenes, not to slots: after a scroll
	 * or a list change they are cleared here and re-applied by the owner */
	{
		std::lock_guard<std::mutex> lock(mutex);
		for (int i = 0; i < VISIBLE; i++) {
			tiles[i].recording = false;
			tiles[i].level = 0;
			tiles[i].status.clear();
		}
	}
	UpdateLights();
	emit TilesChanged();
}

void SceneStrip::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	logicalWidth = display->width();
	UpdateTiles();
}

/* ---------------------------------------------------------------------- */
/* events                                                                  */

void SceneStrip::FrontendEvent(enum obs_frontend_event event, void *data)
{
	SceneStrip *strip = static_cast<SceneStrip *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		QMetaObject::invokeMethod(strip, "Refresh", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_STOPPED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
		QMetaObject::invokeMethod(strip, "UpdateTiles", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
		/* references must be gone before the collection unloads */
		strip->ClearScenes();
		break;
	default:
		break;
	}
}

void SceneStrip::SceneRenamed(void *data, calldata_t *)
{
	SceneStrip *strip = static_cast<SceneStrip *>(data);
	QMetaObject::invokeMethod(strip, "UpdateTiles", Qt::QueuedConnection);
}

/* ---------------------------------------------------------------------- */
/* rendering (graphics thread)                                             */

static inline void paintBox(float x, float y, float cx, float cy, uint32_t color)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *param = gs_effect_get_param_by_name(solid, "color");
	gs_effect_set_color(param, color);

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	while (gs_effect_loop(solid, "Solid")) {
		gs_draw_sprite(nullptr, 0, (uint32_t)cx, (uint32_t)cy);
	}
	gs_matrix_pop();
}

void SceneStrip::Render(void *data, uint32_t cx, uint32_t cy)
{
	SceneStrip *strip = static_cast<SceneStrip *>(data);
	OBSBasic *main = OBSBasic::Get();
	if (!main || cx == 0 || cy == 0)
		return;

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;

	std::vector<OBSSource> visible;
	OBSSource editingSrc;
	{
		std::lock_guard<std::mutex> lock(strip->mutex);
		for (int i = strip->offset; i < strip->offset + VISIBLE && i < (int)strip->scenes.size(); i++)
			visible.push_back(OBSGetStrongRef(strip->scenes[i]));
		editingSrc = OBSGetStrongRef(strip->editing);
	}

	OBSSource previewSrc = main->GetCurrentSceneSource();
	OBSSource programSrc = main->GetProgramSource();

	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "SceneStrip");

	gs_viewport_push();
	gs_projection_push();
	gs_set_viewport(0, 0, (int)cx, (int)cy);
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

	paintBox(0.0f, 0.0f, (float)cx, (float)cy, COLOR_PANEL);

	int logicalWidth = strip->logicalWidth.load();
	float dpr = logicalWidth > 0 ? (float)cx / (float)logicalWidth : 1.0f;
	float pad = TILE_PAD * dpr;
	float frame = 2.0f * dpr;
	float cellW = (float)cx / VISIBLE;

	for (size_t i = 0; i < visible.size(); i++) {
		OBSSource src = visible[i];
		if (!src)
			continue;

		float w = cellW - 2.0f * pad;
		float h = w * 9.0f / 16.0f;
		if (h > (float)cy - 2.0f * pad) {
			h = (float)cy - 2.0f * pad;
			w = h * 16.0f / 9.0f;
		}
		float x0 = (float)i * cellW + (cellW - w) / 2.0f;
		float y0 = ((float)cy - h) / 2.0f;

		bool onProgram = programSrc && src.Get() == programSrc.Get();
		bool onPreview = previewSrc && src.Get() == previewSrc.Get();
		bool isEditing = editingSrc && src.Get() == editingSrc.Get();

		uint32_t color = COLOR_FRAME;
		if (onProgram)
			color = COLOR_PROGRAM;
		else if (onPreview)
			color = COLOR_PREVIEW;
		else if (isEditing)
			color = COLOR_EDITING;

		paintBox(x0, y0, w, h, color);
		float ix = x0 + frame, iy = y0 + frame, iw = w - 2.0f * frame, ih = h - 2.0f * frame;
		if (onProgram && onPreview) {
			/* on both: red outside, green inside */
			paintBox(ix, iy, iw, ih, COLOR_PREVIEW);
			ix += frame;
			iy += frame;
			iw -= 2.0f * frame;
			ih -= 2.0f * frame;
		}
		paintBox(ix, iy, iw, ih, COLOR_BLACK);

		/* the scene, fitted into the inner box */
		int sx, sy;
		float scale;
		GetScaleAndCenterPos((int)ovi.base_width, (int)ovi.base_height, (int)iw, (int)ih, sx, sy, scale);
		int vw = (int)((float)ovi.base_width * scale);
		int vh = (int)((float)ovi.base_height * scale);
		if (vw <= 0 || vh <= 0)
			continue;

		regionStart((int)ix + sx, (int)iy + sy, vw, vh, 0.0f, (float)ovi.base_width, 0.0f,
			    (float)ovi.base_height);
		obs_source_video_render(src);
		regionEnd();
	}

	gs_projection_pop();
	gs_viewport_pop();

	GS_DEBUG_MARKER_END();
}
