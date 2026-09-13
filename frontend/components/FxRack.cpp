/******************************************************************************
    obs2vmix: the FX rack

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "FxRack.hpp"

#include <utility/MidiIn.hpp>
#include <widgets/OBSBasic.hpp>
#include <OBSApp.hpp>

#include <qt-wrappers.hpp>

#include <util/config-file.h>

#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

/* ---------------------------------------------------------------------- */

static const char *SKY_WIN_STYLE = "QLabel{min-width:18px;max-width:18px;min-height:16px;max-height:16px;"
				   "border:1px solid #3a3f4b;border-radius:2px;background:#15171c;"
				   "font-weight:600;font-size:11px;color:%1;qproperty-alignment:AlignCenter;}";

static const char *PANEL_STYLE = "QFrame#obs2vmixRackPanel{background:#15171c;border-top:1px solid #2c303a;}"
				 "QFrame#obs2vmixRackPanel QLabel{color:#8b91a0;font-size:11px;}"
				 "QFrame#obs2vmixRackPanel QLabel[role=\"badge\"]{color:#0b1020;background:#7aa2ff;"
				 "font-size:9px;font-weight:600;padding:0 3px;border-radius:2px;}"
				 "QFrame#obs2vmixRackPanel QWidget[learn=\"true\"]{border:1px dashed #7aa2ff;}"
				 "QFrame#obs2vmixRackPanel QWidget[armed=\"true\"]{border:2px solid #7aa2ff;}"
				 "QFrame#obs2vmixRackPanel QToolButton[power=\"true\"]{border:1px solid #3a3f4b;"
				 "border-radius:11px;min-width:22px;max-width:22px;min-height:22px;max-height:22px;color:#5d6270;}"
				 "QFrame#obs2vmixRackPanel QToolButton[power=\"true\"]:checked{color:#3ec26b;border-color:#3ec26b;}"
				 "QFrame#obs2vmixRackPanel QPushButton[seg=\"true\"]{padding:2px 9px;}"
				 "QFrame#obs2vmixRackPanel QPushButton[seg=\"true\"]:checked{color:#3ec26b;font-weight:600;}"
				 "QFrame#obs2vmixRackPanel QPushButton#obs2vmixApplyBoth:checked{color:#e0413a;}"
				 "QFrame#obs2vmixRackPanel QPushButton#obs2vmixBypass:checked{background:#f0b429;color:#211;}"
				 "QFrame#obs2vmixRackPanel QPushButton#obs2vmixMidi{border:1px solid #7aa2ff;color:#7aa2ff;}"
				 "QFrame#obs2vmixRackPanel QPushButton#obs2vmixMidi:checked{background:#7aa2ff;color:#0b1020;"
				 "font-weight:600;}";

static void repolish(QWidget *w)
{
	w->style()->unpolish(w);
	w->style()->polish(w);
	w->update();
}

/* ---------------------------------------------------------------------- */

FxRack::FxRack(QWidget *parent) : QObject(parent)
{
	plugins = ScanPlugins();

	BuildSummary(parent);
	BuildPanel(parent);

	stateTimer = new QTimer(this);
	stateTimer->setSingleShot(true);
	stateTimer->setInterval(1000);
	connect(stateTimer, &QTimer::timeout, this, &FxRack::SaveState);

	midi = new MidiIn(this);
	connect(midi, &MidiIn::ControlChange, this,
		[this](int cc, int value) { OnMidi(QString("cc:%1").arg(cc), value); });
	connect(midi, &MidiIn::NoteOn, this,
		[this](int note, int velocity) { OnMidi(QString("note:%1").arg(note), velocity); });
	midi->Start();

	LoadMidiMap();
	LoadState();
	RefreshRecordTracks();
	chain.Install();

	RefreshAll();
}

FxRack::~FxRack()
{
	if (stateTimer && stateTimer->isActive()) {
		stateTimer->stop();
		SaveState();
	}
	chain.Remove();
	delete panel;
	delete summary;
}

/* ---------------------------------------------------------------------- */
/* building                                                                */

