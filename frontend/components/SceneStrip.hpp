/******************************************************************************
    obs2vmix: the scene strip

    Five live thumbnails in a row instead of a timeline, and nothing else:
    no names, no buttons, no text. One OBSQTDisplay renders every visible
    scene (the way the multiview does); the name is a tooltip, the actions
    are a right-click menu. Under each thumbnail one reserved line: empty,
    or, while the scene records, a round red light and the time recorded.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <obs.hpp>
#include <obs-frontend-api.h>

#include <QWidget>
#include <QPointer>
#include <QPoint>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class OBSQTDisplay;
class QScrollBar;
class QLabel;
class QTimer;
class SceneStripDisplay;

class SceneStrip : public QWidget {
	Q_OBJECT

public:
	static constexpr int VISIBLE = 5;

	explicit SceneStrip(QWidget *parent = nullptr);
	~SceneStrip();

public slots:
	/* re-read the scene list from the frontend (UI thread) */
	void Refresh();
	/* after a scroll or a list change: the per-tile state is re-applied */
	void UpdateTiles();

public:
	int Count() const;
	OBSSource SceneAt(int index) const;
	int IndexOf(obs_source_t *scene) const;

	/* make index visible, scrolling the strip if needed */
	void ScrollTo(int index);

	/* the scene whose editor is open gets a grey frame */
	void SetEditing(obs_source_t *scene);

	/* per-scene recording state: the light and the time under the
	 * thumbnail, the detail line in the tooltip */
	void SetRecording(int index, bool on);
	void SetStatus(int index, uint64_t elapsedMs, const QString &detail, int level);

	/* the tooltip for a tile: "3 · Cam 1", then the recording line */
	QString TileToolTip(int index) const;

protected:
	void resizeEvent(QResizeEvent *event) override;

signals:
	void SceneClicked(OBSSource scene);
	void SceneDoubleClicked(OBSSource scene);
	void SceneMenuRequested(OBSSource scene, const QPoint &globalPos);
	void TilesChanged();

private:
	struct TileState {
		bool recording = false;
		int level = 0;
		QString status;
		QLabel *light = nullptr;
		QLabel *time = nullptr;
	};

	static void Render(void *data, uint32_t cx, uint32_t cy);
	static void FrontendEvent(enum obs_frontend_event event, void *data);
	static void SceneRenamed(void *data, calldata_t *cd);

	void ClearScenes();
	void SetOffset(int offset);
	int TileAt(const QPoint &pos) const;
	void ConnectRename(obs_source_t *scene);

	SceneStripDisplay *display = nullptr;
	QScrollBar *scrollBar = nullptr;
	QTimer *blinkTimer = nullptr;
	bool blinkOn = true;
	void UpdateLights();

	mutable std::mutex mutex;
	std::vector<OBSWeakSource> scenes;
	std::vector<std::unique_ptr<OBSSignal>> renameSignals;
	TileState tiles[VISIBLE];
	int offset = 0;
	OBSWeakSource editing;
	std::atomic<int> logicalWidth{0};

	friend class SceneStripDisplay;
};
