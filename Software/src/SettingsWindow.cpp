/*
 * SettingsWindow.cpp
 *
 *	Created on: 26.07.2010
 *		Author: Mike Shatohin (brunql)
 *		Project: Lightpack
 *
 *	Lightpack is very simple implementation of the backlight for a laptop
 *
 *	Copyright (c) 2010, 2011 Mike Shatohin, mikeshatohin [at] gmail.com
 *
 *	Lightpack is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Lightpack is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program.	If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <QtAlgorithms>
#include <QStatusBar>
#include <QMenu>
#include "LightpackApplication.hpp"

#include "SettingsWindow.hpp"
#include "ui_SettingsWindow.h"

#include "Settings.hpp"
#include "ColorButton.hpp"
#include "LedDeviceManager.hpp"
#include "enums.hpp"
#include "debug.h"
#include "LogWriter.hpp"
#include "Plugin.hpp"
#include "systrayicon/SysTrayIcon.hpp"
#include "version.h"
#include <QStringBuilder>
#include <QScrollBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCheckBox>
#include <QComboBox>
#include <QRegularExpression>
#include <QPainter>
#include <QPainterPath>
#include <QCoreApplication>
#include <QSettings>
#include <QListView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include "PrismatikMath.hpp"
#include "CameraCalibrationDialog.hpp"

using namespace SettingsScope;

// ----------------------------------------------------------------------------
// Lightpack settings window
// ----------------------------------------------------------------------------
namespace {
class ChairoTitleBar : public QWidget
{
public:
	explicit ChairoTitleBar(QMainWindow *window) : QWidget(window), m_window(window)
	{
		setObjectName(QStringLiteral("chairoTitleBar"));
		setFixedHeight(42);
		auto *layout = new QHBoxLayout(this);
		layout->setContentsMargins(14, 0, 6, 0);
		layout->setSpacing(8);
		auto *mark = new QLabel(QStringLiteral("CL"), this);
		mark->setObjectName(QStringLiteral("titleMark"));
		auto *title = new QLabel(QStringLiteral("ChairoLight"), this);
		title->setObjectName(QStringLiteral("titleText"));
		auto *closeButton = new QPushButton(QString::fromUtf8(u8"×"), this);
		closeButton->setObjectName(QStringLiteral("titleCloseButton"));
		closeButton->setToolTip(QString::fromUtf8(u8"Закрыть"));
		closeButton->setFixedSize(34, 30);
		layout->addWidget(mark);
		layout->addWidget(title);
		layout->addStretch();
		layout->addWidget(closeButton);
		QObject::connect(closeButton, &QPushButton::clicked, window, &QWidget::close);
	}

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton) {
			m_dragOffset = event->globalPos() - m_window->frameGeometry().topLeft();
			event->accept();
		}
	}
	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (event->buttons() & Qt::LeftButton) {
			m_window->move(event->globalPos() - m_dragOffset);
			event->accept();
		}
	}

private:
	QMainWindow *m_window;
	QPoint m_dragOffset;
};

class AmbilightPreview : public QWidget
{
public:
	explicit AmbilightPreview(QWidget *parent = nullptr) : QWidget(parent)
	{
		setObjectName(QStringLiteral("ambilightPreview"));
		setMinimumSize(250, 180);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF area = rect().adjusted(12, 12, -12, -12);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor("#15151c"));
		p.drawRoundedRect(area, 14, 14);

		const QRectF screen(area.left() + area.width() * .16, area.top() + area.height() * .18,
			area.width() * .68, area.height() * .55);
		for (int width = 18; width >= 5; width -= 3) {
			const int alpha = 10 + (18 - width) * 4;
			QPen glow(QColor(124, 92, 255, alpha), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
			p.setPen(glow);
			p.drawRoundedRect(screen, 8, 8);
		}
		QLinearGradient edge(screen.topLeft(), screen.topRight());
		edge.setColorAt(0.0, QColor("#ff5f87"));
		edge.setColorAt(0.5, QColor("#7c5cff"));
		edge.setColorAt(1.0, QColor("#3ad8ff"));
		p.setPen(QPen(QBrush(edge), 3));
		p.setBrush(QColor("#101015"));
		p.drawRoundedRect(screen, 7, 7);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor("#343442"));
		p.drawRoundedRect(QRectF(screen.center().x() - 4, screen.bottom(), 8, 22), 2, 2);
		p.drawRoundedRect(QRectF(screen.center().x() - 30, screen.bottom() + 19, 60, 7), 3, 3);
	}
};

#ifdef Q_OS_WIN
const QString BaudrateWarningSign = QStringLiteral(" <b>!!!</b>");
#else
const QString BaudrateWarningSign = QStringLiteral(" вљ пёЏ");
#endif

QIcon modernNavigationIcon(int type)
{
	auto draw = [type](const QColor &color, const QColor &background) {
		QPixmap pixmap(40, 40);
		pixmap.fill(Qt::transparent);
		QPainter p(&pixmap);
		p.setRenderHint(QPainter::Antialiasing);
		if (background.alpha() > 0) {
			p.setPen(Qt::NoPen);
			p.setBrush(background);
			p.drawRoundedRect(QRectF(2, 2, 36, 36), 9, 9);
		}
		QPen pen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		switch (type) {
		case 0: // home
			p.drawPolyline(QPolygonF() << QPointF(9, 20) << QPointF(20, 10) << QPointF(31, 20));
			p.drawRoundedRect(QRectF(12, 19, 16, 13), 2, 2);
			p.drawLine(QPointF(18, 32), QPointF(18, 24));
			p.drawLine(QPointF(18, 24), QPointF(23, 24));
			p.drawLine(QPointF(23, 24), QPointF(23, 32));
			break;
		case 1: // device / chip
			p.drawRoundedRect(QRectF(12, 12, 16, 16), 3, 3);
			p.drawRoundedRect(QRectF(16, 16, 8, 8), 1.5, 1.5);
			for (int v : {14, 20, 26}) {
				p.drawLine(QPointF(v, 8), QPointF(v, 12)); p.drawLine(QPointF(v, 28), QPointF(v, 32));
				p.drawLine(QPointF(8, v), QPointF(12, v)); p.drawLine(QPointF(28, v), QPointF(32, v));
			}
			break;
		case 2: // profiles
			p.drawEllipse(QRectF(16, 9, 8, 8));
			p.drawArc(QRectF(11, 18, 18, 14), 15 * 16, 150 * 16);
			p.drawArc(QRectF(11, 18, 18, 14), 195 * 16, 150 * 16);
			break;
		case 3: // plugins / grid
			for (int y : {11, 22}) for (int x : {11, 22}) p.drawRoundedRect(QRectF(x, y, 7, 7), 2, 2);
			break;
		case 4: // expert / sliders
			p.drawLine(QPointF(9, 12), QPointF(31, 12)); p.drawEllipse(QRectF(15, 9, 6, 6));
			p.drawLine(QPointF(9, 20), QPointF(31, 20)); p.drawEllipse(QRectF(23, 17, 6, 6));
			p.drawLine(QPointF(9, 28), QPointF(31, 28)); p.drawEllipse(QRectF(12, 25, 6, 6));
			break;
		default: // about
			p.drawEllipse(QRectF(10, 10, 20, 20));
			p.drawPoint(QPointF(20, 15)); p.drawLine(QPointF(20, 20), QPointF(20, 26));
			break;
		}
		return pixmap;
	};
	QIcon icon;
	icon.addPixmap(draw(QColor("#a7a7b3"), Qt::transparent), QIcon::Normal, QIcon::Off);
	icon.addPixmap(draw(QColor("#ffffff"), QColor("#765cff")), QIcon::Selected, QIcon::Off);
	return icon;
}
}
const QString SettingsWindow::DeviceFirmvareVersionUndef = QStringLiteral("undef");
const QString SettingsWindow::LightpackDownloadsPageUrl = QStringLiteral("http://code.google.com/p/lightpack/downloads/list");

// Indexes of supported modes listed in ui->comboBox_Modes and ui->stackedWidget_Modes
const int SettingsWindow::GrabModeIndex = 0;
const int SettingsWindow::MoodLampModeIndex = 1;
#ifdef SOUNDVIZ_SUPPORT
const int SettingsWindow::SoundVisualizeModeIndex = 2;
#endif

SettingsWindow::SettingsWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::SettingsWindow),
	m_deviceFirmwareVersion(DeviceFirmvareVersionUndef)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << "thread id: " << this->thread()->currentThreadId();

	m_trayIcon = NULL;

	ui->setupUi(this);
	setMinimumSize(980, 850);
	resize(1080, 900);
	ui->label_3->setObjectName(QStringLiteral("aboutDescription"));
	ui->label_3->setText(QString::fromUtf8(u8"<div style='font-size:12pt;line-height:150%'><b>ChairoLight</b> — приложение для адаптивной фоновой подсветки экрана.<br/><br/>Сделано Oidzuri на основе открытого проекта <b>Prismatik</b>.</div>"));
	for (QWidget *aboutWidget : ui->tabAbout->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
		if (aboutWidget != ui->label_3)
			aboutWidget->hide();
	}
	ui->label_3->setMinimumHeight(220);
	ui->label_3->setAlignment(Qt::AlignCenter);
	ui->comboBox_GrabColorMode->addItem(tr("Legacy average"), static_cast<int>(Grab::Calculations::ColorProcessingModeLegacy));
	ui->comboBox_GrabColorMode->addItem(tr("Balanced"), static_cast<int>(Grab::Calculations::ColorProcessingModeBalanced));
	ui->comboBox_GrabColorMode->addItem(tr("Accurate edges"), static_cast<int>(Grab::Calculations::ColorProcessingModeAccurate));
	ui->comboBox_GrabColorMode->addItem(tr("Cinema vivid"), static_cast<int>(Grab::Calculations::ColorProcessingModeCinema));
	m_labelScenePreset = new QLabel(tr("Scene preset:"), this);
	m_comboScenePreset = new QComboBox(this);
	m_comboScenePreset->addItem(tr("Auto (recommended)"), static_cast<int>(Grab::Calculations::ScenePresetAuto));
	m_comboScenePreset->addItem(tr("Neutral"), static_cast<int>(Grab::Calculations::ScenePresetNeutral));
	m_comboScenePreset->addItem(tr("Anime"), static_cast<int>(Grab::Calculations::ScenePresetAnime));
	m_comboScenePreset->addItem(tr("Games"), static_cast<int>(Grab::Calculations::ScenePresetGames));
	m_comboScenePreset->addItem(tr("Cinema"), static_cast<int>(Grab::Calculations::ScenePresetCinema));
	m_checkSmartCalibration = new QCheckBox(tr("Smart color calibration"), this);
	m_checkStartWithWindows = new QCheckBox(tr("Start with Windows"), this);
	m_checkStartWithWindows->setText(QString::fromUtf8(u8"Автозапуск с Windows"));
	auto *cameraCalibrationButton = new QPushButton(tr("Experimental webcam calibration"), this);
	cameraCalibrationButton->setObjectName(QStringLiteral("cameraCalibrationButton"));
	cameraCalibrationButton->setMinimumHeight(40);
	cameraCalibrationButton->setToolTip(tr("Webcam auto-exposure may distort measurements. Manual wall correction is recommended for final tuning."));
	m_comboScenePreset->setMinimumWidth(220);
	auto *dashboardHost = new QWidget(this);
	dashboardHost->setObjectName(QStringLiteral("dashboardHost"));
	auto *dashboardLayout = new QVBoxLayout(dashboardHost);
	dashboardLayout->setContentsMargins(8, 4, 8, 8);
	dashboardLayout->setSpacing(16);
	auto *dashboardScroll = new QScrollArea(this);
	dashboardScroll->setObjectName(QStringLiteral("dashboardScroll"));
	dashboardScroll->setWidgetResizable(true);
	dashboardScroll->setFrameShape(QFrame::NoFrame);
	dashboardScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	dashboardScroll->setWidget(dashboardHost);
	ui->gridLayout_5->removeWidget(ui->stackedWidget_LightpackModes);
	ui->gridLayout_5->addWidget(ui->stackedWidget_LightpackModes, 0, 0, 7, 1);
	ui->gridLayout_5->setContentsMargins(0, 0, 0, 0);
	ui->gridLayout_5->setRowStretch(0, 1);

	// Reuse the original controls and signals, but place them in a clean dashboard.
	auto *captureControlsBox = new QFrame(this);
	captureControlsBox->setObjectName(QStringLiteral("dashboardCard"));
	auto *captureCardLayout = new QVBoxLayout(captureControlsBox);
	captureCardLayout->setContentsMargins(20, 18, 20, 18);
	captureCardLayout->setSpacing(14);
	auto *captureHeading = new QLabel(QString::fromUtf8(u8"Захват экрана"), captureControlsBox);
	captureHeading->setObjectName(QStringLiteral("cardHeading"));
	captureCardLayout->addWidget(captureHeading);
	auto *captureBody = new QHBoxLayout();
	captureBody->setSpacing(24);
	auto *captureControlsLayout = new QGridLayout();
	captureControlsLayout->setHorizontalSpacing(14);
	captureControlsLayout->setVerticalSpacing(10);
	captureControlsLayout->setColumnMinimumWidth(0, 210);
	captureControlsLayout->setColumnStretch(1, 1);
	const QList<QWidget *> captureWidgets = {
		ui->label_GrabFrequency_txt, ui->label_GrabFrequency_value,
		ui->label_GrabFrequency_txt_fps, ui->label_SlowdownGrab_txt,
		ui->spinBox_GrabSlowdown, ui->label_SlowdownGrab_txt_ms,
		ui->checkBox_GrabIsAvgColors, ui->label_GrabColorMode,
		ui->comboBox_GrabColorMode
	};
	for (QWidget *widget : captureWidgets)
		ui->gridLayout->removeWidget(widget);
	ui->gridLayout_5->removeWidget(ui->comboBox_LightpackModes);
	ui->label_14->hide();
	ui->comboBox_LightpackModes->setMinimumWidth(210);
	ui->comboBox_GrabColorMode->setMinimumWidth(210);
	auto *modeLabel = new QLabel(QString::fromUtf8(u8"Режим"), captureControlsBox);
	captureControlsLayout->addWidget(modeLabel, 0, 0);
	captureControlsLayout->addWidget(ui->comboBox_LightpackModes, 0, 1, 1, 2);
	ui->label_GrabFrequency_txt->setText(QString::fromUtf8(u8"Текущая частота"));
	ui->label_GrabFrequency_txt->setMinimumWidth(210);
	captureControlsLayout->addWidget(ui->label_GrabFrequency_txt, 1, 0);
	auto *frequencyValueLayout = new QHBoxLayout();
	frequencyValueLayout->setSpacing(5);
	frequencyValueLayout->addWidget(ui->label_GrabFrequency_value);
	frequencyValueLayout->addWidget(ui->label_GrabFrequency_txt_fps);
	frequencyValueLayout->addStretch();
	captureControlsLayout->addLayout(frequencyValueLayout, 1, 1, 1, 2);

	ui->label_SlowdownGrab_txt->setText(QString::fromUtf8(u8"Интервал захвата видео"));
	ui->label_SlowdownGrab_txt->setMinimumWidth(210);
	captureControlsLayout->addWidget(ui->label_SlowdownGrab_txt, 2, 0);
	captureControlsLayout->addWidget(ui->spinBox_GrabSlowdown, 2, 1);
	captureControlsLayout->addWidget(ui->label_SlowdownGrab_txt_ms, 2, 2);

	ui->checkBox_GrabIsAvgColors->setText(QString::fromUtf8(u8"Средний цвет на все светодиоды"));
	captureControlsLayout->addWidget(ui->checkBox_GrabIsAvgColors, 3, 0, 1, 3);
	ui->label_GrabColorMode->setText(QStringLiteral("Adaptive color mode"));
	ui->label_GrabColorMode->setMinimumWidth(210);
	captureControlsLayout->addWidget(ui->label_GrabColorMode, 4, 0);
	captureControlsLayout->addWidget(ui->comboBox_GrabColorMode, 4, 1, 1, 2);
	captureBody->addLayout(captureControlsLayout, 3);
	captureBody->addWidget(new AmbilightPreview(captureControlsBox), 2);
	captureCardLayout->addLayout(captureBody);

	ui->checkBox_GrabApplyBlueLightReduction->hide();
	ui->checkBox_GrabApplyColorTemperature->hide();
	ui->horizontalSlider_GrabColorTemperature->hide();
	ui->spinBox_GrabColorTemperature->hide();
	ui->horizontalSlider_GrabGamma->hide();
	ui->doubleSpinBox_GrabGamma->hide();
	ui->pushButton_grabGammaHelp->hide();
	ui->pushButton_grabApplyColorTemperatureHelp->hide();
	ui->pushButton_grabOverBrightenHelp->hide();
	ui->label_11->hide();
	ui->horizontalSlider_GrabOverBrighten->hide();
	ui->spinBox_GrabOverBrighten->hide();
	ui->groupBox_2->hide();
	ui->gridLayout->setContentsMargins(0, 0, 0, 0);
	ui->gridLayout->setSpacing(0);
	dashboardLayout->addWidget(captureControlsBox);

	auto *sceneControlsBox = new QFrame(this);
	sceneControlsBox->setObjectName(QStringLiteral("dashboardCard"));
	auto *sceneControlsLayout = new QVBoxLayout(sceneControlsBox);
	sceneControlsLayout->setContentsMargins(20, 18, 20, 18);
	sceneControlsLayout->setSpacing(12);
	auto *sceneHeading = new QLabel(QString::fromUtf8(u8"Сцена и калибровка"), sceneControlsBox);
	sceneHeading->setObjectName(QStringLiteral("cardHeading"));
	sceneControlsLayout->addWidget(sceneHeading);
	auto *scenePresetRow = new QGridLayout();
	scenePresetRow->setHorizontalSpacing(14);
	scenePresetRow->setColumnMinimumWidth(0, 210);
	scenePresetRow->setColumnStretch(1, 1);
	m_labelScenePreset->setText(QString::fromUtf8(u8"Профиль сцены"));
	m_checkSmartCalibration->setText(QString::fromUtf8(u8"Умная цветокоррекция"));
	cameraCalibrationButton->setText(QString::fromUtf8(u8"Калибровка по камере"));
	scenePresetRow->addWidget(m_labelScenePreset, 0, 0);
	scenePresetRow->addWidget(m_comboScenePreset, 0, 1);
	sceneControlsLayout->addLayout(scenePresetRow);
	sceneControlsLayout->addWidget(m_checkSmartCalibration);
	sceneControlsLayout->addWidget(m_checkStartWithWindows);
	cameraCalibrationButton->setFixedWidth(250);
	sceneControlsLayout->addWidget(cameraCalibrationButton, 0, Qt::AlignLeft);
	dashboardLayout->addWidget(sceneControlsBox);

	// Eye comfort card, reusing the original connected controls.
	ui->gridLayout_5->removeWidget(ui->groupBox_3);
	auto *eyeCard = new QFrame(this);
	eyeCard->setObjectName(QStringLiteral("dashboardCard"));
	auto *eyeLayout = new QVBoxLayout(eyeCard);
	eyeLayout->setContentsMargins(20, 18, 20, 18);
	eyeLayout->setSpacing(12);
	auto *eyeHeading = new QLabel(QString::fromUtf8(u8"Комфорт для глаз"), eyeCard);
	eyeHeading->setObjectName(QStringLiteral("cardHeading"));
	eyeLayout->addWidget(eyeHeading);
	ui->label_6->setText(QString::fromUtf8(u8"Порог освещённости сцены"));
	eyeLayout->addWidget(ui->label_6);
	auto *thresholdRow = new QHBoxLayout();
	thresholdRow->setSpacing(12);
	ui->horizontalSlider_LuminosityThreshold->setMaximumWidth(620);
	ui->spinBox_LuminosityThreshold->setFixedWidth(64);
	ui->pushButton_lumosityThresholdHelp->setFixedSize(32, 32);
	ui->pushButton_lumosityThresholdHelp->setText(QStringLiteral("?"));
	ui->pushButton_lumosityThresholdHelp->setIcon(QIcon());
	thresholdRow->addWidget(ui->horizontalSlider_LuminosityThreshold, 1);
	thresholdRow->addWidget(ui->spinBox_LuminosityThreshold);
	thresholdRow->addWidget(ui->pushButton_lumosityThresholdHelp);
	thresholdRow->addStretch();
	eyeLayout->addLayout(thresholdRow);
	ui->label_luminosityThresholdUsedFor->hide();
	ui->radioButton_MinimumLuminosity->setText(QString::fromUtf8(u8"Минимальный уровень освещённости"));
	ui->radioButton_LuminosityDeadZone->setText(QString::fromUtf8(u8"Мёртвая зона"));
	eyeLayout->addWidget(ui->radioButton_MinimumLuminosity);
	eyeLayout->addWidget(ui->radioButton_LuminosityDeadZone);
	ui->groupBox_3->hide();
	dashboardLayout->addWidget(eyeCard);

	auto *actionPanel = new QFrame(this);
	actionPanel->setObjectName(QStringLiteral("actionPanel"));
	auto *actionLayout = new QHBoxLayout(actionPanel);
	actionLayout->setContentsMargins(18, 10, 18, 10);
	ui->pushButton_EnableDisableDevice->setMinimumSize(260, 42);
	actionLayout->addStretch();
	actionLayout->addWidget(ui->pushButton_EnableDisableDevice);
	actionLayout->addStretch();
	dashboardLayout->addWidget(actionPanel);
	dashboardLayout->addStretch();
	ui->verticalLayout_8->insertWidget(0, dashboardScroll);
	ui->verticalLayout_8->setContentsMargins(0, 0, 0, 0);
	ui->verticalLayout_8->setSpacing(0);
	connect(cameraCalibrationButton, &QPushButton::clicked, this, [this] {
		CameraCalibrationDialog dialog(this);
		dialog.exec();
	});

#ifdef Q_OS_WIN
	const QString runKeyPath = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
	const QString runValueName = QStringLiteral("ChairoLight");
	const QString legacyRunValueName = QStringLiteral("Prismatik Enhanced");
	QSettings runSettings(runKeyPath, QSettings::NativeFormat);
	const QString baseCommand = QStringLiteral("\"") + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QStringLiteral("\"");
	const QString expectedCommand = baseCommand + QStringLiteral(" --start-in-tray");
	QString currentCommand = runSettings.value(runValueName).toString();
	if (currentCommand.isEmpty()) currentCommand = runSettings.value(legacyRunValueName).toString();
	const bool wasConfigured = currentCommand == expectedCommand || currentCommand == baseCommand;
	if (currentCommand == baseCommand) {
		runSettings.setValue(runValueName, expectedCommand);
	}
	else if (wasConfigured) runSettings.setValue(runValueName, expectedCommand);
	if (wasConfigured) runSettings.remove(legacyRunValueName);
	runSettings.sync();
	m_checkStartWithWindows->setChecked(wasConfigured);
	connect(m_checkStartWithWindows, &QCheckBox::toggled, this, [runKeyPath, runValueName, expectedCommand](bool enabled) {
		QSettings settings(runKeyPath, QSettings::NativeFormat);
		if (enabled) settings.setValue(runValueName, expectedCommand);
		else settings.remove(runValueName);
		settings.sync();
	});
#else
	m_checkStartWithWindows->setVisible(false);
#endif

	// Compact monochrome navigation, inspired by modern desktop control panels.
	ui->listWidget->setViewMode(QListView::ListMode);
	ui->listWidget->setFlow(QListView::TopToBottom);
	ui->listWidget->setWrapping(false);
	ui->listWidget->setFixedWidth(190);
	ui->listWidget->setIconSize(QSize(40, 40));
	ui->listWidget->setSpacing(3);
	const QStringList navTitles = QStringList()
		<< QString::fromUtf8(u8"Главная")
		<< QString::fromUtf8(u8"Устройство")
		<< QString::fromUtf8(u8"Профили")
		<< QString::fromUtf8(u8"Зоны")
		<< QString::fromUtf8(u8"Настройки")
		<< QString::fromUtf8(u8"О программе");
	for (int i = 0; i < ui->listWidget->count(); ++i) {
		QListWidgetItem *item = ui->listWidget->item(i);
		if (i < navTitles.size())
			item->setText(navTitles.at(i));
		item->setToolTip(item->text());
		item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
		item->setSizeHint(QSize(174, 58));
		item->setIcon(modernNavigationIcon(i));
	}
	// The fourth page is the legacy plugin manager, not LED zone setup.
	// Keep it in the tab widget so page indexes and plugin internals remain intact,
	// but do not expose the misleading "Zones" navigation entry.
	if (ui->listWidget->count() > 3)
		ui->listWidget->item(3)->setHidden(true);

	// Clear, theme-native profile actions. The legacy bitmap icons were too dark
	// against the purple buttons and the reset icon looked like a down arrow.
	auto setupProfileAction = [](QPushButton *button, const QString &symbol, const QString &hint) {
		button->setIcon(QIcon());
		button->setText(symbol);
		button->setToolTip(hint);
		button->setFixedSize(38, 36);
		button->setStyleSheet(QStringLiteral("font-size:18pt;font-weight:600;color:#ffffff;padding:0px;"));
	};
	setupProfileAction(ui->pushButton_ProfileNew, QStringLiteral("+"), QString::fromUtf8(u8"Создать профиль"));
	setupProfileAction(ui->pushButton_ProfileResetToDefault, QString::fromUtf8(u8"↻"), QString::fromUtf8(u8"Сбросить профиль"));
	setupProfileAction(ui->pushButton_DeleteProfile, QString::fromUtf8(u8"×"), QString::fromUtf8(u8"Удалить профиль"));
	ui->pushButton_ExportProfile->setText(QString::fromUtf8(u8"Экспорт настроек"));
	ui->pushButton_ImportProfile->setText(QString::fromUtf8(u8"Импорт настроек"));

	ui->gridLayout_8->removeWidget(ui->listWidget);
	ui->gridLayout_8->removeWidget(ui->tabWidget);
	auto *titleBar = new ChairoTitleBar(this);
	ui->gridLayout_8->addWidget(titleBar, 0, 1, 1, 2);
	ui->gridLayout_8->addWidget(ui->listWidget, 2, 1);
	ui->gridLayout_8->addWidget(ui->tabWidget, 2, 2);
	ui->gridLayout_8->setRowStretch(2, 1);

	auto *brand = new QLabel(QStringLiteral("<span style='font-size:22pt;color:#8067ff;font-weight:700'>CL</span> <b>ChairoLight</b>"), this);
	brand->setObjectName(QStringLiteral("brandHeader"));
	brand->setMinimumHeight(54);
	brand->hide();
	auto *statusPanel = new QWidget(this);
	statusPanel->setObjectName(QStringLiteral("statusCards"));
	auto *statusLayout = new QHBoxLayout(statusPanel);
	statusLayout->setContentsMargins(0, 0, 0, 0);
	statusLayout->setSpacing(8);
	auto addCard = [statusLayout, statusPanel](const QString &title, QLabel *&value) {
		auto *card = new QFrame(statusPanel);
		card->setObjectName(QStringLiteral("statusCard"));
		card->setMinimumHeight(62);
		auto *layout = new QVBoxLayout(card);
		layout->setContentsMargins(12, 7, 12, 7);
		layout->setSpacing(1);
		auto *caption = new QLabel(title, card);
		caption->setProperty("role", QStringLiteral("caption"));
		value = new QLabel(QStringLiteral("вЂ”"), card);
		value->setText(QStringLiteral("--"));
		value->setProperty("role", QStringLiteral("value"));
		layout->addWidget(caption);
		layout->addWidget(value);
		statusLayout->addWidget(card, 1);
	};
	addCard(tr("РЈСЃС‚СЂРѕР№СЃС‚РІРѕ"), m_topDevice);
	addCard(tr("РџРѕСЂС‚"), m_topPort);
	addCard(QStringLiteral("LED"), m_topLeds);
	addCard(QStringLiteral("FPS"), m_topFps);
	addCard(tr("РЎС‚Р°С‚СѓСЃ"), m_topStatus);
	if (m_topDevice && m_topDevice->parentWidget() && m_topDevice->parentWidget()->layout()) {
		if (QLabel *caption = qobject_cast<QLabel *>(m_topDevice->parentWidget()->layout()->itemAt(0)->widget()))
			caption->setText(QString::fromUtf8(u8"Устройство"));
	}
	if (m_topPort && m_topPort->parentWidget() && m_topPort->parentWidget()->layout()) {
		if (QLabel *caption = qobject_cast<QLabel *>(m_topPort->parentWidget()->layout()->itemAt(0)->widget()))
			caption->setText(QString::fromUtf8(u8"Порт"));
	}
	if (m_topStatus && m_topStatus->parentWidget() && m_topStatus->parentWidget()->layout()) {
		if (QLabel *caption = qobject_cast<QLabel *>(m_topStatus->parentWidget()->layout()->itemAt(0)->widget()))
			caption->setText(QString::fromUtf8(u8"Статус"));
	}
	ui->gridLayout_8->addWidget(statusPanel, 1, 1, 1, 2);

	ui->tabWidget->setCurrentIndex(0);
	applyModernLightTheme();

	setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
	setFocus(Qt::OtherFocusReason);

#ifdef Q_OS_LINUX
	ui->listWidget->setSpacing(0);
	ui->listWidget->setGridSize(QSize(115, 85));
#endif

	// Check windows reserved symbols in profile input name
	#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	QRegularExpressionValidator *validatorProfileName = new QRegularExpressionValidator(QRegularExpression("[^<>:\"/\\|?*]*"), this);
	QRegularExpressionValidator *validatorApiKey = new QRegularExpressionValidator(QRegularExpression("[a-zA-Z0-9{}_-]*"), this);
	#else
	QRegExpValidator *validatorProfileName = new QRegExpValidator(QRegExp("[^<>:\"/\\|?*]*"), this);
	QRegExpValidator *validatorApiKey = new QRegExpValidator(QRegExp("[a-zA-Z0-9{}_-]*"), this);
	#endif

	ui->comboBox_Profiles->lineEdit()->setValidator(validatorProfileName);
	ui->lineEdit_ApiKey->setValidator(validatorApiKey);

	// hide main tabbar
	QTabBar* tabBar=ui->tabWidget->findChild<QTabBar*>();
	tabBar->hide();
	// hide plugin settings tabbar
	tabBar=ui->tabDevices->findChild<QTabBar*>();
	tabBar->hide();

	initPixmapCache();


	m_labelStatusIcon = new QLabel(statusBar());
	m_labelStatusIcon->setStyleSheet(QStringLiteral("QLabel{margin-right: .5em}"));
	m_labelStatusIcon->setPixmap(Settings::isBacklightEnabled() ? *(m_pixmapCache[QStringLiteral("on16")]) : *(m_pixmapCache[QStringLiteral("off16")]));
	labelProfile = new QLabel(statusBar());
	labelProfile->setStyleSheet(QStringLiteral("margin-left:1em"));
	labelDevice = new QLabel(statusBar());
	labelFPS	= new QLabel(statusBar());

	statusBar()->setStyleSheet(QStringLiteral("QStatusBar{border-top: 1px solid; border-color: palette(midlight);} QLabel{margin:0.2em}"));
	statusBar()->setSizeGripEnabled(false);
	statusBar()->addWidget(labelProfile, 4);
	statusBar()->addWidget(labelDevice, 4);
	statusBar()->addWidget(labelFPS, 4);
	statusBar()->addWidget(m_labelStatusIcon, 0);
	m_labelStatusIcon->hide();

	ui->checkBox_DisableUsbPowerLed->setVisible(false);

	updateStatusBar();

#ifdef SOUNDVIZ_SUPPORT

#ifdef BASS_SOUND_SUPPORT
	ui->label_licenseAndCredits->setText(ui->label_licenseAndCredits->text() + tr(" The sound visualizer uses the <a href=\"http://un4seen.com/\"><span style=\" text-decoration: underline; color:#0000ff;\">BASS</span></a> library."));
#endif // BASS_SOUND_SUPPORT

	if (Settings::getConnectedDevice() != SupportedDevices::DeviceType::DeviceTypeLightpack)
		ui->label_SoundvizLightpackSmoothnessNote->hide();
#else
	ui->comboBox_LightpackModes->removeItem(2);
#endif

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
	ui->checkBox_GrabApplyBlueLightReduction->setVisible(false);
	ui->checkBox_installUpdates->setVisible(false);
#endif
	if (Settings::getConnectedDevice() != SupportedDevices::DeviceType::DeviceTypeLightpack)
		ui->checkBox_PingDeviceEverySecond->hide();

	initGrabbersRadioButtonsVisibility();
	initLanguages();

	loadTranslation(Settings::getLanguage());

	if (Settings::isBacklightEnabled())
	{
		m_backlightStatus = Backlight::StatusOn;
	} else {
		m_backlightStatus = Backlight::StatusOff;
	}

	emit backlightStatusChanged(m_backlightStatus);

	m_deviceLockStatus = DeviceLocked::Unlocked;

	// Expert tab update tooltips with actual defaults
	ui->groupBox_Api->setToolTip(ui->groupBox_Api->toolTip()
		.arg(SettingsScope::Main::Api::IsEnabledDefault ? tr("ON") : tr("OFF")));

	ui->checkBox_listenOnlyOnLoInterface->setToolTip(ui->checkBox_listenOnlyOnLoInterface->toolTip()
		.arg(SettingsScope::Main::Api::ListenOnlyOnLoInterfaceDefault ? tr("ON") : tr("OFF")));

	ui->label_ApiPort->setToolTip(ui->label_ApiPort->toolTip().arg(SettingsScope::Main::Api::PortDefault));
	ui->lineEdit_ApiPort->setToolTip(ui->label_ApiPort->toolTip());

	ui->lineEdit_ApiKey->setToolTip(ui->lineEdit_ApiKey->toolTip()
		.arg(ui->lineEdit_ApiKey->maxLength())
		.arg(SettingsScope::Main::Api::AuthKey.isEmpty() ? tr("none") : SettingsScope::Main::Api::AuthKey));
	ui->label_ApiKey->setToolTip(ui->lineEdit_ApiKey->toolTip());

	ui->spinBox_LoggingLevel->setToolTip(ui->spinBox_LoggingLevel->toolTip()
		.arg(Debug::DebugLevels::ZeroLevel)
		.arg(Debug::DebugLevels::HighLevel)
		.arg(SettingsScope::Main::DebugLevelDefault));
	ui->label_LoggingLevel->setToolTip(ui->spinBox_LoggingLevel->toolTip());

	ui->checkBox_SendDataOnlyIfColorsChanges->setToolTip(ui->checkBox_SendDataOnlyIfColorsChanges->toolTip()
		.arg(SettingsScope::Profile::Grab::IsSendDataOnlyIfColorsChangesDefault ? tr("ON") : tr("OFF")));

	// /Expert tab tooltips update

	resize(1080, 900);

	DEBUG_LOW_LEVEL << Q_FUNC_INFO << "initialized";
}

void SettingsWindow::applyModernLightTheme()
{
	setStyleSheet(QStringLiteral(R"(
QMainWindow {
	background-color: #111113;
	border: 1px solid #303039;
}
QWidget#chairoTitleBar {
	background-color: #15151a;
	border: 1px solid #292932;
	border-radius: 10px;
}
QLabel#titleMark {
	color: #8d75ff;
	font-size: 14pt;
	font-weight: 700;
}
QLabel#titleText {
	color: #f4f3f8;
	font-size: 10pt;
	font-weight: 600;
}
QLabel#sectionHeading {
	color: #c9c3ff;
	font-size: 10pt;
	font-weight: 600;
	padding: 0px 0px 2px 2px;
}
QPushButton#titleCloseButton {
	background: transparent;
	border: none;
	border-radius: 7px;
	padding: 0px;
	min-height: 0px;
	color: #bdbdc8;
	font-size: 18pt;
	font-weight: 400;
}
QPushButton#titleCloseButton:hover {
	background-color: #d84b63;
	color: #ffffff;
}
QPushButton#titleCloseButton:pressed {
	background-color: #b93950;
}
QWidget {
	background-color: #17171a;
	color: #f1f1f4;
	font-size: 10pt;
	font-family: "Segoe UI Variable", "Segoe UI";
}
QLabel#brandHeader {
	background: transparent;
	padding-left: 8px;
}
QWidget#statusCards {
	background: transparent;
}
QFrame#statusCard {
	background: #1d1d21;
	border: 1px solid #303039;
	border-radius: 10px;
}
QFrame#dashboardCard {
	background-color: #1a1a22;
	border: 1px solid #2a2a35;
	border-radius: 12px;
}
QFrame#actionPanel {
	background-color: #17171e;
	border: 1px solid #292933;
	border-radius: 12px;
}
QLabel#cardHeading {
	color: #f4f3f8;
	font-size: 12pt;
	font-weight: 650;
}
QLabel#aboutDescription {
	background-color: #1a1a22;
	border: 1px solid #2a2a35;
	border-radius: 12px;
	color: #ededf3;
	padding: 28px;
}
QWidget#dashboardHost, QScrollArea#dashboardScroll,
QScrollArea#dashboardScroll > QWidget > QWidget {
	background-color: #111116;
}
QWidget#ambilightPreview {
	background: transparent;
}
QFrame#statusCard QLabel[role="caption"] {
	color: #8f8f9a;
	font-size: 9pt;
}
QFrame#statusCard QLabel[role="value"] {
	color: #f5f5f8;
	font-size: 11pt;
	font-weight: 600;
}
QTabWidget::pane, QStackedWidget, QScrollArea, QScrollArea > QWidget > QWidget {
	background: transparent;
	border: none;
}
QGroupBox {
	background-color: #1d1d21;
	border: 1px solid #2d2d34;
	border-radius: 12px;
	margin-top: 14px;
	padding: 12px 10px 10px 10px;
	font-weight: 600;
}
QGroupBox::title {
	subcontrol-origin: margin;
	left: 12px;
	padding: 0 6px;
	color: #c9c3ff;
	background-color: #1d1d21;
}
QListWidget {
	background: #141416;
	border: 1px solid #29292f;
	border-radius: 11px;
	outline: none;
	padding: 6px;
}
QListWidget::item {
	background: transparent;
	border: 1px solid transparent;
	border-radius: 9px;
	padding: 5px;
	margin: 2px 4px;
}
QListWidget::item:selected {
	background-color: #24232c;
	color: #ffffff;
	border: 1px solid #4c426f;
}
QListWidget::item:hover {
	background-color: #202024;
}
QPushButton, QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
	background-color: #222226;
	border: 1px solid #393940;
	border-radius: 8px;
	color: #f5f5f8;
	selection-background-color: #765cff;
	selection-color: #f5fbff;
}
QPushButton {
	background-color: #765cff;
	border-color: #8873ff;
	padding: 3px 10px;
	min-height: 26px;
	color: #dff2ff;
	font-weight: 600;
}
QComboBox, QSpinBox, QDoubleSpinBox {
	padding: 2px 8px;
	min-height: 26px;
}
QLineEdit {
	padding: 3px 8px;
	min-height: 24px;
}
QPushButton:hover {
	background-color: #856dff;
	border-color: #a496ff;
}
QPushButton:pressed {
	background-color: #6448e8;
}
QCommandLinkButton {
	background-color: #222226;
	border: 1px solid #3a3943;
	border-radius: 12px;
	padding: 5px 12px;
	min-height: 30px;
	color: #d7d2ff;
}
QCommandLinkButton:hover {
	background-color: #2a2931;
	border-color: #765cff;
}
QComboBox::drop-down {
	subcontrol-origin: padding;
	subcontrol-position: top right;
	border: none;
	border-left: 1px solid #393940;
	width: 30px;
}
QComboBox::down-arrow {
	image: url(:/icons/arrow_down_gray.png);
	width: 14px;
	height: 14px;
}
QComboBox:hover::drop-down {
	background-color: #2c2b32;
}
QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QLineEdit:disabled {
	color: #8f8f99;
	background-color: #1c1c20;
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
	subcontrol-origin: border;
	subcontrol-position: top right;
	width: 20px;
	border-left: 1px solid #393940;
	border-bottom: 1px solid #303038;
	border-top-right-radius: 7px;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
	subcontrol-origin: border;
	subcontrol-position: bottom right;
	width: 20px;
	border-left: 1px solid #393940;
	border-bottom-right-radius: 7px;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
	image: url(:/icons/arrow_up_gray.png);
	width: 10px;
	height: 10px;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
	image: url(:/icons/arrow_down_gray.png);
	width: 10px;
	height: 10px;
}
QComboBox QAbstractItemView {
	background-color: #202024;
	border: 1px solid #3a3943;
	selection-background-color: #765cff;
	selection-color: #eff7ff;
}
QCheckBox, QRadioButton {
	spacing: 8px;
	background: transparent;
}
QCheckBox::indicator, QRadioButton::indicator {
	width: 16px;
	height: 16px;
	border: 1px solid #555560;
	background: #202024;
	border-radius: 4px;
}
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
	background: #765cff;
	border-color: #a294ff;
}
QSlider::groove:horizontal {
	height: 6px;
	background: #34343b;
	border-radius: 3px;
}
QSlider::handle:horizontal {
	background: #8067ff;
	border: 1px solid #b0a4ff;
	width: 16px;
	margin: -6px 0;
	border-radius: 8px;
}
QStatusBar {
	background-color: #121214;
	border-top: 1px solid #29292f;
}
QStatusBar::item {
	border: none;
}
QStatusBar QLabel {
	background: transparent;
	color: #94949f;
	border-right: 1px solid #292932;
	padding: 2px 12px;
}
QLabel {
	background: transparent;
}
QScrollBar:vertical {
	background: #111116;
	width: 12px;
	border-radius: 6px;
	margin: 2px;
}
QScrollBar::handle:vertical {
	background: #3b3a43;
	border-radius: 6px;
	min-height: 24px;
}
QScrollBar::handle:vertical:hover {
	background: #655a98;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
	background: none;
	border: none;
}
QToolTip {
	background-color: #24242a;
	color: #ffffff;
	border: 1px solid #62568c;
	padding: 6px;
}
)"));
}

void SettingsWindow::changePage(int page)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << page;

	ui->tabWidget->setCurrentIndex(page);
	if (page == 5) {
		ui->textBrowser->verticalScrollBar()->setValue(0);
		using namespace std::chrono_literals;
		m_smoothScrollTimer.setInterval(100ms);
		m_smoothScrollTimer.start();
	} else {
		m_smoothScrollTimer.stop();
	}

}

SettingsWindow::~SettingsWindow()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;
	if (m_trayIcon)
		delete m_trayIcon;

	//delete ui;
}

void SettingsWindow::connectSignalsSlots()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

//	if (m_trayIcon!=NULL)
//	{
//		connect(m_trayIcon, &SysTrayIcon::activated, this, &SettingsWindow::onTrayIcon_Activated);
//		connect(m_trayIcon, &SysTrayIcon::messageClicked, this, &SettingsWindow::onTrayIcon_MessageClicked);
//	}

	connect(ui->listWidget, &QListWidget::currentRowChanged, this, &SettingsWindow::changePage);
	connect(ui->spinBox_GrabSlowdown, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsWindow::onGrabSlowdown_valueChanged);
	connect(ui->spinBox_LuminosityThreshold, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsWindow::onLuminosityThreshold_valueChanged);
	connect(ui->radioButton_MinimumLuminosity, &QRadioButton::toggled, this, &SettingsWindow::onMinimumLumosity_toggled);
	connect(ui->radioButton_LuminosityDeadZone, &QRadioButton::toggled, this, &SettingsWindow::onMinimumLumosity_toggled);
	connect(ui->checkBox_GrabIsAvgColors, &QCheckBox::toggled, this, &SettingsWindow::onGrabIsAvgColors_toggled);
	connect(ui->comboBox_GrabColorMode, qOverload<int>(&QComboBox::currentIndexChanged), this, &SettingsWindow::onGrabColorMode_currentIndexChanged);
	connect(m_comboScenePreset, qOverload<int>(&QComboBox::currentIndexChanged), this, &SettingsWindow::onGrabScenePreset_currentIndexChanged);
	connect(m_checkSmartCalibration, &QCheckBox::toggled, this, &SettingsWindow::onGrabSmartCalibration_toggled);
	connect(ui->spinBox_GrabOverBrighten, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsWindow::onGrabOverBrighten_valueChanged);
	connect(ui->checkBox_GrabApplyBlueLightReduction, &QCheckBox::toggled, this, &SettingsWindow::onGrabApplyBlueLightReduction_toggled);
	connect(ui->checkBox_GrabApplyColorTemperature, &QCheckBox::toggled, this, &SettingsWindow::onGrabApplyColorTemperature_toggled);
	connect(ui->horizontalSlider_GrabColorTemperature, &QSlider::valueChanged, this, &SettingsWindow::onGrabColorTemperature_valueChanged);
	connect(ui->horizontalSlider_GrabGamma, &QSlider::valueChanged, this, &SettingsWindow::onSliderGrabGamma_valueChanged);
	connect(ui->doubleSpinBox_GrabGamma, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &SettingsWindow::onGrabGamma_valueChanged);

	connect(ui->radioButton_GrabWidgetsDontShow, &QRadioButton::toggled, this, &SettingsWindow:: onDontShowLedWidgets_Toggled);
	connect(ui->radioButton_Colored, &QRadioButton::toggled, this, &SettingsWindow::onSetColoredLedWidgets);
	connect(ui->radioButton_White, &QRadioButton::toggled, this, &SettingsWindow::onSetWhiteLedWidgets);

	connect(ui->radioButton_LiquidColorMoodLampMode, &QRadioButton::toggled, this, &SettingsWindow::onMoodLampLiquidMode_Toggled);
	connect(ui->horizontalSlider_MoodLampSpeed, &QSlider::valueChanged, this, &SettingsWindow::onMoodLampSpeed_valueChanged);
	connect(ui->comboBox_MoodLampLamp, qOverload<int>(&QComboBox::currentIndexChanged), this, &SettingsWindow::onMoodLampLamp_currentIndexChanged);

	// Main options
	connect(ui->comboBox_LightpackModes, qOverload<int>(&QComboBox::currentIndexChanged), this, &SettingsWindow::onLightpackModes_currentIndexChanged);
	connect(ui->comboBox_Language, &QComboBox::currentTextChanged, this, &SettingsWindow::loadTranslation);
	connect(ui->pushButton_EnableDisableDevice, &QPushButton::clicked, this, &SettingsWindow::toggleBacklight);

	// Device options
	connect(ui->spinBox_DeviceRefreshDelay, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsWindow::onDeviceRefreshDelay_valueChanged);
	connect(ui->checkBox_DisableUsbPowerLed, &QCheckBox::toggled, this, &SettingsWindow::onDisableUsbPowerLed_toggled);
	connect(ui->spinBox_DeviceSmooth, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsWindow::onDeviceSmooth_valueChanged);
	connect(ui->spinBox_DeviceBrightness, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsWindow::onDeviceBrightness_valueChanged);
	connect(ui->spinBox_DeviceBrightnessCap, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsWindow::onDeviceBrightnessCap_valueChanged);
	connect(ui->spinBox_DeviceColorDepth, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsWindow::onDeviceColorDepth_valueChanged);
	connect(ui->doubleSpinBox_DeviceGamma, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &SettingsWindow::onDeviceGammaCorrection_valueChanged);
	connect(ui->checkBox_EnableDithering, &QCheckBox::toggled, this, &SettingsWindow::onDeviceDitheringEnabled_toggled);
	connect(ui->horizontalSlider_GammaCorrection, &QSlider::valueChanged, this, &SettingsWindow::onSliderDeviceGammaCorrection_valueChanged);
	connect(ui->checkBox_SendDataOnlyIfColorsChanges, &QCheckBox::toggled, this, &SettingsWindow::onDeviceSendDataOnlyIfColorsChanged_toggled);

	connect(ui->pbRunConfigurationWizard, &QPushButton::clicked, this, &SettingsWindow::onRunConfigurationWizard_clicked);

	// Open Settings file
	connect(ui->commandLinkButton_OpenSettings, &QCommandLinkButton::clicked, this, &SettingsWindow::openCurrentProfile);
	connect(ui->pushButton_ExportProfile, &QPushButton::clicked, this, &SettingsWindow::exportCurrentProfile);
	connect(ui->pushButton_ImportProfile, &QPushButton::clicked, this, &SettingsWindow::importProfile);

	// Connect profile signals to this slots
	connect(ui->comboBox_Profiles->lineEdit(), &QLineEdit::editingFinished /* or returnPressed() */, this, &SettingsWindow::profileRename);
	connect(ui->comboBox_Profiles, &QComboBox::currentTextChanged, this, &SettingsWindow::profileSwitch);

	connect(Settings::settingsSingleton(), &Settings::currentProfileInited, this, &SettingsWindow::handleProfileLoaded);

	// connect(Settings::settingsSingleton(), SIGNAL(hotkeyChanged(QString,QKeySequence,QKeySequence)), this, &SettingsWindow::onHotkeyChanged);
	connect(Settings::settingsSingleton(), &Settings::lightpackModeChanged, this, &SettingsWindow::onLightpackModeChanged);

	connect(ui->pushButton_ProfileNew, &QPushButton::clicked, this, &SettingsWindow::profileNew);
	connect(ui->pushButton_ProfileResetToDefault, &QPushButton::clicked, this, &SettingsWindow::profileResetToDefaultCurrent);
	connect(ui->pushButton_DeleteProfile, &QPushButton::clicked, this, &SettingsWindow::profileDeleteCurrent);

	connect(ui->pushButton_SelectColorMoodLamp, &ColorButton::colorChanged, this, &SettingsWindow::onMoodLampColor_changed);
