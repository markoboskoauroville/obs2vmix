/******************************************************************************
    obs2vmix: the FX rack

    One floating window over one MasterChain, opened from the Final
    monitor's right-click menu (Audio effects…), Blue Cat PatchWork style:
    eight slots in signal order, on/off, plugin, its presets, its own
    window, a mix knob; Live / Live + Record; Bypass; rack presets; MIDI
    learn. OK closes it; it can as well stay open and float.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <utility/MasterChain.hpp>

#include <QObject>
#include <QDialog>
#include <QFrame>
#include <QWidget>
#include <QPointer>
#include <QString>
#include <QMap>

#include <vector>

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

	/* Settings -> Output may have changed which tracks the recording uses */
	void RefreshRecordTracks();

public slots:
	/* show the floating window, or bring it to the front */
	void Open();

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

	void BuildPanel(QWidget *parent);
	void BuildSlotRow(int i, class QGridLayout *grid);

	void RefreshAll();
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

	QPointer<QDialog> window;
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
