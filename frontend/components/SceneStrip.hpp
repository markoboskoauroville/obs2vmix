/******************************************************************************
    obs2vmix: the scene strip

    Five live thumbnails in a row instead of a timeline. One OBSQTDisplay
    renders every visible scene (the way the multiview does), the names and
    the per-scene record controls are Qt widgets under it.

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

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class OBSQTDisplay;
class QLabel;
class QScrollBar;
class QToolButton;
class QHBoxLayout;
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
	/* names, colours and elision of the labels under the thumbnails */
	void UpdateTiles();

public:

	int Count() const;
	OBSSource SceneAt(int index) const;
	int IndexOf(obs_source_t *scene) const;

	/* make index visible, scrolling the strip if needed */
	void ScrollTo(int index);

	/* the scene whose editor is open gets a dashed frame */
	void SetEditing(obs_source_t *scene);

	/* per-scene recording state, drawn by the tile (phase 6 drives it) */
	void SetRecording(int index, bool on);
	void SetStatus(int index, const QString &text, int level);

protected:
	void resizeEvent(QResizeEvent *event) override;

signals:
	void SceneClicked(OBSSource scene);
	void SceneDoubleClicked(OBSSource scene);
	void RecordClicked(OBSSource scene);
	/* after a scroll or list change: record marks and status lines must be re-applied */
	void TilesChanged();

private:
	struct Tile {
		QWidget *box = nullptr;
		QToolButton *rec = nullptr;
		QLabel *name = nullptr;
		QLabel *status = nullptr;
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
	QHBoxLayout *tileLayout = nullptr;
	QLabel *countLabel = nullptr;
	std::vector<Tile> tiles;

	mutable std::mutex mutex;
	std::vector<OBSWeakSource> scenes;
	std::vector<std::unique_ptr<OBSSignal>> renameSignals;
	int offset = 0;
	OBSWeakSource editing;
	std::atomic<int> logicalWidth{0};

	friend class SceneStripDisplay;
};