#ifdef SOUNDVIZ_SUPPORT
	connect(ui->comboBox_SoundVizDevice, qOverload<int>(&QComboBox::currentIndexChanged), this, &SettingsWindow::onSoundVizDevice_currentIndexChanged);
	connect(ui->comboBox_SoundVizVisualizer, qOverload<int>(&QComboBox::currentIndexChanged), this, &SettingsWindow::onSoundVizVisualizer_currentIndexChanged);
	connect(ui->pushButton_SelectColorSoundVizMin, &ColorButton::colorChanged, this, &SettingsWindow::onSoundVizMinColor_changed);
	connect(ui->pushButton_SelectColorSoundVizMax, &ColorButton::colorChanged, this, &SettingsWindow::onSoundVizMaxColor_changed);
	connect(ui->radioButton_SoundVizLiquidMode, &QRadioButton::toggled, this, &SettingsWindow::onSoundVizLiquidMode_Toggled);
	connect(ui->horizontalSlider_SoundVizLiquidSpeed, &QSlider::valueChanged, this, &SettingsWindow::onSoundVizLiquidSpeed_valueChanged);
#ifdef Q_OS_MACOS
	connect(ui->pushButton_SoundVizDeviceHelp, &QPushButton::clicked, this, &SettingsWindow::onSoundVizDeviceHelp_clicked);
