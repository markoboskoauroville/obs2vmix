/******************************************************************************
    obs2vmix: the FX rack

    Two widgets over one MasterChain: the skyscraper, a thin line under the
    Record monitor with one small window per slot, and the panel that opens
    beneath the monitors, Blue Cat PatchWork style: eight slots in signal
    order, on/off, plugin, its presets, its own window, a mix knob; Live /
    Live + Record; Bypass; rack presets; MIDI learn.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <utility/MasterChain.hpp>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QMap>

#include <vector>

class QWidget;
class QFrame;
class QLabel;
class QComboBox;
class QPushButton;
class QToolButton;
class QSlider;
class QTimer;
class QEvent;
class MidiIn;

class FxRack : public QObject {
	Q_OBJECT

public:
	explicit FxRack(QWidget *parent);
	~FxRack();

	/* the line under the Record monitor */
	QWidget *Summary() const { return summary; }
	/* the panel, hidden until the line is clicked */
	QWidget *Panel() const { return panel; }

	/* Settings -> Output may have changed which tracks the recording uses */
	void RefreshRecordTracks();

public slots:
	void TogglePanel();

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;

private:
	struct SlotRow {
		QLabel *number = nullptr;
		QToolButton *power = nullptr;
		QLabel *powerBadge = nullptr;
		QComboBox *plugin = nullptr;
		QComboBox *preset = nullptr;
		QPushButton *ui = nullptr;
		QSlider *mix = nullptr;
		QLabel *mixValue = nullptr;
		QLabel *mixBadge = nullptr;
		QToolButton *up = nullptr;
		QToolButton *down = nullptr;
	};

	struct PluginEntry {
		QString name;
		QString path;
	};

	void BuildSummary(QWidget *parent);
	void BuildPanel(QWidget *parent);
	void BuildSlotRow(int i, class QGridLayout *grid);

	void RefreshAll();
	void RefreshSummary();
	void RefreshSlot(int i);
	void RefreshHead();
	void RefreshBadges();

	static std::vector<PluginEntry> ScanPlugins();
	void FillPluginCombo(QComboBox *combo);

	/* rack presets in <profile>/racks/ */
	QString RacksFolder() const;
	QString StateFile() const;
	QString MidiFile() const;
	void RefreshRackList();
	void LoadRack(const QString &name);
	void SaveRack(const QString &name);
	void ScheduleStateSave();
	void SaveState();
	void LoadState();

	/* MIDI learn */
	void SetLearning(bool on);
	void Arm(const QString &controlId);
	void OnMidi(const QString &key, int value);
	void ApplyControl(const QString &controlId, int value, bool isNote);
	void SaveMidiMap();
	void LoadMidiMap();
	void RegisterMappable(QWidget *w, const QString &id, QLabel *badge);
	QString MappedKey(const QString &controlId) const;

	MasterChain chain;
	MidiIn *midi = nullptr;
	std::vector<PluginEntry> plugins;

	QPointer<QWidget> summary;
	QLabel *skyWindows[MasterChain::SLOTS] = {};
	QLabel *chip = nullptr;
	QLabel *caret = nullptr;

	QPointer<QFrame> panel;
	QComboBox *rackCombo = nullptr;
	QPushButton *saveButton = nullptr;
	QPushButton *saveAsButton = nullptr;
	QPushButton *applyLive = nullptr;
	QPushButton *applyBoth = nullptr;
	QLabel *applyBadge = nullptr;
	QPushButton *bypassButton = nullptr;
	QLabel *bypassBadge = nullptr;
	QLabel *midiState = nullptr;
	QPushButton *midiButton = nullptr;
	QLabel *footLabel = nullptr;
	SlotRow rows[MasterChain::SLOTS];

	QString rackName;
	QTimer *stateTimer = nullptr;
	bool refreshing = false;

	struct Mappable {
		QWidget *widget;
		QString id;
		QLabel *badge;
	};
	std::vector<Mappable> mappables;
	QMap<QString, QString> midiMap; /* "cc:21" / "note:36" -> control id */
	bool learning = false;
	QString armed;
};