void FxRack::BuildSummary(QWidget *parent)
{
	summary = new QWidget(parent);
	summary->setObjectName("obs2vmixRackSummary");
	summary->setCursor(Qt::PointingHandCursor);
	summary->setToolTip(QTStr("obs2vmix.Rack.SummaryTT"));
	summary->setFixedHeight(22);
	summary->installEventFilter(this);

	QHBoxLayout *row = new QHBoxLayout(summary);
	row->setContentsMargins(8, 0, 8, 0);
	row->setSpacing(4);

	QLabel *fx = new QLabel(QStringLiteral("FX"));
	fx->setStyleSheet("color:#8b91a0;font-size:11px;letter-spacing:1px;");
	row->addWidget(fx);
	row->addSpacing(4);

	for (int i = 0; i < MasterChain::SLOTS; i++) {
		skyWindows[i] = new QLabel(QStringLiteral("·"));
		skyWindows[i]->setStyleSheet(QString(SKY_WIN_STYLE).arg("#5d6270"));
		row->addWidget(skyWindows[i]);
	}

	row->addStretch(1);

	chip = new QLabel();
	chip->setStyleSheet("color:#8b91a0;font-size:11px;border:1px solid #3a3f4b;border-radius:2px;padding:0 5px;");
	row->addWidget(chip);

	caret = new QLabel(QStringLiteral("▾"));
	caret->setStyleSheet("color:#5d6270;font-size:11px;");
	row->addWidget(caret);
}