#else
	ui->pushButton_SoundVizDeviceHelp->hide();
#endif // Q_OS_MACOS

#endif// SOUNDVIZ_SUPPORT
	connect(ui->checkBox_KeepLightsOnAfterExit, &QCheckBox::toggled, this, &SettingsWindow::onKeepLightsAfterExit_Toggled);
	connect(ui->checkBox_KeepLightsOnAfterLockComputer, &QCheckBox::toggled, this, &SettingsWindow::onKeepLightsAfterLock_Toggled);
	connect(ui->checkBox_KeepLightsOnAfterSuspend, &QCheckBox::toggled, this, &SettingsWindow::onKeepLightsAfterSuspend_Toggled);
	connect(ui->checkBox_KeepLightsOnAfterScreenOff, &QCheckBox::toggled, this, &SettingsWindow::onKeepLightsAfterScreenOff_Toggled);

	// Dev tab
#ifdef WGC_GRAB_SUPPORT
	connect(ui->radioButton_GrabWgc, &QRadioButton::toggled, this, &SettingsWindow::onGrabberChanged);
#endif
#ifdef WINAPI_GRAB_SUPPORT
	connect(ui->radioButton_GrabWinAPI, &QRadioButton::toggled, this, &SettingsWindow::onGrabberChanged);
#endif
#ifdef DDUPL_GRAB_SUPPORT
	connect(ui->radioButton_GrabDDupl, &QRadioButton::toggled, this, &SettingsWindow::onGrabberChanged);
#endif
#ifdef X11_GRAB_SUPPORT
	connect(ui->radioButton_GrabX11, &QRadioButton::toggled, this, &SettingsWindow::onGrabberChanged);
#endif
#ifdef MAC_OS_AV_GRAB_SUPPORT
	connect(ui->radioButton_GrabMacAVFoundation, &QRadioButton::toggled, this, &SettingsWindow::onGrabberChanged);
#endif
#ifdef MAC_OS_CG_GRAB_SUPPORT
	connect(ui->radioButton_GrabMacCoreGraphics, &QRadioButton::toggled, this, &SettingsWindow::onGrabberChanged);
#endif
#ifdef D3D10_GRAB_SUPPORT
	connect(ui->checkBox_EnableDx1011Capture, &QCheckBox::toggled, this, &SettingsWindow::onDx1011CaptureEnabledChanged);
	connect(ui->checkBox_EnableDx9Capture, &QCheckBox::toggled, this, &SettingsWindow::onDx9CaptureEnabledChanged);
#endif


	// Dev tab configure API (port, apikey)
	connect(ui->groupBox_Api, &QGroupBox::toggled, this, &SettingsWindow::onEnableApi_Toggled);
	connect(ui->checkBox_listenOnlyOnLoInterface, &QCheckBox::toggled, this, &SettingsWindow::onListenOnlyOnLoInterface_Toggled);
	connect(ui->lineEdit_ApiPort, &QLineEdit::editingFinished, this, &SettingsWindow::onSetApiPort_Clicked);
	//connect(ui->checkBox_IsApiAuthEnabled, &QCheckBox::toggled, this, &SettingsWindow::onIsApiAuthEnabled_Toggled);
	connect(ui->pushButton_GenerateNewApiKey, &QPushButton::clicked, this, &SettingsWindow::onGenerateNewApiKey_Clicked);
	connect(ui->lineEdit_ApiKey, &QLineEdit::editingFinished, this, &SettingsWindow::onApiKey_EditingFinished);

	connect(ui->spinBox_LoggingLevel, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsWindow::onLoggingLevel_valueChanged);
	connect(ui->toolButton_OpenLogs, &QToolButton::clicked, this, &SettingsWindow::onOpenLogs_clicked);
	connect(ui->checkBox_PingDeviceEverySecond, &QCheckBox::toggled, this, &SettingsWindow::onPingDeviceEverySecond_Toggled);

	//Plugins
	//	connected during setupUi by name:
	//	connect(ui->list_Plugins,SIGNAL(currentRowChanged(int)),this,SLOT(on_list_Plugins_itemClicked(QListWidgetItem *)));
	connect(ui->pushButton_UpPriority, &QPushButton::clicked, this, &SettingsWindow::MoveUpPlugin);
	connect(ui->pushButton_DownPriority, &QPushButton::clicked, this, &SettingsWindow::MoveDownPlugin);

	// About page
	connect(&m_smoothScrollTimer, &QTimer::timeout, this, &SettingsWindow::scrollThanks);
	connect(ui->checkBox_checkForUpdates, &QCheckBox::toggled, this, &SettingsWindow::onCheckBox_checkForUpdates_Toggled);
	connect(ui->checkBox_installUpdates, &QCheckBox::toggled, this, &SettingsWindow::onCheckBox_installUpdates_Toggled);
	connect(&m_baudrateWarningClearTimer, &QTimer::timeout, this, &SettingsWindow::clearBaudrateWarning);
}

// ----------------------------------------------------------------------------
// Events
// ----------------------------------------------------------------------------

void SettingsWindow::changeEvent(QEvent *e)
{
	DEBUG_MID_LEVEL << Q_FUNC_INFO << e->type();

	int currentPage = 0;
	QListWidgetItem * item = NULL;
	QMainWindow::changeEvent(e);
	switch (e->type()) {
	case QEvent::LanguageChange: {

		currentPage = ui->listWidget->currentRow();
		updatingFromSettings = true;
		ui->retranslateUi(this);
		updatingFromSettings = false;
		ui->label_3->setText(QString::fromUtf8(u8"<div style='font-size:12pt;line-height:150%'><b>ChairoLight</b> — приложение для адаптивной фоновой подсветки экрана.<br/><br/>Сделано Oidzuri на основе открытого проекта <b>Prismatik</b>.</div>"));
		const QStringList navTitles = QStringList()
			<< QString::fromUtf8(u8"Главная")
			<< QString::fromUtf8(u8"Устройство")
			<< QString::fromUtf8(u8"Профили")
			<< QString::fromUtf8(u8"Зоны")
			<< QString::fromUtf8(u8"Настройки")
			<< QString::fromUtf8(u8"О программе");
		for (int i = 0; i < ui->listWidget->count(); ++i) {
			QListWidgetItem *navItem = ui->listWidget->item(i);
			if (i < navTitles.size())
				navItem->setText(navTitles.at(i));
			navItem->setToolTip(navItem->text());
			navItem->setSizeHint(QSize(174, 58));
			navItem->setIcon(modernNavigationIcon(i));
		}
		if (ui->listWidget->count() > 3)
			ui->listWidget->item(3)->setHidden(true);
		if (m_trayIcon)
			m_trayIcon->retranslateUi();

		setWindowTitle(tr("ChairoLight: %1").arg(ui->comboBox_Profiles->lineEdit()->text()));

		ui->listWidget->addItem(QStringLiteral("dirty hack"));
		item = ui->listWidget->takeItem(ui->listWidget->count()-1);
		delete item;
		ui->listWidget->setCurrentRow(currentPage);


		ui->comboBox_Language->setItemText(0, tr("System default"));

		updateStatusBar();

		break;
	}
	default:
		break;
	}
}

void SettingsWindow::closeEvent(QCloseEvent *event)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	if (m_trayIcon && m_trayIcon->isVisible()) {
		// Just hide settings
		hideSettings();
		event->ignore();
	}
	else {
		// terminate application if we're running in "trayless" mode
		QApplication::quit();
	}
}

void SettingsWindow::onFocus()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;
	if (!ui->radioButton_GrabWidgetsDontShow->isChecked() && ui->comboBox_LightpackModes->currentIndex() == GrabModeIndex) {
		emit showLedWidgets(true);
	}
}

void SettingsWindow::onBlur()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;
	emit showLedWidgets(false);
}

void SettingsWindow::updateStatusBar() {
	DEBUG_MID_LEVEL << Q_FUNC_INFO;

	this->labelProfile->setText(tr("Profile: %1").arg(Settings::getCurrentProfileName()));
	this->labelDevice->setText(tr("Device: %1").arg(Settings::getConnectedDeviceName()));
	this->labelFPS->setText(tr("FPS: %1").arg(QLatin1String("")));
	if (m_topDevice) m_topDevice->setText(Settings::getConnectedDeviceName());
	if (m_topPort) m_topPort->setText(Settings::getAdalightSerialPortName());
	if (m_topLeds) m_topLeds->setText(QString::number(Settings::getNumberOfLeds(Settings::getConnectedDevice())));
	if (m_topFps) m_topFps->setText(QStringLiteral("вЂ”"));
	if (m_topStatus) {
		m_topStatus->setText(m_backlightStatus == Backlight::StatusDeviceError
			? QString::fromUtf8(u8"Ошибка")
			: QString::fromUtf8(u8"Подключено"));
		m_topStatus->setStyleSheet(m_backlightStatus == Backlight::StatusDeviceError ? QStringLiteral("color:#ff657a") : QStringLiteral("color:#58d878"));
	}
}

void SettingsWindow::updateDeviceTabWidgetsVisibility()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	SupportedDevices::DeviceType connectedDevice = Settings::getConnectedDevice();

	switch (connectedDevice)
	{
	case SupportedDevices::DeviceTypeVirtual:
		ui->tabDevices->show();
		ui->tabDevices->setCurrentWidget(ui->tabDeviceVirtual);
		// Sync Virtual Leds count with NumberOfLeds field
		initVirtualLeds(Settings::getNumberOfLeds(SupportedDevices::DeviceTypeVirtual));
		break;

	case SupportedDevices::DeviceTypeLightpack:
		ui->tabDevices->show();
		ui->tabDevices->setCurrentWidget(ui->tabDeviceLightpack);
		// Sync Virtual Leds count with NumberOfLeds field
		break;

	default:
		ui->tabDevices->hide();
//		qCritical() << Q_FUNC_INFO << "Fail. Unknown connectedDevice ==" << connectedDevice;
		break;
	}
	setDeviceTabWidgetsVisibility(DeviceTab::Lightpack);

}

void SettingsWindow::setDeviceTabWidgetsVisibility(DeviceTab::Options options)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << options;

#ifdef QT_NO_DEBUG
	int majorVersion = getLigtpackFirmwareVersionMajor();
	bool isShowOldSettings = (majorVersion == 4 || majorVersion == 5);

	// Show color depth only if lightpack hw4.x or hw5.x
	ui->label_DeviceColorDepth->setVisible(isShowOldSettings);
	ui->horizontalSlider_DeviceColorDepth->setVisible(isShowOldSettings);
	ui->spinBox_DeviceColorDepth->setVisible(isShowOldSettings);
	ui->pushButton_LightpackColorDepthHelp->setVisible(isShowOldSettings);

	ui->label_DeviceRefreshDelay->setVisible(isShowOldSettings);
	ui->horizontalSlider_DeviceRefreshDelay->setVisible(isShowOldSettings);
	ui->spinBox_DeviceRefreshDelay->setVisible(isShowOldSettings);
	ui->pushButton_LightpackRefreshDelayHelp->setVisible(isShowOldSettings);
#endif
}

void SettingsWindow::syncLedDeviceWithSettingsWindow()
{
	emit updateBrightness(Settings::getDeviceBrightness());
	emit updateBrightnessCap(Settings::getDeviceBrightnessCap());
	emit updateGamma(Settings::getDeviceGamma());
}

int SettingsWindow::getLigtpackFirmwareVersionMajor()
{
	DEBUG_MID_LEVEL << Q_FUNC_INFO;

	if (Settings::getConnectedDevice() != SupportedDevices::DeviceTypeLightpack)
		return -1;

	if (m_deviceFirmwareVersion == DeviceFirmvareVersionUndef)
		return -1;

	bool ok = false;
	double version = m_deviceFirmwareVersion.toDouble(&ok);

	if (!ok)
	{
		DEBUG_MID_LEVEL << Q_FUNC_INFO << "Convert to double fail. Device firmware version =" << m_deviceFirmwareVersion;
		return -1;
	}

	int majorVersion = (int)version;

	DEBUG_LOW_LEVEL << Q_FUNC_INFO << "Prismatik major version:" << majorVersion;

	return majorVersion;
}

void SettingsWindow::onPostInit() {
	updateUiFromSettings();
	emit requestFirmwareVersion();
	emit requestMoodLampLamps();
#ifdef SOUNDVIZ_SUPPORT
	emit requestSoundVizDevices();
	emit requestSoundVizVisualizers();
#endif

	if (m_trayIcon) {
		bool updateJustFailed = false;
		if (!Settings::getAutoUpdatingVersion().isEmpty()) {
			if (Settings::getAutoUpdatingVersion() != QStringLiteral(VERSION_STR)) {
				m_trayIcon->showMessage(tr("Prismatik was updated"), tr("Successfully updated to version %1.").arg(QStringLiteral(VERSION_STR)));
			} else {
				QMessageBox::critical(
					this,
					tr("Prismatik automatic update failed"),
					tr("There was a problem when trying to automatically update Prismatik to the latest version.\n")
					+ tr("You are still on version %1.\n").arg(QStringLiteral(VERSION_STR))
					+ tr("Installing updates automatically was disabled."));
				updateJustFailed = true;
				ui->checkBox_installUpdates->setChecked(false);
			}
			Settings::setAutoUpdatingVersion(QLatin1String(""));
		}

		if (Settings::isCheckForUpdatesEnabled() && !updateJustFailed) {
			using namespace std::chrono_literals;
			QTimer::singleShot(10s, m_trayIcon, &SysTrayIcon::checkUpdate);
		}
	}
}

void SettingsWindow::onEnableApi_Toggled(bool isEnabled)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << isEnabled;

	Settings::setIsApiEnabled(isEnabled);

}

void SettingsWindow::onListenOnlyOnLoInterface_Toggled(bool localOnly)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << localOnly;
	Settings::setListenOnlyOnLoInterface(localOnly);
}

void SettingsWindow::onApiKey_EditingFinished()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	QString apikey = ui->lineEdit_ApiKey->text();

	Settings::setApiKey(apikey);
}

void SettingsWindow::onSetApiPort_Clicked()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << ui->lineEdit_ApiPort->text();

	bool ok;
	int port = ui->lineEdit_ApiPort->text().toInt(&ok);

	if (ok)
	{
		Settings::setApiPort(port);
		emit updateApiPort(port);

		ui->lineEdit_ApiPort->setStyleSheet(this->styleSheet());
		ui->lineEdit_ApiPort->setToolTip(QLatin1String(""));
	} else {
		QString errorMessage = QStringLiteral("Convert to 'int' fail");

		ui->lineEdit_ApiPort->setStyleSheet(QStringLiteral("background-color:red;"));
		ui->lineEdit_ApiPort->setToolTip(errorMessage);

		qWarning() << Q_FUNC_INFO << errorMessage << "port:" << ui->lineEdit_ApiPort->text();
	}
}

void SettingsWindow::onGenerateNewApiKey_Clicked()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	QString generatedApiKey = QUuid::createUuid().toString();

	ui->lineEdit_ApiKey->setText(generatedApiKey);

	Settings::setApiKey(generatedApiKey);
}

void SettingsWindow::onLoggingLevel_valueChanged(int value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;

	// WARNING: Multithreading bug here with g_debugLevel
	g_debugLevel = value;

	Settings::setDebugLevel(value);

	if (LogWriter::getLogsDir().exists()) {
		ui->toolButton_OpenLogs->setEnabled(true);
		ui->toolButton_OpenLogs->setToolTip(ui->toolButton_OpenLogs->whatsThis());
	} else {
		ui->toolButton_OpenLogs->setEnabled(false);

		if (value != Debug::DebugLevels::ZeroLevel)
			ui->toolButton_OpenLogs->setToolTip(ui->toolButton_OpenLogs->whatsThis() + tr(" (restart the program first)"));
		else
			ui->toolButton_OpenLogs->setToolTip(ui->toolButton_OpenLogs->whatsThis() + tr(" (enable logs first and restart the program)"));
	}
}

void SettingsWindow::onOpenLogs_clicked()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	QDesktopServices::openUrl(QUrl(LogWriter::getLogsDir().absolutePath()));
}

void SettingsWindow::setDeviceLockViaAPI(const DeviceLocked::DeviceLockStatus status,	const QList<QString>& modules)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << status;
	m_deviceLockStatus = status;
	m_deviceLockKey = modules;


	if (m_deviceLockStatus == DeviceLocked::Unlocked)
	{
		syncLedDeviceWithSettingsWindow();
	}

	startBacklight();
}

void SettingsWindow::setModeChanged(Lightpack::Mode mode)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << mode;
	updateUiFromSettings();
}

void SettingsWindow::setBacklightStatus(Backlight::Status status)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	if (m_backlightStatus != Backlight::StatusDeviceError
			|| status == Backlight::StatusOff)
	{
		m_backlightStatus = status;
		emit backlightStatusChanged(m_backlightStatus);
	}

	startBacklight();
}

void SettingsWindow::backlightOn()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	m_backlightStatus = Backlight::StatusOn;
	emit backlightStatusChanged(m_backlightStatus);
	startBacklight();
}