void FxRack::BuildPanel(QWidget *parent)
{
	panel = new QFrame(parent);
	panel->setObjectName("obs2vmixRackPanel");
	panel->setStyleSheet(PANEL_STYLE);
	panel->hide();

	QVBoxLayout *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(12, 8, 12, 10);
	layout->setSpacing(8);

	/* head: rack presets · apply to · bypass · midi */
	QHBoxLayout *head = new QHBoxLayout();
	head->setSpacing(8);

	QLabel *rackLabel = new QLabel(QTStr("obs2vmix.Rack.Rack"));
	rackCombo = new QComboBox();
	rackCombo->setMinimumWidth(140);
	rackCombo->setToolTip(QTStr("obs2vmix.Rack.RackTT"));
	saveButton = new QPushButton(QTStr("obs2vmix.Rack.Save"));
	saveAsButton = new QPushButton(QTStr("obs2vmix.Rack.SaveAs"));
	head->addWidget(rackLabel);
	head->addWidget(rackCombo);
	head->addWidget(saveButton);
	head->addWidget(saveAsButton);
	head->addSpacing(12);

	QLabel *applyLabel = new QLabel(QTStr("obs2vmix.Rack.ApplyTo"));
	applyLive = new QPushButton(QTStr("obs2vmix.Rack.Live"));
	applyLive->setCheckable(true);
	applyLive->setProperty("seg", true);
	applyLive->setToolTip(QTStr("obs2vmix.Rack.LiveTT"));
	applyBoth = new QPushButton(QTStr("obs2vmix.Rack.LiveRecord"));
	applyBoth->setObjectName("obs2vmixApplyBoth");
	applyBoth->setCheckable(true);
	applyBoth->setProperty("seg", true);
	applyBoth->setToolTip(QTStr("obs2vmix.Rack.LiveRecordTT"));
	applyBadge = new QLabel();
	applyBadge->setProperty("role", "badge");
	applyBadge->hide();
	head->addWidget(applyLabel);
	head->addWidget(applyLive);
	head->addWidget(applyBoth);
	head->addWidget(applyBadge);
	head->addSpacing(12);

	bypassButton = new QPushButton(QTStr("obs2vmix.Rack.Bypass"));
	bypassButton->setObjectName("obs2vmixBypass");
	bypassButton->setCheckable(true);
	bypassButton->setToolTip(QTStr("obs2vmix.Rack.BypassTT"));
	bypassBadge = new QLabel();
	bypassBadge->setProperty("role", "badge");
	bypassBadge->hide();
	head->addWidget(bypassButton);
	head->addWidget(bypassBadge);

	head->addStretch(1);

	midiState = new QLabel();
	midiState->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	midiButton = new QPushButton(QStringLiteral("MIDI"));
	midiButton->setObjectName("obs2vmixMidi");
	midiButton->setCheckable(true);
	midiButton->setToolTip(QTStr("obs2vmix.Rack.MidiTT"));
	head->addWidget(midiState);
	head->addWidget(midiButton);

	layout->addLayout(head);

	QFrame *line = new QFrame();
	line->setFrameShape(QFrame::HLine);
	line->setStyleSheet("color:#2c303a;");
	layout->addWidget(line);

	/* the slots */
	QGridLayout *grid = new QGridLayout();
	grid->setHorizontalSpacing(6);
	grid->setVerticalSpacing(4);
	for (int i = 0; i < MasterChain::SLOTS; i++)
		BuildSlotRow(i, grid);
	grid->setColumnStretch(3, 1);
	layout->addLayout(grid);

	footLabel = new QLabel();
	footLabel->setStyleSheet("color:#5d6270;font-size:11px;");
	layout->addWidget(footLabel);

	/* wiring */
	connect(rackCombo, &QComboBox::activated, this, [this](int idx) {
		if (refreshing || idx < 0)
			return;
		LoadRack(rackCombo->itemText(idx));
	});
	connect(saveButton, &QPushButton::clicked, this, [this]() {
		if (rackName.isEmpty()) {
			saveAsButton->click();
			return;
		}
		SaveRack(rackName);
	});
	connect(saveAsButton, &QPushButton::clicked, this, [this]() {
		bool ok = false;
		QString name = QInputDialog::getText(panel, QTStr("obs2vmix.Rack.SaveAs"),
						     QTStr("obs2vmix.Rack.Name"), QLineEdit::Normal, rackName, &ok);
		name = name.trimmed();
		if (!ok || name.isEmpty())
			return;
		name.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "_");
		SaveRack(name);
	});
	connect(applyLive, &QPushButton::clicked, this, [this]() {
		chain.SetApply(MasterChain::Apply::Live);
		RefreshHead();
		RefreshSummary();
		ScheduleStateSave();
	});
	connect(applyBoth, &QPushButton::clicked, this, [this]() {
		chain.SetApply(MasterChain::Apply::LiveAndRecord);
		RefreshRecordTracks();
		RefreshHead();
		RefreshSummary();
		ScheduleStateSave();
	});
	connect(bypassButton, &QPushButton::clicked, this, [this](bool checked) {
		chain.SetBypass(checked);
		RefreshHead();
		RefreshSummary();
		ScheduleStateSave();
	});
	connect(midiButton, &QPushButton::clicked, this, [this](bool checked) { SetLearning(checked); });

	RegisterMappable(applyLive, "apply", applyBadge);
	RegisterMappable(applyBoth, "apply", applyBadge);
	RegisterMappable(bypassButton, "byp", bypassBadge);
}