void SettingsWindow::backlightOff()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	m_backlightStatus = Backlight::StatusOff;
	emit backlightStatusChanged(m_backlightStatus);
	startBacklight();
}

void SettingsWindow::toggleBacklight()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	switch (m_backlightStatus)
	{
	case Backlight::StatusOn:
		m_backlightStatus = Backlight::StatusOff;
		break;
	case Backlight::StatusOff:
		m_backlightStatus = Backlight::StatusOn;
		break;
	case Backlight::StatusDeviceError:
		m_backlightStatus = Backlight::StatusOff;
		break;
	default:
		qWarning() << Q_FUNC_INFO << "m_backlightStatus contains crap =" << m_backlightStatus;
		break;
	}

	emit backlightStatusChanged(m_backlightStatus);

	startBacklight();
}

void SettingsWindow::startBacklight()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << "m_backlightStatus =" << m_backlightStatus
					<< "m_deviceLockStatus =" << m_deviceLockStatus;

	if(ui->list_Plugins->count()>0)
		{
			int count = ui->list_Plugins->count();
			for(int index = 0; index < count; index++)
			{
				DEBUG_LOW_LEVEL << Q_FUNC_INFO << "check session key";
				QListWidgetItem * item = ui->list_Plugins->item(index);
				int indexPlugin =item->data(Qt::UserRole).toUInt();
				QString key = _plugins[indexPlugin]->Guid();
				if (m_deviceLockKey.contains(key))
				{
					if (m_deviceLockStatus != DeviceLocked::Api	&& m_deviceLockKey.indexOf(key)==0)
						m_deviceLockModule = _plugins[indexPlugin]->Name();
					item->setText(getPluginName(_plugins[indexPlugin])+" (Lock)");
				}
				else
					item->setText(getPluginName(_plugins[indexPlugin]));
			}
		}


	if (m_deviceLockKey.count()==0)
		m_deviceLockModule = QLatin1String("");

	updateTrayAndActionStates();
}

void SettingsWindow::nextProfile()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;
	QStringList profiles = Settings::findAllProfiles();
	QString profile = Settings::getCurrentProfileName();

	int curIndex = profiles.indexOf(profile);
	int newIndex = (curIndex == profiles.count() - 1) ? 0 : curIndex + 1;

	Settings::loadOrCreateProfile(profiles[newIndex]);
}

void SettingsWindow::prevProfile()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;
	QStringList profiles = Settings::findAllProfiles();
	QString profile = Settings::getCurrentProfileName();

	int curIndex = profiles.indexOf(profile);
	int newIndex = (curIndex == 0) ? profiles.count() - 1 : curIndex - 1;

	Settings::loadOrCreateProfile(profiles[newIndex]);
}


void SettingsWindow::updateTrayAndActionStates()
{
	DEBUG_MID_LEVEL << Q_FUNC_INFO;

	if (m_trayIcon== NULL) return;

	switch (m_backlightStatus)
	{
	case Backlight::StatusOn:
		ui->pushButton_EnableDisableDevice->setIcon(QIcon(*m_pixmapCache[QStringLiteral("off16")]));
		ui->pushButton_EnableDisableDevice->setText("	" + tr("Turn lights OFF"));

		if (m_deviceLockStatus == DeviceLocked::Api)
		{
			m_labelStatusIcon->setPixmap(*m_pixmapCache[QStringLiteral("lock16")]);
			if (m_trayIcon)
				m_trayIcon->setStatus(SysTrayIcon::StatusLockedByApi);
		} else
			if (m_deviceLockStatus == DeviceLocked::Plugin)
			{
				m_labelStatusIcon->setPixmap(*m_pixmapCache[QStringLiteral("lock16")]);
				if (m_trayIcon)
					m_trayIcon->setStatus(SysTrayIcon::StatusLockedByPlugin, &m_deviceLockModule);
			} else
				if (m_deviceLockStatus == DeviceLocked::ApiPersist)
				{
					m_labelStatusIcon->setPixmap(*m_pixmapCache[QStringLiteral("persist16")]);
					if (m_trayIcon)
						m_trayIcon->setStatus(SysTrayIcon::StatusApiPersist);
				} else {
						m_labelStatusIcon->setPixmap(*m_pixmapCache[QStringLiteral("on16")]);
						if (m_trayIcon)
							m_trayIcon->setStatus(SysTrayIcon::StatusOn);
				}
		break;

	case Backlight::StatusOff:
		m_labelStatusIcon->setPixmap(*m_pixmapCache[QStringLiteral("off16")]);
		ui->pushButton_EnableDisableDevice->setIcon(QIcon(*m_pixmapCache[QStringLiteral("on16")]));
		ui->pushButton_EnableDisableDevice->setText(QStringLiteral("	%1").arg(tr("Turn lights ON")));
		if (m_trayIcon)
			m_trayIcon->setStatus(SysTrayIcon::StatusOff);
		break;

	case Backlight::StatusDeviceError:
		m_labelStatusIcon->setPixmap(*m_pixmapCache[QStringLiteral("error16")]);
		ui->pushButton_EnableDisableDevice->setIcon(QIcon(*m_pixmapCache[QStringLiteral("off16")]));
		ui->pushButton_EnableDisableDevice->setText(QStringLiteral("	%1").arg(tr("Turn lights OFF")));
		if (m_trayIcon)
			m_trayIcon->setStatus(SysTrayIcon::StatusError);
		break;
	default:
		qWarning() << Q_FUNC_INFO << "m_backlightStatus = " << m_backlightStatus;
		break;
	}
	if (m_trayIcon)
		m_labelStatusIcon->setToolTip(m_trayIcon->toolTip());
}

void SettingsWindow::initGrabbersRadioButtonsVisibility()
{
#ifndef WGC_GRAB_SUPPORT
	ui->radioButton_GrabWgc->setVisible(false);
#else
	ui->radioButton_GrabWgc->setChecked(true);
#endif
#ifndef WINAPI_GRAB_SUPPORT
	ui->radioButton_GrabWinAPI->setVisible(false);
#else
	if (!ui->radioButton_GrabWgc->isChecked())
		ui->radioButton_GrabWinAPI->setChecked(true);
#endif
#ifndef DDUPL_GRAB_SUPPORT
	ui->radioButton_GrabDDupl->setVisible(false);
#endif
#ifndef D3D10_GRAB_SUPPORT
	ui->checkBox_EnableDx1011Capture->setVisible(false);
	ui->checkBox_EnableDx9Capture->setVisible(false);
#endif
#ifndef X11_GRAB_SUPPORT
	ui->radioButton_GrabX11->setVisible(false);
#else
	ui->radioButton_GrabX11->setChecked(true);
#endif
#ifndef MAC_OS_AV_GRAB_SUPPORT
	ui->radioButton_GrabMacAVFoundation->setVisible(false);
#else
	ui->radioButton_GrabMacAVFoundation->setChecked(true);
#endif
#ifndef MAC_OS_CG_GRAB_SUPPORT
	ui->radioButton_GrabMacCoreGraphics->setVisible(false);
#else
	ui->radioButton_GrabMacCoreGraphics->setChecked(true);
#endif
}

// ----------------------------------------------------------------------------
// Show grabbed colors in another GUI
// ----------------------------------------------------------------------------

void SettingsWindow::initVirtualLeds(int virtualLedsCount)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << virtualLedsCount;

	// Remove all virtual leds from grid layout
	for (int i = 0; i < m_labelsGrabbedColors.count(); i++)
	{
		ui->gridLayout_VirtualLeds->removeWidget(m_labelsGrabbedColors[i]);
		m_labelsGrabbedColors[i]->deleteLater();
	}

	m_labelsGrabbedColors.clear();

	for (int i = 0; i < virtualLedsCount; i++)
	{
		QLabel *label = new QLabel(this);
		label->setText(QString::number(i + 1));
		label->setAlignment(Qt::AlignCenter);
		label->setAutoFillBackground(true);

		if (m_backlightStatus == Backlight::StatusOff)
		{
			// If status off fill labels black:
			QPalette pal = label->palette();
			pal.setBrush(QPalette::Window, QBrush(Qt::black));
			label->setPalette(pal);
		}

		m_labelsGrabbedColors.append(label);

		int row = i / 10;
		int col = i % 10;

		ui->gridLayout_VirtualLeds->addWidget(label, row, col);
	}

	ui->frame_VirtualLeds->update();
}

void SettingsWindow::updateVirtualLedsColors(const QList<QRgb> & colors)
{
	DEBUG_HIGH_LEVEL << Q_FUNC_INFO;

	if (colors.count() != m_labelsGrabbedColors.count())
	{
		qWarning() << Q_FUNC_INFO << "colors.count()" << colors.count() << "!=" << "m_labelsGrabbedColors.count()" << m_labelsGrabbedColors.count() << "."
					<< "Cancel updating virtual colors." << sender();
		return;
	}

	for (int i = 0; i < colors.count(); i++)
	{
		QLabel *label = m_labelsGrabbedColors[i];
		QColor color(colors[i]);

		QPalette pal = label->palette();
		pal.setBrush(QPalette::Window, QBrush(color));
		pal.setColor(label->foregroundRole(), PrismatikMath::getBrightness(colors[i]) > 150 ? Qt::black : Qt::white);
		label->setPalette(pal);
	}
}

void SettingsWindow::requestBacklightStatus()
{
	emit resultBacklightStatus(m_backlightStatus);
}

void SettingsWindow::onApiServer_ErrorOnStartListening(const QString& errorMessage)
{
	ui->lineEdit_ApiPort->setStyleSheet(QStringLiteral("background-color:red;"));
	ui->lineEdit_ApiPort->setToolTip(errorMessage);
}

void SettingsWindow::onPingDeviceEverySecond_Toggled(bool state)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << state;

	Settings::setPingDeviceEverySecond(state);

	// Force update colors on device for start ping device
	//	m_grabManager->reset();
	//	m_moodlampManager->reset();
}

void SettingsWindow::processMessage(const QString &message)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << message;

	if (message == QStringLiteral("on"))
		setBacklightStatus(Backlight::StatusOn);
	else if (message == QStringLiteral("off"))
		setBacklightStatus(Backlight::StatusOff);
	else if (message.startsWith(QStringLiteral("set-profile "))) {
		QString profile = message.mid(12);
		profileSwitch(profile);
	} else if (message == QStringLiteral("quitForWizard")) {
		qWarning() << "Wizard was started, quitting!";
		LightpackApplication::quit();
	} else if (m_trayIcon != NULL) { // "alreadyRunning"
		qWarning() << message;
		m_trayIcon->showMessage(SysTrayIcon::MessageAnotherInstance);
		this->show();
		this->activateWindow();
	}
}

#ifdef SOUNDVIZ_SUPPORT
void SettingsWindow::updateAvailableSoundVizDevices(const QList<SoundManagerDeviceInfo> & devices, int recommended)
{
	ui->comboBox_SoundVizDevice->blockSignals(true);
	ui->comboBox_SoundVizDevice->clear();
	int selectedDevice = Settings::getSoundVisualizerDevice();
	if (selectedDevice == -1) selectedDevice = recommended;
	int selectIndex = -1;
	for (int i = 0; i < devices.size(); i++) {
		ui->comboBox_SoundVizDevice->addItem(devices[i].name, devices[i].id);
		if (devices[i].id == selectedDevice) {
			selectIndex = i;
		}
	}
	ui->comboBox_SoundVizDevice->setCurrentIndex(selectIndex);
	ui->comboBox_SoundVizDevice->blockSignals(false);
}

void SettingsWindow::updateAvailableSoundVizVisualizers(const QList<SoundManagerVisualizerInfo> & visualizers, int recommended)
{
	ui->comboBox_SoundVizVisualizer->blockSignals(true);
	ui->comboBox_SoundVizVisualizer->clear();
	int selectedVisualizer = Settings::getSoundVisualizerVisualizer();
	if (selectedVisualizer == -1) selectedVisualizer = recommended;
	int selectIndex = -1;
	for (int i = 0; i < visualizers.size(); i++) {
		ui->comboBox_SoundVizVisualizer->addItem(visualizers[i].name, visualizers[i].id);
		if (visualizers[i].id == selectedVisualizer) {
			selectIndex = i;
		}
	}
	ui->comboBox_SoundVizVisualizer->setCurrentIndex(selectIndex);
	ui->comboBox_SoundVizVisualizer->blockSignals(false);
}
#endif

void SettingsWindow::updateAvailableMoodLampLamps(const QList<MoodLampLampInfo> & lamps, int recommended)
{
	ui->comboBox_MoodLampLamp->blockSignals(true);
	ui->comboBox_MoodLampLamp->clear();
	int selectedLamp = Settings::getMoodLampLamp();
	if (selectedLamp == -1) selectedLamp = recommended;
	int selectIndex = -1;
	for (int i = 0; i < lamps.size(); i++) {
		ui->comboBox_MoodLampLamp->addItem(lamps[i].name, lamps[i].id);
		if (lamps[i].id == selectedLamp) {
			selectIndex = i;
		}
	}
	ui->comboBox_MoodLampLamp->setCurrentIndex(selectIndex);
	ui->comboBox_MoodLampLamp->blockSignals(false);
}

// ----------------------------------------------------------------------------
// Show / Hide settings and about windows
// ----------------------------------------------------------------------------

void SettingsWindow::showAbout()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	emit requestFirmwareVersion();

	ui->tabWidget->setCurrentWidget(ui->tabAbout);
	this->show();

	using namespace std::chrono_literals;
	m_smoothScrollTimer.setInterval(100ms);
	connect(&m_smoothScrollTimer, &QTimer::timeout, this, &SettingsWindow::scrollThanks);
	m_smoothScrollTimer.start();
}

void SettingsWindow::scrollThanks()
{
	QScrollBar *scrollBar = this->ui->textBrowser->verticalScrollBar();

	scrollBar->setValue(scrollBar->value() + 1);

}

void SettingsWindow::showSettings()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	// A window started hidden in the tray may still carry a minimized state.
	// Fully restore it before asking Windows to bring it to the foreground.
	setWindowState(windowState() & ~Qt::WindowMinimized);
	showNormal();
	setVisible(true);
	raise();
	activateWindow();
	QTimer::singleShot(0, this, [this] {
		raise();
		activateWindow();
		setFocus(Qt::ActiveWindowFocusReason);
	});
}

void SettingsWindow::hideSettings()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	emit showLedWidgets(false);

	this->hide();
}

void SettingsWindow::toggleSettings()
{
	if(this->isVisible())
		hideSettings();
	else
		showSettings();
}


// ----------------------------------------------------------------------------
// Public slots
// ----------------------------------------------------------------------------

void SettingsWindow::ledDeviceOpenSuccess(bool isSuccess)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << isSuccess;

	if (isSuccess)
	{
		// Device just connected and for updating colors
		// we should reset previous saved states
		//m_grabManager->reset();
		// m_moodlampManager->reset();
	}

	ledDeviceCallSuccess(isSuccess);
}

void SettingsWindow::ledDeviceCallSuccess(bool isSuccess)
{
	DEBUG_HIGH_LEVEL << Q_FUNC_INFO << isSuccess << m_backlightStatus << sender();

	// If Backlight::StatusOff then nothings changed

	if (isSuccess == false)
	{
		if (m_backlightStatus == Backlight::StatusOn) {
			m_backlightStatus = Backlight::StatusDeviceError;
			updateTrayAndActionStates();
		}
		DEBUG_LOW_LEVEL << Q_FUNC_INFO << "Backlight::StatusDeviceError";
	} else {
		if (m_backlightStatus == Backlight::StatusDeviceError) {
			m_backlightStatus = Backlight::StatusOn;
			updateTrayAndActionStates();
		}
	}
}

void SettingsWindow::ledDeviceFirmwareVersionResult(const QString & fwVersion)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << fwVersion;

	m_deviceFirmwareVersion = fwVersion;

	QString aboutDialogFirmwareString = m_deviceFirmwareVersion;

	if (Settings::getConnectedDevice() == SupportedDevices::DeviceTypeLightpack)
	{
		if (m_deviceFirmwareVersion == QStringLiteral("5.0") || m_deviceFirmwareVersion == QStringLiteral("4.3"))
		{
			aboutDialogFirmwareString += QStringLiteral(" (<a href=\"%1\">%2</a>").arg(LightpackDownloadsPageUrl, tr("update firmware"));

			if (Settings::isUpdateFirmwareMessageShown() == false)
			{
				if (m_trayIcon!=NULL)
					m_trayIcon->showMessage(SysTrayIcon::MessageUpdateFirmware);
				Settings::setUpdateFirmwareMessageShown(true);
			}
		}
	}

	this->setFirmwareVersion(aboutDialogFirmwareString);

	updateDeviceTabWidgetsVisibility();
}

void SettingsWindow::ledDeviceFirmwareVersionUnofficialResult(const int version) {
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << version;

	// Here we handle certain options that have to be hidden/made visible depending on our version
	ui->checkBox_DisableUsbPowerLed->setVisible(version >= 1);
}