void FxRack::BuildSlotRow(int i, QGridLayout *grid)
{
	SlotRow &r = rows[i];

	r.number = new QLabel(QString::number(i + 1));
	r.number->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	r.number->setAlignment(Qt::AlignCenter);
	r.number->setMinimumWidth(18);

	r.power = new QToolButton();
	r.power->setText(QStringLiteral("⏻"));
	r.power->setCheckable(true);
	r.power->setProperty("power", true);
	r.power->setToolTip(QTStr("obs2vmix.Rack.OnOff"));
	r.powerBadge = new QLabel();
	r.powerBadge->setProperty("role", "badge");
	r.powerBadge->hide();

	r.plugin = new QComboBox();
	r.plugin->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	r.plugin->setMinimumContentsLength(18);
	r.plugin->setToolTip(QTStr("obs2vmix.Rack.PluginTT"));
	FillPluginCombo(r.plugin);

	r.preset = new QComboBox();
	r.preset->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	r.preset->setMinimumContentsLength(10);
	r.preset->setToolTip(QTStr("obs2vmix.Rack.PresetTT"));

	r.ui = new QPushButton(QStringLiteral("UI"));
	r.ui->setToolTip(QTStr("obs2vmix.Rack.UITT"));

	r.mix = new QSlider(Qt::Horizontal);
	r.mix->setRange(0, 100);
	r.mix->setValue(100);
	r.mix->setFixedWidth(80);
	r.mix->setToolTip(QTStr("obs2vmix.Rack.MixTT"));
	r.mixValue = new QLabel(QStringLiteral("100"));
	r.mixValue->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	r.mixValue->setMinimumWidth(28);
	r.mixBadge = new QLabel();
	r.mixBadge->setProperty("role", "badge");
	r.mixBadge->hide();

	r.up = new QToolButton();
	r.up->setText(QStringLiteral("▲"));
	r.up->setAutoRaise(true);
	r.up->setToolTip(QTStr("obs2vmix.Rack.MoveUp"));
	r.down = new QToolButton();
	r.down->setText(QStringLiteral("▼"));
	r.down->setAutoRaise(true);
	r.down->setToolTip(QTStr("obs2vmix.Rack.MoveDown"));

	int col = 0;
	grid->addWidget(r.number, i, col++);
	grid->addWidget(r.power, i, col++);
	grid->addWidget(r.powerBadge, i, col++);
	grid->addWidget(r.plugin, i, col++);
	grid->addWidget(r.preset, i, col++);
	grid->addWidget(r.ui, i, col++);
	grid->addWidget(r.mix, i, col++);
	grid->addWidget(r.mixValue, i, col++);
	grid->addWidget(r.mixBadge, i, col++);
	grid->addWidget(r.up, i, col++);
	grid->addWidget(r.down, i, col++);

	connect(r.power, &QToolButton::clicked, this, [this, i](bool checked) {
		chain.SetOn(i, checked);
		RefreshSlot(i);
		RefreshSummary();
		ScheduleStateSave();
	});
	connect(r.plugin, &QComboBox::activated, this, [this, i](int idx) {
		if (refreshing)
			return;
		QString path = rows[i].plugin->itemData(idx).toString();
		QString name = rows[i].plugin->itemText(idx);
		if (path.isEmpty()) {
			chain.ClearSlot(i);
		} else if (!chain.SetPlugin(i, path.toStdString(), name.toStdString())) {
			OBSMessageBox::warning(panel, QTStr("obs2vmix.Rack.LoadFailed"), name);
		}
		RefreshSlot(i);
		RefreshSummary();
		ScheduleStateSave();
	});
	connect(r.preset, &QComboBox::activated, this, [this, i](int idx) {
		if (refreshing)
			return;
		VSTPlugin *p = chain.Plugin(i);
		if (p && idx >= 0)
			p->setProgram(idx);
		ScheduleStateSave();
	});
	connect(r.ui, &QPushButton::clicked, this, [this, i]() {
		VSTPlugin *p = chain.Plugin(i);
		if (p)
			p->openEditor();
	});
	connect(r.mix, &QSlider::valueChanged, this, [this, i](int v) {
		if (refreshing)
			return;
		chain.SetMix(i, (float)v / 100.0f);
		rows[i].mixValue->setText(QString::number(v));
		ScheduleStateSave();
	});
	connect(r.up, &QToolButton::clicked, this, [this, i]() {
		chain.Move(i, i - 1);
		RefreshAll();
		ScheduleStateSave();
	});
	connect(r.down, &QToolButton::clicked, this, [this, i]() {
		chain.Move(i, i + 1);
		RefreshAll();
		ScheduleStateSave();
	});

	RegisterMappable(r.power, QString("pw%1").arg(i), r.powerBadge);
	RegisterMappable(r.mix, QString("mix%1").arg(i), r.mixBadge);
}

/* ---------------------------------------------------------------------- */
/* plugins on disk, the same folders obs-vst searches                      */

std::vector<FxRack::PluginEntry> FxRack::ScanPlugins()
{
	QStringList dirs;
	QStringList filters;

#ifdef __APPLE__
	dirs << "/Library/Audio/Plug-Ins/VST/" << QDir::homePath() + "/Library/Audio/Plug-ins/VST/";
	filters << "*.vst";
#elif defined(_WIN32)
	dirs << qEnvironmentVariable("ProgramFiles") + "/Steinberg/VstPlugins/"
	     << qEnvironmentVariable("CommonProgramFiles") + "/Steinberg/Shared Components/"
	     << qEnvironmentVariable("CommonProgramFiles") + "/VST2"
	     << qEnvironmentVariable("CommonProgramFiles") + "/Steinberg/VST2"
	     << qEnvironmentVariable("CommonProgramFiles") + "/VSTPlugins/"
	     << qEnvironmentVariable("ProgramFiles") + "/VSTPlugins/";
	filters << "*.dll";
#else
	QString vstPath = qEnvironmentVariable("VST_PATH");
	if (!vstPath.isEmpty()) {
		dirs = vstPath.split(":");
	} else {
		QString home = QDir::homePath();
		dirs << "/usr/lib/vst/" << "/usr/lib/lxvst/" << "/usr/lib/linux_vst/" << "/usr/lib64/vst/"
		     << "/usr/lib64/lxvst/" << "/usr/lib64/linux_vst/" << "/usr/local/lib/vst/"
		     << "/usr/local/lib/lxvst/" << "/usr/local/lib/linux_vst/" << "/usr/local/lib64/vst/"
		     << "/usr/local/lib64/lxvst/" << "/usr/local/lib64/linux_vst/" << home + "/.vst/"
		     << home + "/.lxvst/";
	}
	filters << "*.so" << "*.o";
#endif

	std::vector<PluginEntry> found;
	for (const QString &d : dirs) {
		QDir dir(d);
		if (!dir.exists())
			continue;
		dir.setNameFilters(filters);
		QDirIterator it(dir, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
		while (it.hasNext()) {
			QString path = it.next();
			QString name = it.fileName();
			for (const QString &f : filters)
				name.remove(f.mid(1), Qt::CaseInsensitive);
			found.push_back({name, path});
		}
	}
	std::stable_sort(found.begin(), found.end(),
			 [](const PluginEntry &a, const PluginEntry &b) { return a.name.toLower() < b.name.toLower(); });
	return found;
}

void FxRack::FillPluginCombo(QComboBox *combo)
{
	combo->clear();
	combo->addItem(QTStr("obs2vmix.Rack.Empty"), QString());
	for (const PluginEntry &p : plugins)
		combo->addItem(p.name, p.path);
}

/* ---------------------------------------------------------------------- */
/* refresh                                                                 */

void FxRack::RefreshAll()
{
	for (int i = 0; i < MasterChain::SLOTS; i++)
		RefreshSlot(i);
	RefreshHead();
	RefreshSummary();
	RefreshRackList();
	RefreshBadges();
}

void FxRack::RefreshSlot(int i)
{
	SlotRow &r = rows[i];
	refreshing = true;

	std::string path = chain.SlotPath(i);
	bool has = !path.empty();
	VSTPlugin *p = has ? chain.Plugin(i) : nullptr;

	int idx = 0;
	if (has) {
		idx = r.plugin->findData(QString::fromStdString(path));
		if (idx < 0) {
			/* a plugin from a preset that is not in the scanned folders */
			r.plugin->addItem(QString::fromStdString(chain.SlotName(i)), QString::fromStdString(path));
			idx = r.plugin->count() - 1;
		}
	}
	r.plugin->setCurrentIndex(idx);

	r.power->setChecked(chain.SlotOn(i));
	r.power->setEnabled(has);

	r.preset->clear();
	if (p) {
		int n = std::min(p->numPrograms(), 128);
		for (int k = 0; k < n; k++) {
			std::string name = p->programName(k);
			r.preset->addItem(name.empty() ? QString::number(k + 1) : QString::fromStdString(name));
		}
		int cur = p->getProgram();
		if (cur >= 0 && cur < n)
			r.preset->setCurrentIndex(cur);
	}
	r.preset->setEnabled(p && r.preset->count() > 0);
	r.ui->setEnabled(has);

	int mix = (int)(chain.SlotMix(i) * 100.0f + 0.5f);
	r.mix->setValue(mix);
	r.mixValue->setText(QString::number(mix));
	r.mix->setEnabled(has);

	r.up->setEnabled(i > 0);
	r.down->setEnabled(i < MasterChain::SLOTS - 1);

	refreshing = false;
}

void FxRack::RefreshHead()
{
	bool both = chain.GetApply() == MasterChain::Apply::LiveAndRecord;
	applyLive->setChecked(!both);
	applyBoth->setChecked(both);
	bypassButton->setChecked(chain.GetBypass());
	bypassButton->setText(chain.GetBypass() ? QTStr("obs2vmix.Rack.Bypassed") : QTStr("obs2vmix.Rack.Bypass"));

	int used = 0;
	for (int i = 0; i < MasterChain::SLOTS; i++)
		if (!chain.IsEmpty(i))
			used++;
	footLabel->setText(QTStr("obs2vmix.Rack.Foot")
				   .arg(used)
				   .arg(MasterChain::SLOTS)
				   .arg(both ? QTStr("obs2vmix.Rack.FootBoth") : QTStr("obs2vmix.Rack.FootLive"))
				   .arg(midiMap.size()));

	if (!learning) {
		QStringList devs = midi ? midi->Devices() : QStringList();
		midiState->setText(devs.isEmpty() ? QTStr("obs2vmix.Rack.NoController") : devs.join(", "));
	}
}

void FxRack::RefreshSummary()
{
	bool bypass = chain.GetBypass();
	for (int i = 0; i < MasterChain::SLOTS; i++) {
		std::string name = chain.SlotName(i);
		QLabel *w = skyWindows[i];
		if (name.empty()) {
			w->setText(QStringLiteral("·"));
			w->setStyleSheet(QString(SKY_WIN_STYLE).arg("#5d6270"));
			w->setToolTip(QTStr("obs2vmix.Rack.Empty"));
			continue;
		}
		QString n = QString::fromStdString(name).trimmed();
		w->setText(n.isEmpty() ? QStringLiteral("?") : n.left(1).toUpper());
		bool lit = chain.SlotOn(i) && !bypass;
		w->setStyleSheet(QString(SKY_WIN_STYLE).arg(lit ? "#3ec26b" : "#5d6270"));
		w->setToolTip(n);
	}

	bool both = chain.GetApply() == MasterChain::Apply::LiveAndRecord;
	chip->setText(bypass ? QTStr("obs2vmix.Rack.ChipBypass")
			     : both ? QTStr("obs2vmix.Rack.ChipBoth")
				    : QTStr("obs2vmix.Rack.ChipLive"));
	const char *color = bypass ? "#f0b429" : both ? "#e0413a" : "#8b91a0";
	chip->setStyleSheet(QString("color:%1;font-size:11px;border:1px solid #3a3f4b;border-radius:2px;padding:0 5px;")
				    .arg(color));
	caret->setText(panel && panel->isVisible() ? QStringLiteral("▴") : QStringLiteral("▾"));
}

void FxRack::TogglePanel()
{
	if (!panel)
		return;
	panel->setVisible(!panel->isVisible());
	RefreshSummary();
}

bool FxRack::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == summary.data() && event->type() == QEvent::MouseButtonPress) {
		TogglePanel();
		return true;
	}
	if (learning && event->type() == QEvent::MouseButtonPress) {
		for (const Mappable &m : mappables) {
			if (m.widget == obj) {
				Arm(m.id);
				return true;
			}
		}
	}
	return QObject::eventFilter(obj, event);
}