void SettingsWindow::refreshAmbilightEvaluated(double updateResultMs)
{
	DEBUG_HIGH_LEVEL << Q_FUNC_INFO << updateResultMs;

	double hz = 0;

	if (updateResultMs != 0)
		hz = 1000.0 / updateResultMs; /* ms to hz */

	QString fpsText = QString::number(hz, 'f', 0);
	if (ui->comboBox_LightpackModes->currentIndex() == GrabModeIndex) {
		const double maxHz = 1000.0 / ui->spinBox_GrabSlowdown->value(); // cap with display refresh rate?
		fpsText += QStringLiteral(" / %1").arg(QString::number(maxHz, 'f', 0));
	}
	ui->label_GrabFrequency_value->setText(fpsText);

	const SupportedDevices::DeviceType device = Settings::getConnectedDevice();

	if (device == SupportedDevices::DeviceTypeArdulight || device == SupportedDevices::DeviceTypeAdalight) {
		const double ledCount = static_cast<double>(Settings::getNumberOfLeds(device));
		const double baudRate = static_cast<double>(device == SupportedDevices::DeviceTypeAdalight ? Settings::getAdalightSerialPortBaudRate() : Settings::getArdulightSerialPortBaudRate());
		m_maxFPS = std::max(hz, m_maxFPS);
		const double theoreticalMaxHz = PrismatikMath::theoreticalMaxFrameRate(ledCount, baudRate);

		DEBUG_HIGH_LEVEL << Q_FUNC_INFO << "Therotical Max Hz for led count and baud rate:" << theoreticalMaxHz << ledCount << baudRate;
		if (theoreticalMaxHz <= hz)
			qWarning() << Q_FUNC_INFO << hz << "FPS went over theoretical max of" << theoreticalMaxHz;

		const QPalette& defaultPalette = ui->label_GrabFrequency_txt_fps->palette();

		QPalette palette = ui->label_GrabFrequency_value->palette();
		if (theoreticalMaxHz <= m_maxFPS) {
			palette.setColor(QPalette::WindowText, Qt::red);
			fpsText += BaudrateWarningSign;

			QString toolTipMsg = tr(
"<html><body><p>Your frame rate reached <b>%1 FPS</b>, your baud rate of <b>%2</b> might be too low for the amount of LEDs (%3).</p>\
<p>You might experience lag or visual artifacts with your LEDs.</p>\
<p>Lower your target framerate to <b>under %4 FPS</b> or increase your baud rate to <b>above %5</b>.</p></body></html>")
			.arg(m_maxFPS, 0, 'f', 0).arg(baudRate).arg(ledCount)
			.arg(PrismatikMath::theoreticalMaxFrameRate(ledCount, baudRate), 0, 'f', 0)
			.arg(std::round(PrismatikMath::theoreticalMinBaudRate(ledCount, m_maxFPS) / 100.0) * 100.0, 0, 'f', 0);
			this->labelFPS->setToolTip(toolTipMsg);
			using namespace std::chrono_literals;
			m_baudrateWarningClearTimer.start(15s);
		} else
			palette.setColor(QPalette::WindowText, defaultPalette.color(QPalette::WindowText));

		ui->label_GrabFrequency_value->setPalette(palette);
		this->labelFPS->setPalette(palette);
	}

	this->labelFPS->setText(tr("FPS: ") + fpsText);
	if (m_topFps) m_topFps->setText(fpsText);
}

void SettingsWindow::clearBaudrateWarning()
{
	const QPalette& defaultPalette = ui->label_GrabFrequency_txt_fps->palette();
	QPalette palette = ui->label_GrabFrequency_value->palette();
	palette.setColor(QPalette::WindowText, defaultPalette.color(QPalette::WindowText));

	ui->label_GrabFrequency_value->setPalette(palette);
	this->labelFPS->setPalette(palette);
	this->labelFPS->setToolTip(QLatin1String(""));

	this->labelFPS->setText(this->labelFPS->text().remove(BaudrateWarningSign));
}

// ----------------------------------------------------------------------------
// UI handlers
// ----------------------------------------------------------------------------

void SettingsWindow::onGrabberChanged()
{
	if (!updatingFromSettings) {
		Grab::GrabberType grabberType = getSelectedGrabberType();

		if (grabberType != Settings::getGrabberType()) {
			DEBUG_LOW_LEVEL << Q_FUNC_INFO << "GrabberType: " << grabberType << ", isDx1011CaptureEnabled: " << isDx1011CaptureEnabled();
			Settings::setGrabberType(grabberType);
		}
	}
}

void SettingsWindow::onDx1011CaptureEnabledChanged(bool isEnabled) {
	if (!updatingFromSettings) {
		DEBUG_LOW_LEVEL << Q_FUNC_INFO << isEnabled;
#ifdef D3D10_GRAB_SUPPORT
		Settings::setDx1011GrabberEnabled(isEnabled);
#endif
	}
	ui->checkBox_EnableDx9Capture->setEnabled(isEnabled);
}

void SettingsWindow::onDx9CaptureEnabledChanged(bool isEnabled) {
	if (!updatingFromSettings) {
		DEBUG_LOW_LEVEL << Q_FUNC_INFO << isEnabled;
#ifdef D3D10_GRAB_SUPPORT
		Settings::setDx9GrabbingEnabled(isEnabled);
#endif
	}
}

void SettingsWindow::onGrabSlowdown_valueChanged(int value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;

	Settings::setGrabSlowdown(value);
	refreshAmbilightEvaluated(0);// update max grab rate
}

void SettingsWindow::onGrabIsAvgColors_toggled(bool state)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << state;

	Settings::setGrabAvgColorsEnabled(state);
}

void SettingsWindow::onGrabColorMode_currentIndexChanged(int index)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << index;
	updateGrabColorModeUi();
	if (!updatingFromSettings && index >= 0) {
		Settings::setGrabColorProcessingMode(ui->comboBox_GrabColorMode->itemData(index).toInt());
	}
}

void SettingsWindow::onGrabScenePreset_currentIndexChanged(int index)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << index;
	updateScenePresetUi();
	if (!updatingFromSettings && index >= 0) {
		Settings::setGrabScenePreset(m_comboScenePreset->itemData(index).toInt());
	}
}

void SettingsWindow::onGrabSmartCalibration_toggled(bool state)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << state;
	if (!updatingFromSettings) {
		Settings::setGrabSmartCalibrationEnabled(state);
	}
	updateScenePresetUi();
}

void SettingsWindow::onGrabOverBrighten_valueChanged(int value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;

	Settings::setGrabOverBrighten(value);
}

void SettingsWindow::onGrabApplyBlueLightReduction_toggled(bool state)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << state;

	Settings::setGrabApplyBlueLightReductionEnabled(state);
	if (state == true && ui->checkBox_GrabApplyColorTemperature->isChecked())
	{
		ui->checkBox_GrabApplyColorTemperature->setChecked(false);
		Settings::setGrabApplyColorTemperatureEnabled(false);
	}
}

void SettingsWindow::onGrabApplyColorTemperature_toggled(bool state)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << state;

	Settings::setGrabApplyColorTemperatureEnabled(state);
	if (state == true && ui->checkBox_GrabApplyBlueLightReduction->isChecked())
	{
		ui->checkBox_GrabApplyBlueLightReduction->setChecked(false);
		Settings::setGrabApplyBlueLightReductionEnabled(false);
	}
}

void SettingsWindow::onGrabColorTemperature_valueChanged(int value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;

	Settings::setGrabColorTemperature(value);
}

void SettingsWindow::onGrabGamma_valueChanged(double value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;

	Settings::setGrabGamma(value);
	ui->horizontalSlider_GrabGamma->setValue(floor((value * 100)));
}

void SettingsWindow::onSliderGrabGamma_valueChanged(int value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;
	Settings::setGrabGamma(value / 100.0);
	ui->doubleSpinBox_GrabGamma->setValue(value / 100.0);
}

void SettingsWindow::onLuminosityThreshold_valueChanged(int value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;

	Settings::setLuminosityThreshold(value);
}

void SettingsWindow::onMinimumLumosity_toggled(bool value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;

	Settings::setMinimumLuminosityEnabled(ui->radioButton_MinimumLuminosity->isChecked());
}

void SettingsWindow::onDeviceRefreshDelay_valueChanged(int value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;

	Settings::setDeviceRefreshDelay(value);
}

void SettingsWindow::onDisableUsbPowerLed_toggled(bool state) {
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << state;

	Settings::setDeviceUsbPowerLedDisabled(state);
}

void SettingsWindow::onDeviceSmooth_valueChanged(int value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;

	Settings::setDeviceSmooth(value);
}

void SettingsWindow::onDeviceBrightness_valueChanged(int percent)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << percent;

	Settings::setDeviceBrightness(percent);
}

void SettingsWindow::onDeviceBrightnessCap_valueChanged(int percent)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << percent;

	Settings::setDeviceBrightnessCap(percent);
}

void SettingsWindow::onDeviceColorDepth_valueChanged(int value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;

	Settings::setDeviceColorDepth(value);
}

void SettingsWindow::onDeviceGammaCorrection_valueChanged(double value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;

	Settings::setDeviceGamma(value);
	ui->horizontalSlider_GammaCorrection->setValue(floor((value * 100 + 0.5)));
	emit updateGamma(Settings::getDeviceGamma());
}

void SettingsWindow::onSliderDeviceGammaCorrection_valueChanged(int value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;
	Settings::setDeviceGamma(static_cast<double>(value + 0.4) / 100);
	ui->doubleSpinBox_DeviceGamma->setValue(Settings::getDeviceGamma());
	emit updateGamma(Settings::getDeviceGamma());
}

void SettingsWindow::onDeviceDitheringEnabled_toggled(bool state) {
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << state;

	Settings::setDeviceDitheringEnabled(state);
}


void SettingsWindow::onLightpackModes_currentIndexChanged(int index)
{
	if (updatingFromSettings) return;

	DEBUG_LOW_LEVEL << Q_FUNC_INFO << index << sender();

	using namespace Lightpack;

	switch (index) {
		case MoodLampModeIndex:
			Settings::setLightpackMode(MoodLampMode);
			break;
#ifdef SOUNDVIZ_SUPPORT
		case SoundVisualizeModeIndex:
			Settings::setLightpackMode(SoundVisualizeMode);
			break;
#endif
		default:
			Settings::setLightpackMode(AmbilightMode);

	}
}

void SettingsWindow::onLightpackModeChanged(Lightpack::Mode mode)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << mode << ui->comboBox_LightpackModes->currentIndex();

	switch (mode)
	{
	case Lightpack::AmbilightMode:
		ui->comboBox_LightpackModes->setCurrentIndex(GrabModeIndex);
		ui->stackedWidget_LightpackModes->setCurrentIndex(GrabModeIndex);
		emit showLedWidgets(!ui->radioButton_GrabWidgetsDontShow->isChecked() && this->isVisible());
		break;

	case Lightpack::MoodLampMode:
		ui->comboBox_LightpackModes->setCurrentIndex(MoodLampModeIndex);
		ui->stackedWidget_LightpackModes->setCurrentIndex(MoodLampModeIndex);
		emit showLedWidgets(false);
		break;

#ifdef SOUNDVIZ_SUPPORT
	case SoundVisualizeModeIndex:
		ui->comboBox_LightpackModes->setCurrentIndex(SoundVisualizeModeIndex);
		ui->stackedWidget_LightpackModes->setCurrentIndex(SoundVisualizeModeIndex);
		emit showLedWidgets(false);
		break;
#endif

	default:
		DEBUG_LOW_LEVEL << "LightpacckMode unsuppotred value =" << mode;
		break;
	}
	emit backlightStatusChanged(m_backlightStatus);
}

void SettingsWindow::onMoodLampColor_changed(QColor color)
{
	DEBUG_MID_LEVEL << Q_FUNC_INFO << color;
	Settings::setMoodLampColor(color);
}

void SettingsWindow::onMoodLampSpeed_valueChanged(int value)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << value;
	Settings::setMoodLampSpeed(value);
}

void SettingsWindow::onMoodLampLamp_currentIndexChanged(int index)
{
	if (!updatingFromSettings) {
		DEBUG_MID_LEVEL << Q_FUNC_INFO << index << ui->comboBox_MoodLampLamp->currentData().toInt();
		Settings::setMoodLampLamp(ui->comboBox_MoodLampLamp->currentData().toInt());
	}
}

void SettingsWindow::onMoodLampLiquidMode_Toggled(bool checked)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << checked;

	Settings::setMoodLampLiquidMode(checked);
	if (Settings::isMoodLampLiquidMode())
	{
		ui->pushButton_SelectColorMoodLamp->setEnabled(false);
		ui->horizontalSlider_MoodLampSpeed->setEnabled(true);
	} else {
		ui->pushButton_SelectColorMoodLamp->setEnabled(true);
		ui->horizontalSlider_MoodLampSpeed->setEnabled(false);
	}
}

#ifdef SOUNDVIZ_SUPPORT
void SettingsWindow::onSoundVizDevice_currentIndexChanged(int index)
{
	if (!updatingFromSettings) {
		DEBUG_MID_LEVEL << Q_FUNC_INFO << index << ui->comboBox_SoundVizDevice->currentData().toInt();
		Settings::setSoundVisualizerDevice(ui->comboBox_SoundVizDevice->currentData().toInt());
	}
}

void SettingsWindow::onSoundVizVisualizer_currentIndexChanged(int index)
{
	if (!updatingFromSettings) {
		DEBUG_MID_LEVEL << Q_FUNC_INFO << index << ui->comboBox_SoundVizVisualizer->currentData().toInt();
		Settings::setSoundVisualizerVisualizer(ui->comboBox_SoundVizVisualizer->currentData().toInt());
	}
}

void SettingsWindow::onSoundVizMinColor_changed(QColor color)
{
	DEBUG_MID_LEVEL << Q_FUNC_INFO << color;
	Settings::setSoundVisualizerMinColor(color);
}

void SettingsWindow::onSoundVizMaxColor_changed(QColor color)
{
	DEBUG_MID_LEVEL << Q_FUNC_INFO << color;
	Settings::setSoundVisualizerMaxColor(color);
}

void SettingsWindow::onSoundVizLiquidMode_Toggled(bool isLiquidMode)
{
	DEBUG_MID_LEVEL << Q_FUNC_INFO << isLiquidMode;

	Settings::setSoundVisualizerLiquidMode(isLiquidMode);
	if (Settings::isSoundVisualizerLiquidMode())
	{
		ui->pushButton_SelectColorSoundVizMin->setEnabled(false);
		ui->pushButton_SelectColorSoundVizMax->setEnabled(false);
		ui->horizontalSlider_SoundVizLiquidSpeed->setEnabled(true);
	} else {
		ui->pushButton_SelectColorSoundVizMin->setEnabled(true);
		ui->pushButton_SelectColorSoundVizMax->setEnabled(true);
		ui->horizontalSlider_SoundVizLiquidSpeed->setEnabled(false);
	}
}

void SettingsWindow::onSoundVizLiquidSpeed_valueChanged(int value)
{
	DEBUG_MID_LEVEL << Q_FUNC_INFO << value;
	Settings::setSoundVisualizerLiquidSpeed(value);
}
#endif

void SettingsWindow::onDontShowLedWidgets_Toggled(bool checked)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << checked;
	emit showLedWidgets(!checked);
}

void SettingsWindow::onSetColoredLedWidgets(bool checked)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;
	if (checked)
		emit setColoredLedWidget(true);
}

void SettingsWindow::onSetWhiteLedWidgets(bool checked)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;
	if (checked)
		emit setColoredLedWidget(false);
}

void SettingsWindow::onDeviceSendDataOnlyIfColorsChanged_toggled(bool state)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << state;

	Settings::setSendDataOnlyIfColorsChanges(state);
}

// ----------------------------------------------------------------------------

void SettingsWindow::openFile(const QString &filePath)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	QString filePrefix = QStringLiteral("file://");

#ifdef Q_OS_WIN
	filePrefix = QStringLiteral("file:///");
#endif

	QDesktopServices::openUrl(QUrl(filePrefix + filePath, QUrl::TolerantMode));
}

// ----------------------------------------------------------------------------
// Profiles
// ----------------------------------------------------------------------------

void SettingsWindow::openCurrentProfile()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	openFile(Settings::getCurrentProfilePath());
}

void SettingsWindow::exportCurrentProfile()
{
	const QString currentProfilePath = Settings::getCurrentProfilePath();
	const QFileInfo currentInfo(currentProfilePath);
	const QString defaultPath = QDir::homePath() + QStringLiteral("/") + currentInfo.completeBaseName() + QStringLiteral(".ini");
	const QString filePath = QFileDialog::getSaveFileName(
		this,
		tr("Export profile"),
		defaultPath,
		tr("Prismatik profile (*.ini);;All files (*.*)"));

	if (filePath.isEmpty())
		return;

	QFile::remove(filePath);
	if (!QFile::copy(currentProfilePath, filePath)) {
		QMessageBox::warning(this, tr("Export failed"), tr("Could not export the current profile."));
		return;
	}

	statusBar()->showMessage(tr("Profile exported to %1").arg(QDir::toNativeSeparators(filePath)), 5000);
}