/* which tracks the recording reads, for Live + Record */
void FxRack::RefreshRecordTracks()
{
	OBSBasic *main = OBSBasic::Get();
	if (!main)
		return;
	config_t *config = main->Config();
	const char *mode = config_get_string(config, "Output", "Mode");
	bool advanced = mode && strcmp(mode, "Advanced") == 0;
	int tracks = (int)config_get_int(config, advanced ? "AdvOut" : "SimpleOutput", "RecTracks");
	if (tracks <= 0)
		tracks = 1;
	chain.SetRecordTracks((uint32_t)tracks);
}

/* ---------------------------------------------------------------------- */
/* files in the profile                                                    */

static QString profileFolder()
{
	OBSBasic *main = OBSBasic::Get();
	if (!main)
		return QString();
	return QString::fromStdString(main->GetCurrentProfile().path.u8string());
}

QString FxRack::RacksFolder() const
{
	return profileFolder() + "/racks";
}

QString FxRack::StateFile() const
{
	return profileFolder() + "/obs2vmix-rack.json";
}

QString FxRack::MidiFile() const
{
	return profileFolder() + "/obs2vmix-midi.json";
}

void FxRack::RefreshRackList()
{
	refreshing = true;
	rackCombo->clear();
	QDir dir(RacksFolder());
	QStringList names;
	for (const QFileInfo &fi : dir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Name))
		names << fi.completeBaseName();
	if (!rackName.isEmpty() && !names.contains(rackName))
		names.prepend(rackName);
	if (names.isEmpty())
		names << QTStr("obs2vmix.Rack.Unsaved");
	rackCombo->addItems(names);
	int idx = rackCombo->findText(rackName);
	rackCombo->setCurrentIndex(idx >= 0 ? idx : 0);
	refreshing = false;
}