QString SettingsWindow::importedProfileNameFromFile(const QString &filePath) const
{
	QString baseName = QFileInfo(filePath).completeBaseName().trimmed();
	if (baseName.isEmpty())
		baseName = tr("Imported profile");

	baseName.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
	QString candidate = baseName;
	int suffix = 1;
	while (Settings::findAllProfiles().contains(candidate)) {
		candidate = tr("%1 %2").arg(baseName).arg(QString::number(suffix++));
	}
	return candidate;
}

void SettingsWindow::importProfile()
{
	const QString filePath = QFileDialog::getOpenFileName(
		this,
		tr("Import profile"),
		QDir::homePath(),
		tr("Prismatik profile (*.ini);;All files (*.*)"));

	if (filePath.isEmpty())
		return;

	const QString importedName = importedProfileNameFromFile(filePath);
	const QString targetPath = QStringLiteral("%1%2.ini").arg(Settings::getProfilesPath(), importedName);

	QFile::remove(targetPath);
	if (!QFile::copy(filePath, targetPath)) {
		QMessageBox::warning(this, tr("Import failed"), tr("Could not import the selected profile."));
		return;
	}

	profilesLoadAll();
	profileSwitch(importedName);
	statusBar()->showMessage(tr("Imported as profile: %1").arg(importedName), 5000);
}

void SettingsWindow::profileRename()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	QString configName = ui->comboBox_Profiles->currentText().trimmed();
	ui->comboBox_Profiles->lineEdit()->setText(configName);

	// Signal editingFinished() will be emited if focus wasn't lost (for example when return pressed),
	// and profileRename() function will be called again here
	this->setFocus(Qt::OtherFocusReason);

	if (Settings::getCurrentProfileName() == configName)
	{
		DEBUG_LOW_LEVEL << Q_FUNC_INFO << "Nothing has changed";
		return;
	}

	if (configName.isEmpty())
	{
		configName = Settings::getCurrentProfileName();
		DEBUG_LOW_LEVEL << Q_FUNC_INFO << "Profile name is empty, return back to" << configName;
	}
	else
	{
		Settings::renameCurrentProfile(configName);
	}

	ui->comboBox_Profiles->lineEdit()->setText(configName);
	ui->comboBox_Profiles->setItemText(ui->comboBox_Profiles->currentIndex(), configName);
}

void SettingsWindow::profileSwitch(const QString & configName)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << configName;

	profilesLoadAll();

	int index = ui->comboBox_Profiles->findText(configName);

	if (index < 0)
	{
		qCritical() << Q_FUNC_INFO << "Fail find text:" << configName << "in profiles combobox";
		return;
	}

	ui->comboBox_Profiles->setCurrentIndex(index);

	Settings::loadOrCreateProfile(configName);

	if (m_trayIcon)
		m_trayIcon->updateProfiles();

}

void SettingsWindow::handleProfileLoaded(const QString &configName) {

	this->labelProfile->setText(tr("Profile: %1").arg(configName));
	updateUiFromSettings();
}

void SettingsWindow::profileTraySwitch(const QString &profileName)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << "switch to" << profileName;
	profileSwitchCombobox(profileName);
	return;
}

void SettingsWindow::profileSwitchCombobox(const QString& profile)
{
	const int index = ui->comboBox_Profiles->findText(profile);
	ui->comboBox_Profiles->setCurrentIndex(index);
}

void SettingsWindow::profileNew()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	QString profileName = tr("New profile");

	if(ui->comboBox_Profiles->findText(profileName) != -1){
		int i = 1;
		while(ui->comboBox_Profiles->findText(profileName +" "+ QString::number(i)) != -1){
			i++;
		}
		profileName += QStringLiteral(" %1").arg(QString::number(i));
	}

	ui->comboBox_Profiles->insertItem(0, profileName);
	ui->comboBox_Profiles->setCurrentIndex(0);

	ui->comboBox_Profiles->lineEdit()->selectAll();
	ui->comboBox_Profiles->lineEdit()->setFocus(Qt::MouseFocusReason);
}

void SettingsWindow::profileResetToDefaultCurrent()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	Settings::resetDefaults();

	// By default backlight is enabled, but make it same as current backlight status for usability purposes
	Settings::setIsBacklightEnabled(m_backlightStatus != Backlight::StatusOff);

	// Update settings
	updateUiFromSettings();
}

void SettingsWindow::profileDeleteCurrent()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	if(ui->comboBox_Profiles->count() <= 1){
		qWarning() << "void MainWindow::profileDeleteCurrent(): profiles count ==" << ui->comboBox_Profiles->count();
		return;
	}

	// Delete settings file
	Settings::removeCurrentProfile();
	// Remove from combobox
	ui->comboBox_Profiles->removeItem(ui->comboBox_Profiles->currentIndex());
}

void SettingsWindow::profilesLoadAll()
{
	QStringList profiles = Settings::findAllProfiles();

	DEBUG_LOW_LEVEL << Q_FUNC_INFO << "found profiles:" << profiles;


	disconnect(ui->comboBox_Profiles, &QComboBox::currentTextChanged, this, &SettingsWindow::profileSwitch);

	for (int i = 0; i < profiles.count(); i++)
	{
		if (ui->comboBox_Profiles->findText(profiles.at(i)) == -1)
			ui->comboBox_Profiles->addItem(profiles.at(i));
	}

	connect(ui->comboBox_Profiles, &QComboBox::currentTextChanged, this, &SettingsWindow::profileSwitch);
}

void SettingsWindow::settingsProfileChanged_UpdateUI(const QString &profileName)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	setWindowTitle(tr("ChairoLight: %1").arg(profileName));

	if (m_backlightStatus == Backlight::StatusOn && m_trayIcon!=NULL)
		m_trayIcon->updateProfiles();

	if(ui->comboBox_Profiles->count() > 1){
		ui->pushButton_DeleteProfile->setEnabled(true);
	}else{
		ui->pushButton_DeleteProfile->setEnabled(false);
	}
}

// ----------------------------------------------------------------------------

void SettingsWindow::initPixmapCache()
{
	m_pixmapCache.insert(QStringLiteral("lock16"), new QPixmap(QPixmap(QStringLiteral(":/icons/lock.png")).scaledToWidth(16, Qt::SmoothTransformation)));
	m_pixmapCache.insert(QStringLiteral("on16"), new QPixmap(QPixmap(QStringLiteral(":/icons/on.png")).scaledToWidth(16, Qt::SmoothTransformation)) );
	m_pixmapCache.insert(QStringLiteral("off16"), new QPixmap(QPixmap(QStringLiteral(":/icons/off.png")).scaledToWidth(16, Qt::SmoothTransformation)) );
	m_pixmapCache.insert(QStringLiteral("error16"), new QPixmap(QPixmap(QStringLiteral(":/icons/error.png")).scaledToWidth(16, Qt::SmoothTransformation)) );
	m_pixmapCache.insert(QStringLiteral("persist16"), new QPixmap(QPixmap(QStringLiteral(":/icons/persist.png")).scaledToWidth(16, Qt::SmoothTransformation)));
}

//void SettingsWindow::handleConnectedDeviceChange(const SupportedDevices::DeviceType deviceType) {
//	this->labelDevice->setText(tr("Device: %1").arg(Settings::getConnectedDeviceName()));
//	updateUiFromSettings();
//}

// ----------------------------------------------------------------------------
// Translate GUI
// ----------------------------------------------------------------------------

void SettingsWindow::initLanguages()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	ui->comboBox_Language->clear();
	ui->comboBox_Language->addItem(tr("System default"));
	ui->comboBox_Language->addItem(QStringLiteral("English"));
	ui->comboBox_Language->addItem(QStringLiteral("Russian"));
	ui->comboBox_Language->addItem(QStringLiteral("Ukrainian"));

	int langIndex = 0; // "System default"
	QString langSaved = Settings::getLanguage();
	if(langSaved != QStringLiteral("<System>")){
		langIndex = ui->comboBox_Language->findText(langSaved);
	}
	ui->comboBox_Language->setCurrentIndex(langIndex);

	m_translator = NULL;
}

void SettingsWindow::loadTranslation(const QString & language)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << language;

	QString settingsLanguage = language;

	QString locale = QLocale::system().name();

	// add translation to Lightpack.pro TRANSLATIONS
	// lupdate Lightpack.pro
	// open linguist and translate application
	// lrelease Lightpack.pro
	// add new language to LightpackResources.qrc :/translations/
	// add new language to MainWindow::initLanguages() function
	// and only when all this done - append new line
	// locale - name of translation binary file form resources: %locale%.qm
	if(ui->comboBox_Language->currentIndex() == 0 /* System */){
		settingsLanguage = SettingsScope::Main::LanguageDefault;
		DEBUG_LOW_LEVEL << "System locale" << locale;

		if (locale.startsWith(QStringLiteral("en_"))) locale = QStringLiteral("en_EN"); // :/translations/en_EN.qm
		else if (locale.startsWith(QStringLiteral("ru_"))) locale = QStringLiteral("ru_RU"); // :/translations/ru_RU.qm
		else if (locale.startsWith(QStringLiteral("uk_"))) locale = QStringLiteral("uk_UA"); // :/translations/uk_UA.qm

		DEBUG_LOW_LEVEL << "System translation" << locale;
	}
	else if (language == QStringLiteral("English")) locale = QStringLiteral("en_EN"); // :/translations/en_EN.qm
	else if (language == QStringLiteral("Russian")) locale = QStringLiteral("ru_RU"); // :/translations/ru_RU.qm
	else if (language == QStringLiteral("Ukrainian")) locale = QStringLiteral("uk_UA"); // :/translations/uk_UA.qm
	// append line for new language/locale here
	else {
		qWarning() << "Language" << language << "not found. Set to default" << SettingsScope::Main::LanguageDefault;
		DEBUG_LOW_LEVEL << "System locale" << locale;

		settingsLanguage = SettingsScope::Main::LanguageDefault;
	}

	Settings::setLanguage(settingsLanguage);

	const QString pathToLocale = QStringLiteral(":/translations/%1").arg(locale);

	if(m_translator != NULL){
		qApp->removeTranslator(m_translator);
		delete m_translator;
		m_translator = NULL;
	}

	if(locale == QStringLiteral("en_EN") /* default no need to translate */){
		DEBUG_LOW_LEVEL << "Translation removed, using default locale" << locale;
		return;
	}

	updatingFromSettings = true;
	m_translator = new QTranslator();
	if(m_translator->load(pathToLocale)){
		DEBUG_LOW_LEVEL << Q_FUNC_INFO << "Load translation for locale" << locale;
		qApp->installTranslator(m_translator);
	}else{
		qWarning() << "Fail load translation for locale" << locale << "pathToLocale" << pathToLocale;
	}
	updatingFromSettings = false;
}

// ----------------------------------------------------------------------------
// Create tray icon
// ----------------------------------------------------------------------------

void SettingsWindow::createTrayIcon()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;
	m_trayIcon = new SysTrayIcon();
	connect(m_trayIcon, &SysTrayIcon::quit, this, &SettingsWindow::quit);
	connect(m_trayIcon, &SysTrayIcon::showSettings, this, &SettingsWindow::showSettings);
	connect(m_trayIcon, &SysTrayIcon::toggleSettings, this, &SettingsWindow::toggleSettings);
	connect(m_trayIcon, &SysTrayIcon::backlightOn, this, &SettingsWindow::backlightOn);
	connect(m_trayIcon, &SysTrayIcon::backlightOff, this, &SettingsWindow::backlightOff);
	connect(m_trayIcon, &SysTrayIcon::profileSwitched, this, &SettingsWindow::profileTraySwitch);

	m_trayIcon->init();
	connect(this, &SettingsWindow::backlightStatusChanged, this, &SettingsWindow::updateTrayAndActionStates);
}

void SettingsWindow::updateUiFromSettings()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;
	updatingFromSettings = true;

	profilesLoadAll();

	ui->comboBox_Profiles->setCurrentIndex(ui->comboBox_Profiles->findText(Settings::getCurrentProfileName()));

	Lightpack::Mode mode = Settings::getLightpackMode();
	onLightpackModeChanged(mode);

	ui->checkBox_checkForUpdates->setChecked							(Settings::isCheckForUpdatesEnabled());
	ui->checkBox_installUpdates->setChecked							(Settings::isInstallUpdatesEnabled());

	ui->checkBox_SendDataOnlyIfColorsChanges->setChecked				(Settings::isSendDataOnlyIfColorsChanges());
	ui->checkBox_KeepLightsOnAfterExit->setChecked					(Settings::isKeepLightsOnAfterExit());
	ui->checkBox_KeepLightsOnAfterLockComputer->setChecked			(Settings::isKeepLightsOnAfterLock());
	ui->checkBox_KeepLightsOnAfterSuspend->setChecked				(Settings::isKeepLightsOnAfterSuspend());
	ui->checkBox_KeepLightsOnAfterScreenOff->setChecked				(Settings::isKeepLightsOnAfterScreenOff());
	ui->checkBox_PingDeviceEverySecond->setChecked					(Settings::isPingDeviceEverySecond());

	ui->checkBox_GrabIsAvgColors->setChecked							(Settings::isGrabAvgColorsEnabled());
	ui->comboBox_GrabColorMode->setCurrentIndex(ui->comboBox_GrabColorMode->findData(Settings::getGrabColorProcessingMode()));
	m_comboScenePreset->setCurrentIndex(m_comboScenePreset->findData(Settings::getGrabScenePreset()));
	m_checkSmartCalibration->setChecked(Settings::isGrabSmartCalibrationEnabled());
	ui->spinBox_GrabSlowdown->setValue								(Settings::getGrabSlowdown());
	ui->spinBox_GrabOverBrighten->setValue							(Settings::getGrabOverBrighten());
	ui->checkBox_GrabApplyBlueLightReduction->setChecked						(Settings::isGrabApplyBlueLightReductionEnabled());
	ui->checkBox_GrabApplyColorTemperature->setChecked              (Settings::isGrabApplyColorTemperatureEnabled());
	ui->spinBox_GrabColorTemperature->setValue                      (Settings::getGrabColorTemperature());
	ui->horizontalSlider_GrabColorTemperature->setValue             (Settings::getGrabColorTemperature());
	ui->doubleSpinBox_GrabGamma->setValue                           (Settings::getGrabGamma());
	ui->horizontalSlider_GrabGamma->setValue                        (Settings::getGrabGamma() * 100);
	ui->spinBox_LuminosityThreshold->setValue						(Settings::getLuminosityThreshold());
	updateGrabColorModeUi();
	updateScenePresetUi();

	// Check the selected moodlamp mode (setChecked(false) not working to select another)
	ui->radioButton_MinimumLuminosity->setChecked					(Settings::isMinimumLuminosityEnabled());
	ui->radioButton_LuminosityDeadZone->setChecked					(!Settings::isMinimumLuminosityEnabled());

	// Check the selected moodlamp mode (setChecked(false) not working to select another)
	ui->radioButton_ConstantColorMoodLampMode->setChecked			(!Settings::isMoodLampLiquidMode());
	ui->radioButton_LiquidColorMoodLampMode->setChecked				(Settings::isMoodLampLiquidMode());
	ui->pushButton_SelectColorMoodLamp->setColor						(Settings::getMoodLampColor());
	ui->horizontalSlider_MoodLampSpeed->setValue						(Settings::getMoodLampSpeed());
	for (int i = 0; i < ui->comboBox_MoodLampLamp->count(); i++) {
		if (ui->comboBox_MoodLampLamp->itemData(i).toInt() == Settings::getMoodLampLamp()) {
			ui->comboBox_MoodLampLamp->setCurrentIndex(i);
			break;
		}
	}

#ifdef SOUNDVIZ_SUPPORT
	for (int i = 0; i < ui->comboBox_SoundVizDevice->count(); i++) {
		if (ui->comboBox_SoundVizDevice->itemData(i).toInt() == Settings::getSoundVisualizerDevice()) {
			ui->comboBox_SoundVizDevice->setCurrentIndex(i);
			break;
		}
	}

	for (int i = 0; i < ui->comboBox_SoundVizVisualizer->count(); i++) {
		if (ui->comboBox_SoundVizVisualizer->itemData(i).toInt() == Settings::getSoundVisualizerVisualizer()) {
			ui->comboBox_SoundVizVisualizer->setCurrentIndex(i);
			break;
		}
	}

	ui->pushButton_SelectColorSoundVizMin->setColor					(Settings::getSoundVisualizerMinColor());
	ui->pushButton_SelectColorSoundVizMax->setColor					(Settings::getSoundVisualizerMaxColor());
	ui->radioButton_SoundVizConstantMode->setChecked				(!Settings::isSoundVisualizerLiquidMode());
	ui->radioButton_SoundVizLiquidMode->setChecked					(Settings::isSoundVisualizerLiquidMode());
	ui->horizontalSlider_SoundVizLiquidSpeed->setValue				(Settings::getSoundVisualizerLiquidSpeed());