void FxRack::LoadRack(const QString &name)
{
	QString file = RacksFolder() + "/" + name + ".json";
	OBSDataAutoRelease data = obs_data_create_from_json_file(QT_TO_UTF8(file));
	if (!data)
		return;
	chain.Load(data);
	rackName = name;
	RefreshAll();
	ScheduleStateSave();
}

void FxRack::SaveRack(const QString &name)
{
	QDir().mkpath(RacksFolder());
	QString file = RacksFolder() + "/" + name + ".json";
	OBSDataAutoRelease data = chain.Save();
	if (!obs_data_save_json_safe(data, QT_TO_UTF8(file), "tmp", "bak")) {
		OBSMessageBox::warning(panel, QTStr("obs2vmix.Rack.Save"), file);
		return;
	}
	rackName = name;
	RefreshRackList();
	ScheduleStateSave();
}

void FxRack::ScheduleStateSave()
{
	if (stateTimer)
		stateTimer->start();
}

/* the live rack survives a restart, separately from the named presets */
void FxRack::SaveState()
{
	QString file = StateFile();
	if (file.isEmpty())
		return;
	OBSDataAutoRelease data = chain.Save();
	obs_data_set_string(data, "rack", QT_TO_UTF8(rackName));
	obs_data_save_json_safe(data, QT_TO_UTF8(file), "tmp", "bak");
}

void FxRack::LoadState()
{
	QString file = StateFile();
	if (file.isEmpty())
		return;
	OBSDataAutoRelease data = obs_data_create_from_json_file(QT_TO_UTF8(file));
	if (!data)
		return;
	chain.Load(data);
	rackName = QString::fromUtf8(obs_data_get_string(data, "rack"));
}

/* ---------------------------------------------------------------------- */
/* MIDI learn, the way Ableton Live does it                                */

void FxRack::RegisterMappable(QWidget *w, const QString &id, QLabel *badge)
{
	w->installEventFilter(this);
	mappables.push_back({w, id, badge});
}

QString FxRack::MappedKey(const QString &controlId) const
{
	for (auto it = midiMap.constBegin(); it != midiMap.constEnd(); ++it)
		if (it.value() == controlId)
			return it.key();
	return QString();
}

static QString keyLabel(const QString &key)
{
	if (key.startsWith("cc:"))
		return "CC " + key.mid(3);
	if (key.startsWith("note:"))
		return "N " + key.mid(5);
	return key;
}

void FxRack::RefreshBadges()
{
	for (const Mappable &m : mappables) {
		QString key = MappedKey(m.id);
		bool isArmed = learning && armed == m.id;
		m.widget->setProperty("learn", learning);
		m.widget->setProperty("armed", isArmed);
		repolish(m.widget);
		if (m.badge) {
			m.badge->setText(isArmed ? QStringLiteral("…") : keyLabel(key));
			m.badge->setVisible(learning || !key.isEmpty());
		}
		if (!key.isEmpty())
			m.widget->setToolTip(m.widget->toolTip().section(" [", 0, 0) + " [" + keyLabel(key) + "]");
	}
}

void FxRack::SetLearning(bool on)
{
	learning = on;
	armed.clear();
	midiButton->setChecked(on);
	if (on) {
		if (midi && !midi->Running())
			midi->Start();
		if (!MidiIn::Available())
			midiState->setText(QTStr("obs2vmix.Rack.NoMidiBuild"));
		else if (midi && midi->Devices().isEmpty())
			midiState->setText(QTStr("obs2vmix.Rack.NoController"));
		else
			midiState->setText(QTStr("obs2vmix.Rack.LearnClick"));
	}
	RefreshHead();
	RefreshBadges();
}

void FxRack::Arm(const QString &controlId)
{
	armed = controlId;
	midiState->setText(QTStr("obs2vmix.Rack.LearnTouch"));
	RefreshBadges();
}

void FxRack::OnMidi(const QString &key, int value)
{
	if (learning && !armed.isEmpty()) {
		/* one controller per control */
		for (auto it = midiMap.begin(); it != midiMap.end();) {
			if (it.value() == armed)
				it = midiMap.erase(it);
			else
				++it;
		}
		midiMap[key] = armed;
		midiState->setText(armed + QStringLiteral(" ← ") + keyLabel(key));
		armed.clear();
		SaveMidiMap();
		RefreshBadges();
		RefreshHead();
		return;
	}

	auto it = midiMap.constFind(key);
	if (it == midiMap.constEnd())
		return;
	ApplyControl(it.value(), value, key.startsWith("note:"));
}

void FxRack::ApplyControl(const QString &id, int value, bool isNote)
{
	bool pressed = isNote ? true : value >= 64;

	if (id == "apply") {
		bool both = chain.GetApply() == MasterChain::Apply::LiveAndRecord;
		bool want = isNote ? !both : pressed;
		chain.SetApply(want ? MasterChain::Apply::LiveAndRecord : MasterChain::Apply::Live);
		if (want)
			RefreshRecordTracks();
	} else if (id == "byp") {
		chain.SetBypass(isNote ? !chain.GetBypass() : pressed);
	} else if (id.startsWith("pw")) {
		int i = id.mid(2).toInt();
		if (i >= 0 && i < MasterChain::SLOTS)
			chain.SetOn(i, isNote ? !chain.SlotOn(i) : pressed);
		RefreshSlot(i);
	} else if (id.startsWith("mix")) {
		if (isNote)
			return;
		int i = id.mid(3).toInt();
		if (i >= 0 && i < MasterChain::SLOTS) {
			chain.SetMix(i, (float)value / 127.0f);
			RefreshSlot(i);
		}
	} else {
		return;
	}
	RefreshHead();
	RefreshSummary();
	ScheduleStateSave();
}

void FxRack::SaveMidiMap()
{
	QString file = MidiFile();
	if (file.isEmpty())
		return;
	OBSDataAutoRelease data = obs_data_create();
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (auto it = midiMap.constBegin(); it != midiMap.constEnd(); ++it) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "key", QT_TO_UTF8(it.key()));
		obs_data_set_string(item, "control", QT_TO_UTF8(it.value()));
		obs_data_array_push_back(arr, item);
	}
	obs_data_set_array(data, "map", arr);
	obs_data_save_json_safe(data, QT_TO_UTF8(file), "tmp", "bak");
}

void FxRack::LoadMidiMap()
{
	midiMap.clear();
	QString file = MidiFile();
	if (file.isEmpty())
		return;
	OBSDataAutoRelease data = obs_data_create_from_json_file(QT_TO_UTF8(file));
	if (!data)
		return;
	OBSDataArrayAutoRelease arr = obs_data_get_array(data, "map");
	size_t n = arr ? obs_data_array_count(arr) : 0;
	for (size_t i = 0; i < n; i++) {
		OBSDataAutoRelease item = obs_data_array_item(arr, i);
		QString key = QString::fromUtf8(obs_data_get_string(item, "key"));
		QString control = QString::fromUtf8(obs_data_get_string(item, "control"));
		if (!key.isEmpty() && !control.isEmpty())
			midiMap[key] = control;
	}
}