#endif

	ui->checkBox_DisableUsbPowerLed->setChecked						(Settings::isDeviceUsbPowerLedDisabled());
	ui->horizontalSlider_DeviceRefreshDelay->setValue				(Settings::getDeviceRefreshDelay());
	ui->horizontalSlider_DeviceBrightness->setValue					(Settings::getDeviceBrightness());
	ui->horizontalSlider_DeviceBrightnessCap->setValue				(Settings::getDeviceBrightnessCap());
	ui->horizontalSlider_DeviceSmooth->setValue						(Settings::getDeviceSmooth());
	ui->horizontalSlider_DeviceColorDepth->setValue					(Settings::getDeviceColorDepth());
	ui->doubleSpinBox_DeviceGamma->setValue							(Settings::getDeviceGamma());
	ui->horizontalSlider_GammaCorrection->setValue					(floor((Settings::getDeviceGamma() * 100 + 0.5)));
	ui->checkBox_EnableDithering->setChecked						(Settings::isDeviceDitheringEnabled());

	ui->groupBox_Api->setChecked									(Settings::isApiEnabled());
	ui->checkBox_listenOnlyOnLoInterface->setChecked				(Settings::isListenOnlyOnLoInterface());
	ui->lineEdit_ApiPort->setText									(QString::number(Settings::getApiPort()));
	ui->lineEdit_ApiPort->setValidator								(new QIntValidator(1, 49151));
	ui->lineEdit_ApiKey->setText									(Settings::getApiAuthKey());
	ui->spinBox_LoggingLevel->setValue								(g_debugLevel);

	if (g_debugLevel == Debug::DebugLevels::ZeroLevel) {
		ui->toolButton_OpenLogs->setEnabled(false);
		ui->toolButton_OpenLogs->setToolTip(ui->toolButton_OpenLogs->whatsThis() + tr(" (enable logs first and restart the program)"));
	}

	switch (Settings::getGrabberType())
	{
#ifdef WGC_GRAB_SUPPORT
	case Grab::GrabberTypeWgc:
		ui->radioButton_GrabWgc->setChecked(true);
		break;
#endif
#ifdef WINAPI_GRAB_SUPPORT
	case Grab::GrabberTypeWinAPI:
		ui->radioButton_GrabWinAPI->setChecked(true);
		break;
#endif
#ifdef DDUPL_GRAB_SUPPORT
	case Grab::GrabberTypeDDupl:
		ui->radioButton_GrabDDupl->setChecked(true);
		break;
#endif
#ifdef X11_GRAB_SUPPORT
	case Grab::GrabberTypeX11:
		ui->radioButton_GrabX11->setChecked(true);
		break;
#endif
#ifdef MAC_OS_AV_GRAB_SUPPORT
	case Grab::GrabberTypeMacAVFoundation:
		ui->radioButton_GrabMacAVFoundation->setChecked(true);
		break;
#endif
#ifdef MAC_OS_CG_GRAB_SUPPORT
	case Grab::GrabberTypeMacCoreGraphics:
		ui->radioButton_GrabMacCoreGraphics->setChecked(true);
		break;
#endif
	default:
		qWarning() << Q_FUNC_INFO << "unsupported grabber in settings: " << Settings::getGrabberType();
		break;
	}

#ifdef D3D10_GRAB_SUPPORT
	ui->checkBox_EnableDx1011Capture->setChecked(Settings::isDx1011GrabberEnabled());
	ui->checkBox_EnableDx9Capture->setChecked(Settings::isDx9GrabbingEnabled());
#endif

	onMoodLampLiquidMode_Toggled(ui->radioButton_LiquidColorMoodLampMode->isChecked());
	updateDeviceTabWidgetsVisibility();
	onGrabberChanged();
	settingsProfileChanged_UpdateUI(Settings::getCurrentProfileName());
	updatingFromSettings = false;
}

void SettingsWindow::updateGrabColorModeUi()
{
	const int mode = ui->comboBox_GrabColorMode->currentData().toInt();
	QString description;

	switch (mode) {
	case Grab::Calculations::ColorProcessingModeLegacy:
		description = tr("Classic averaging. Bright scenes can flood the wall more easily.");
		break;
	case Grab::Calculations::ColorProcessingModeAccurate:
		description = tr("Keeps left/right/top separation stronger and restrains bright warm fills.");
		break;
	case Grab::Calculations::ColorProcessingModeCinema:
		description = tr("More vivid and punchy, but still softly clamps white and yellow peaks.");
		break;
	case Grab::Calculations::ColorProcessingModeBalanced:
	default:
		description = tr("Recommended. Preserves normal colors, gently clamps bright peaks and avoids wall flood.");
		break;
	}

	ui->comboBox_GrabColorMode->setToolTip(description);
	ui->label_GrabColorMode->setToolTip(description);
}

void SettingsWindow::updateScenePresetUi()
{
	const int preset = m_comboScenePreset->currentData().toInt();
	QString description;

	switch (preset) {
	case Grab::Calculations::ScenePresetAnime:
		description = tr("Pushes clean saturated colors, keeps outlines vivid and avoids yellow-white wall flood.");
		break;
	case Grab::Calculations::ScenePresetGames:
		description = tr("Keeps contrast and shadow separation stronger for HUD, dark rooms and effects.");
		break;
	case Grab::Calculations::ScenePresetCinema:
		description = tr("More restrained and film-like. Warm highlights stay soft instead of turning into a projector.");
		break;
	case Grab::Calculations::ScenePresetNeutral:
		description = tr("Neutral tuning without scene guessing. Good if you want one stable behavior everywhere.");
		break;
	case Grab::Calculations::ScenePresetAuto:
	default:
		description = tr("Automatically switches between Anime, Games and Cinema by scene brightness, contrast and saturation.");
		break;
	}

	if (m_checkSmartCalibration->isChecked()) {
		description += tr(" Smart calibration is ON: bright white/yellow fills are softly clamped and dark scenes keep cleaner separation.");
	} else {
		description += tr(" Smart calibration is OFF.");
	}

	m_comboScenePreset->setToolTip(description);
	m_labelScenePreset->setToolTip(description);
	m_checkSmartCalibration->setToolTip(description);
}

Grab::GrabberType SettingsWindow::getSelectedGrabberType()
{
#ifdef X11_GRAB_SUPPORT
	if (ui->radioButton_GrabX11->isChecked()) {
		return Grab::GrabberTypeX11;
	}
#endif
#ifdef WGC_GRAB_SUPPORT
	if (ui->radioButton_GrabWgc->isChecked()) {
		return Grab::GrabberTypeWgc;
	}
#endif
#ifdef WINAPI_GRAB_SUPPORT
	if (ui->radioButton_GrabWinAPI->isChecked()) {
		return Grab::GrabberTypeWinAPI;
	}
#endif
#ifdef DDUPL_GRAB_SUPPORT
	if (ui->radioButton_GrabDDupl->isChecked()) {
		return Grab::GrabberTypeDDupl;
	}
#endif
#ifdef MAC_OS_AV_GRAB_SUPPORT
	if (ui->radioButton_GrabMacAVFoundation->isChecked()) {
		return Grab::GrabberTypeMacAVFoundation;
	}
#endif
#ifdef MAC_OS_CG_GRAB_SUPPORT
	if (ui->radioButton_GrabMacCoreGraphics->isChecked()) {
		return Grab::GrabberTypeMacCoreGraphics;
	}
#endif

	return Grab::GrabberTypeQt;
}

bool SettingsWindow::isDx1011CaptureEnabled() {
	return ui->checkBox_EnableDx1011Capture->isChecked();
}

// ----------------------------------------------------------------------------
// Quit application
// ----------------------------------------------------------------------------

void SettingsWindow::quit()
{
	if (!ui->checkBox_KeepLightsOnAfterExit->isChecked())
	{
		// Process all currently pending signals (which may include updating the color signals)
		QApplication::processEvents(QEventLoop::AllEvents, 1000);

		emit switchOffLeds();
		QApplication::processEvents(QEventLoop::AllEvents, 1000);
	}

	DEBUG_LOW_LEVEL << Q_FUNC_INFO << "trayIcon->hide();";

	if (m_trayIcon!=NULL) {
		m_trayIcon->hide();
	}

	DEBUG_LOW_LEVEL << Q_FUNC_INFO << "QApplication::quit();";

	QApplication::quit();
}

void SettingsWindow::setFirmwareVersion(const QString &firmwareVersion)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	this->fimwareVersion = firmwareVersion;
	versionsUpdate();
}

void SettingsWindow::versionsUpdate()
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	// use template to construct version string
	QString versionsTemplate = tr("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\"> <html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\"> p, li { white-space: pre-wrap; } </style></head><body style=\" font-family:'MS Shell Dlg 2'; font-size:8.25pt; font-weight:400; font-style:normal;\"> <p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">software <span style=\" font-size:8pt; font-weight:600;\">%1</span><span style=\" font-size:8pt;\"> (rev </span><a href=\"https://github.com/psieg/Lightpack/commit/%2\"><span style=\" font-size:8pt; text-decoration: underline; color:#0000ff;\">%2</span></a><span style=\" font-size:8pt;\">, Qt %4), firmware <b>%3</b></span></p></body></html>");

#ifdef GIT_REVISION
	versionsTemplate = versionsTemplate.arg(
				QApplication::applicationVersion(),
				GIT_REVISION,
				fimwareVersion,
				QT_VERSION_STR);
#else
	versionsTemplate = versionsTemplate.arg(
				QApplication::applicationVersion(),
				"unknown",
				fimwareVersion,
				QT_VERSION_STR);
	versionsTemplate.remove(QRegularExpression(" \\([^()]+unknown[^()]+\\)"));
#endif

	ui->labelVersions->setText( versionsTemplate );

	resize(1080, 820);
}

void SettingsWindow::showHelpOf(QObject *object)
{
	QCoreApplication::postEvent(object, new QHelpEvent(QEvent::WhatsThis, QPoint(0,0), QCursor::pos()));
}

#if defined(SOUNDVIZ_SUPPORT) && defined(Q_OS_MACOS)
void SettingsWindow::onSoundVizDeviceHelp_clicked()
{
	showHelpOf(ui->pushButton_SoundVizDeviceHelp);
}
#endif

void SettingsWindow::on_pushButton_LightpackSmoothnessHelp_clicked()
{
	showHelpOf(ui->horizontalSlider_DeviceSmooth);
}

void SettingsWindow::on_pushButton_LightpackColorDepthHelp_clicked()
{
	showHelpOf(ui->horizontalSlider_DeviceColorDepth);
}

void SettingsWindow::on_pushButton_LightpackRefreshDelayHelp_clicked()
{
	showHelpOf(ui->horizontalSlider_DeviceRefreshDelay);
}

void SettingsWindow::on_pushButton_GammaCorrectionHelp_clicked()
{
	showHelpOf(ui->horizontalSlider_GammaCorrection);
}

void SettingsWindow::on_pushButton_DitheringHelp_clicked()
{
	showHelpOf(ui->checkBox_EnableDithering);
}

void SettingsWindow::on_pushButton_BrightnessCapHelp_clicked()
{
	showHelpOf(ui->horizontalSlider_DeviceBrightnessCap);
}

void SettingsWindow::on_pushButton_lumosityThresholdHelp_clicked()
{
	showHelpOf(ui->horizontalSlider_LuminosityThreshold);
}

void SettingsWindow::on_pushButton_grabApplyColorTemperatureHelp_clicked()
{
	showHelpOf(ui->checkBox_GrabApplyColorTemperature);
}

void SettingsWindow::on_pushButton_grabGammaHelp_clicked()
{
	showHelpOf(ui->horizontalSlider_GrabGamma);
}

void SettingsWindow::on_pushButton_grabOverBrightenHelp_clicked()
{
	showHelpOf(ui->horizontalSlider_GrabOverBrighten);
}

void SettingsWindow::on_pushButton_AllPluginsHelp_clicked()
{
	showHelpOf(ui->label_AllPlugins);
}

bool SettingsWindow::toPriority(Plugin* s1 ,Plugin* s2 )
{
	return s1->getPriority() > s2->getPriority();
}

void SettingsWindow::updatePlugin(const QList<Plugin*>& plugins)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;


	_plugins = plugins;
	// sort priority
	std::sort(_plugins.begin(), _plugins.end(), SettingsWindow::toPriority);
	ui->list_Plugins->clear();
	foreach(Plugin* plugin, _plugins){
		int index = _plugins.indexOf(plugin);
		QListWidgetItem *item = new QListWidgetItem(getPluginName(plugin));
		item->setData(Qt::UserRole, index);
		item->setIcon(plugin->Icon());
		if (plugin->isEnabled())
		{
			item->setCheckState(Qt::Checked);
		}
		else
			item->setCheckState(Qt::Unchecked);

		ui->list_Plugins->addItem(item);
	}

	ui->pushButton_ReloadPlugins->setEnabled(true);

}

void SettingsWindow::on_list_Plugins_itemClicked(QListWidgetItem* current)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;

	bool isEnabled = true;

	if (current->checkState() == Qt::Checked)
		isEnabled = true;
	else
		isEnabled = false;

	int index =current->data(Qt::UserRole).toUInt();
	if (_plugins[index]->isEnabled() != isEnabled)
		_plugins[index]->setEnabled(isEnabled);

	pluginSwitch(index);
}

void SettingsWindow::pluginSwitch(int index)
{
	DEBUG_LOW_LEVEL << Q_FUNC_INFO << index;

	if (index == -1)
	{
		ui->label_PluginName->setText(QLatin1String(""));
		ui->label_PluginAuthor->setText(QLatin1String(""));
		ui->label_PluginVersion->setText(QLatin1String(""));
		ui->tb_PluginDescription->setText(QLatin1String(""));
		ui->label_PluginIcon->setPixmap(QIcon(QStringLiteral(":/plugin/Plugin.png")).pixmap(50,50));
		return;
	}

	ui->label_PluginName->setText(_plugins[index]->Name());
	ui->label_PluginAuthor->setText(_plugins[index]->Author());
	ui->label_PluginVersion->setText(_plugins[index]->Version());
	ui->tb_PluginDescription->setText(_plugins[index]->Description());
	ui->label_PluginIcon->setPixmap(_plugins[index]->Icon().pixmap(50,50));

}

void SettingsWindow::on_pushButton_ReloadPlugins_clicked()
{
	foreach(Plugin* plugin, _plugins){
		plugin->Stop();
	}
	ui->list_Plugins->clear();
	_plugins.clear();
	ui->pushButton_ReloadPlugins->setEnabled(false);
	emit reloadPlugins();
}

void SettingsWindow::MoveUpPlugin() {
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;
	int k= ui->list_Plugins->currentRow();
	if (k==0) return;
	int n = k-1;
	QListWidgetItem* pItem = ui->list_Plugins->takeItem(k);
	ui->list_Plugins->insertItem(n, pItem);
	ui->list_Plugins->setCurrentRow(n);
	savePriorityPlugin();

}
void SettingsWindow::MoveDownPlugin() {
	DEBUG_LOW_LEVEL << Q_FUNC_INFO;
	int k= ui->list_Plugins->currentRow();
	if (k==ui->list_Plugins->count()-1) return;
	int n = k+1;
	QListWidgetItem* pItem = ui->list_Plugins->takeItem(k);
	ui->list_Plugins->insertItem(n, pItem);
	ui->list_Plugins->setCurrentRow(n);
	savePriorityPlugin();
}

void SettingsWindow::savePriorityPlugin()
{
	int count = ui->list_Plugins->count();
	for(int index = 0; index < count; index++)
	{
		QListWidgetItem * item = ui->list_Plugins->item(index);
		int indexPlugin =item->data(Qt::UserRole).toUInt();
		_plugins[indexPlugin]->setPriority(count - index);
	}
}

QString SettingsWindow::getPluginName(const Plugin *plugin) const
{
	if (plugin->state() == QProcess::Running) {
		return plugin->Name().append(QStringLiteral(" (running)"));
	} else {
		return plugin->Name().append(QStringLiteral(" (not running)"));
	}
}

void SettingsWindow::onRunConfigurationWizard_clicked()
{
	const QStringList args(QStringLiteral("--wizard"));
	QString cmdLine(QApplication::applicationFilePath());
#ifdef Q_OS_WIN
	cmdLine.prepend('"');
	cmdLine.append('"');
#endif
	QProcess::startDetached(cmdLine, args);

	quit();
}

void SettingsWindow::onKeepLightsAfterExit_Toggled(bool isEnabled)
{
	Settings::setKeepLightsOnAfterExit(isEnabled);
}

void SettingsWindow::onKeepLightsAfterLock_Toggled(bool isEnabled)
{
	Settings::setKeepLightsOnAfterLock(isEnabled);
}

void SettingsWindow::onKeepLightsAfterSuspend_Toggled(bool isEnabled)
{
	Settings::setKeepLightsOnAfterSuspend(isEnabled);
}

void SettingsWindow::onKeepLightsAfterScreenOff_Toggled(bool isEnabled)
{
	Settings::setKeepLightsOnAfterScreenOff(isEnabled);
}

void SettingsWindow::onCheckBox_checkForUpdates_Toggled(bool isEnabled)
{
	Settings::setCheckForUpdatesEnabled(isEnabled);
	ui->checkBox_installUpdates->setEnabled(isEnabled);
}

void SettingsWindow::onCheckBox_installUpdates_Toggled(bool isEnabled)
{
	Settings::setInstallUpdatesEnabled(isEnabled);
}
