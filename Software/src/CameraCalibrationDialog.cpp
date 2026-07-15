#include "CameraCalibrationDialog.hpp"

#include "Settings.hpp"

#include <QApplication>
#include <QBoxLayout>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QTemporaryFile>
#include <QTimer>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <vfw.h>
#endif

namespace {
class CalibrationTarget final : public QWidget
{
public:
	void setTarget(const QColor &color, const QString &caption)
	{
		m_color = color;
		m_caption = caption;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), m_color);
		const QColor ink = m_color.lightness() > 130 ? QColor(15, 18, 25) : QColor(240, 245, 255);
		QPen pen(ink);
		pen.setWidth(10);
		painter.setPen(pen);
		painter.drawRect(rect().adjusted(18, 18, -18, -18));
		painter.setFont(QFont(QStringLiteral("Segoe UI"), 22, QFont::DemiBold));
		painter.drawText(rect().adjusted(30, 30, -30, -30), Qt::AlignCenter, m_caption);
	}

private:
	QColor m_color{Qt::black};
	QString m_caption;
};

QVector3D averageRect(const QImage &image, const QRect &rect)
{
	const QRect r = rect.intersected(image.rect());
	if (r.isEmpty())
		return {};
	double red = 0.0, green = 0.0, blue = 0.0;
	int count = 0;
	for (int y = r.top(); y <= r.bottom(); y += 3) {
		const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
		for (int x = r.left(); x <= r.right(); x += 3) {
			red += qRed(line[x]); green += qGreen(line[x]); blue += qBlue(line[x]);
			++count;
		}
	}
	return count ? QVector3D(red / count, green / count, blue / count) : QVector3D();
}
}

CameraCalibrationDialog::CameraCalibrationDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(tr("Camera color calibration"));
	setMinimumSize(760, 620);
	setModal(true);

	m_title = new QLabel(tr("Aim the camera at the monitor so that the screen and the light halo around it are visible."), this);
	m_title->setWordWrap(true);
	m_title->setObjectName(QStringLiteral("calibrationTitle"));
	m_preview = new QLabel(this);
	m_preview->setMinimumSize(640, 360);
	m_preview->setAlignment(Qt::AlignCenter);
	m_preview->setStyleSheet(QStringLiteral("background:#080b12;border:1px solid #2f75ff;border-radius:8px;"));
	m_status = new QLabel(tr("Opening camera…"), this);
	m_status->setWordWrap(true);
	m_progress = new QProgressBar(this);
	m_progress->setRange(0, 8);
	m_progress->setValue(0);
	m_start = new QPushButton(tr("Start automatic calibration"), this);
	m_keep = new QPushButton(tr("Keep result"), this);
	m_cancel = new QPushButton(tr("Cancel"), this);
	m_keep->setEnabled(false);

	auto *buttons = new QHBoxLayout;
	buttons->addWidget(m_start);
	buttons->addStretch();
	buttons->addWidget(m_cancel);
	buttons->addWidget(m_keep);
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(m_title);
	layout->addWidget(m_preview, 1);
	layout->addWidget(m_status);
	layout->addWidget(m_progress);
	layout->addLayout(buttons);

	m_previewTimer = new QTimer(this);
	m_previewTimer->setInterval(120);
	m_stepTimer = new QTimer(this);
	m_stepTimer->setSingleShot(true);
	connect(m_previewTimer, &QTimer::timeout, this, [this] { updatePreview(); });
	connect(m_stepTimer, &QTimer::timeout, this, [this] { advanceCalibration(); });
	connect(m_start, &QPushButton::clicked, this, [this] { startCalibration(); });
	connect(m_cancel, &QPushButton::clicked, this, [this] { reject(); });
	connect(m_keep, &QPushButton::clicked, this, [this] { m_resultApplied = false; accept(); });

	const int ledCount = SettingsScope::Settings::getNumberOfLeds(SettingsScope::Settings::getConnectedDevice());
	for (int i = 0; i < ledCount; ++i) {
		m_oldRed << SettingsScope::Settings::getLedCoefRed(i);
		m_oldGreen << SettingsScope::Settings::getLedCoefGreen(i);
		m_oldBlue << SettingsScope::Settings::getLedCoefBlue(i);
	}
	m_oldGamma = SettingsScope::Settings::getDeviceGamma();
	m_oldBrightnessCap = SettingsScope::Settings::getDeviceBrightnessCap();

	if (openCamera()) {
		m_status->setText(tr("Camera is ready. Keep the room lighting unchanged during measurement."));
		m_previewTimer->start();
	} else {
		m_status->setText(tr("No compatible camera was found. Close other applications using the camera and try again."));
		m_start->setEnabled(false);
	}
}

CameraCalibrationDialog::~CameraCalibrationDialog()
{
	restoreOriginalCalibration();
	closeCamera();
	delete m_target;
}

bool CameraCalibrationDialog::openCamera()
{
#ifdef Q_OS_WIN
	m_captureWindow = capCreateCaptureWindowW(L"Prismatik calibration camera", WS_CHILD,
		0, 0, 640, 360, reinterpret_cast<HWND>(m_preview->winId()), 0);
	if (!m_captureWindow)
		return false;
	for (int driver = 0; driver < 10; ++driver) {
		if (SendMessage(m_captureWindow, WM_CAP_DRIVER_CONNECT, driver, 0)) {
			SendMessage(m_captureWindow, WM_CAP_SET_SCALE, TRUE, 0);
			SendMessage(m_captureWindow, WM_CAP_SET_PREVIEWRATE, 66, 0);
			SendMessage(m_captureWindow, WM_CAP_SET_PREVIEW, TRUE, 0);
			ShowWindow(m_captureWindow, SW_SHOW);
			return true;
		}
	}
	DestroyWindow(m_captureWindow);
	m_captureWindow = nullptr;
#endif
	return false;
}

void CameraCalibrationDialog::closeCamera()
{
#ifdef Q_OS_WIN
	if (m_captureWindow) {
		SendMessage(m_captureWindow, WM_CAP_SET_PREVIEW, FALSE, 0);
		SendMessage(m_captureWindow, WM_CAP_DRIVER_DISCONNECT, 0, 0);
		DestroyWindow(m_captureWindow);
		m_captureWindow = nullptr;
	}
#endif
}

QImage CameraCalibrationDialog::captureFrame()
{
#ifdef Q_OS_WIN
	if (!m_captureWindow)
		return {};
	SendMessage(m_captureWindow, WM_CAP_GRAB_FRAME_NOSTOP, 0, 0);
	QTemporaryFile file(QDir::tempPath() + QStringLiteral("/prismatik-camera-XXXXXX.bmp"));
	file.setAutoRemove(false);
	if (!file.open())
		return {};
	const QString path = file.fileName();
	file.close();
	SendMessageW(m_captureWindow, WM_CAP_FILE_SAVEDIB, 0, reinterpret_cast<LPARAM>(path.utf16()));
	QImage image(path);
	QFile::remove(path);
	return image.convertToFormat(QImage::Format_RGB32);
#else
	return {};
#endif
}

void CameraCalibrationDialog::resizeEvent(QResizeEvent *event)
{
	QDialog::resizeEvent(event);
#ifdef Q_OS_WIN
	if (m_captureWindow)
		SetWindowPos(m_captureWindow, nullptr, 0, 0, m_preview->width(), m_preview->height(), SWP_NOZORDER);
#endif
}

void CameraCalibrationDialog::updatePreview()
{
	// Native preview is hosted directly inside the preview widget on Windows.
}

void CameraCalibrationDialog::startCalibration()
{
	// Always measure the physical strip from a neutral baseline. Reusing the
	// previous result makes every repeated calibration multiply its errors.
	for (int i = 0; i < m_oldRed.size(); ++i) {
		SettingsScope::Settings::setLedCoefRed(i, 1.0);
		SettingsScope::Settings::setLedCoefGreen(i, 1.0);
		SettingsScope::Settings::setLedCoefBlue(i, 1.0);
	}
	m_resultApplied = true; // Cancel still restores the values from before opening the wizard.
	m_start->setEnabled(false);
	m_keep->setEnabled(false);
	m_previewTimer->stop();
	m_primarySamples.clear();
	m_step = -1;
	m_target = new CalibrationTarget;
	m_target->setWindowFlag(Qt::FramelessWindowHint, true);
	m_target->setWindowFlag(Qt::WindowStaysOnTopHint, true);
	m_target->setGeometry(QGuiApplication::primaryScreen()->geometry());
	m_target->showFullScreen();
	advanceCalibration();
}

void CameraCalibrationDialog::showTarget(const QColor &color, const QString &caption)
{
	static_cast<CalibrationTarget *>(m_target)->setTarget(color, caption);
	m_target->raise();
	m_target->activateWindow();
	QApplication::processEvents();
}

void CameraCalibrationDialog::advanceCalibration()
{
	if (m_step >= 0) {
		const QImage frame = captureFrame();
		if (frame.isNull()) {
			m_status->setText(tr("The camera stopped responding. Calibration was cancelled."));
			m_target->hide();
			m_start->setEnabled(true);
			return;
		}
		if (m_step == 0) m_blackFrame = frame;
		else if (m_step == 1) m_whiteFrame = frame;
		else if (m_step >= 2) m_primarySamples << measure(frame, m_screenRect, m_blackFrame);
	}

	++m_step;
	m_progress->setValue(std::min(m_step, 8));
	if (m_step == 0) showTarget(Qt::black, tr("Measuring room light…"));
	else if (m_step == 1) showTarget(Qt::white, tr("Finding the screen…"));
	else if (m_step == 2) {
		m_screenRect = detectScreen(m_blackFrame, m_whiteFrame);
		if (m_screenRect.isEmpty()) {
			m_target->hide();
			m_status->setText(tr("The screen could not be detected. Move the camera farther away so the complete monitor and wall halo are visible."));
			m_start->setEnabled(true);
			return;
		}
		showTarget(QColor(255, 0, 0), tr("Calibrating red…"));
	} else if (m_step == 3) showTarget(QColor(0, 255, 0), tr("Calibrating green…"));
	else if (m_step == 4) showTarget(QColor(0, 0, 255), tr("Calibrating blue…"));
	else if (m_step == 5) showTarget(QColor(64, 64, 64), tr("Calibrating shadows and gamma…"));
	else if (m_step == 6) showTarget(QColor(128, 128, 128), tr("Calibrating midtones…"));
	else if (m_step == 7) showTarget(QColor(255, 255, 255), tr("Final white balance…"));
	else {
		finishCalibration();
		return;
	}
	m_stepTimer->start(m_step < 2 ? 1800 : 1400);
}

QRect CameraCalibrationDialog::detectScreen(const QImage &black, const QImage &white) const
{
	if (black.size() != white.size())
		return {};
	int left = white.width(), top = white.height(), right = -1, bottom = -1, hits = 0;
	for (int y = 0; y < white.height(); y += 4) {
		for (int x = 0; x < white.width(); x += 4) {
			const QColor a(black.pixel(x, y)), b(white.pixel(x, y));
			const int delta = (b.red() + b.green() + b.blue()) - (a.red() + a.green() + a.blue());
			// A high threshold rejects the softer wall halo and keeps the actual panel.
			if (delta > 300) {
				left = std::min(left, x); top = std::min(top, y);
				right = std::max(right, x); bottom = std::max(bottom, y); ++hits;
			}
		}
	}
	QRect found(QPoint(left, top), QPoint(right, bottom));
	if (hits < 250 || found.width() < white.width() / 5 || found.height() < white.height() / 5)
		return {};
	return found.intersected(white.rect());
}

CameraCalibrationDialog::Sample CameraCalibrationDialog::measure(const QImage &image, const QRect &screenRect, const QImage &black) const
{
	Sample result;
	const QRect center = screenRect.adjusted(screenRect.width() / 5, screenRect.height() / 5,
		-screenRect.width() / 5, -screenRect.height() / 5);
	result.screen = averageRect(image, center) - averageRect(black, center);

	const int bandX = std::max(8, screenRect.width() / 7);
	const int bandY = std::max(8, screenRect.height() / 7);
	const QList<QRect> haloRects = {
		QRect(screenRect.left(), screenRect.top() - bandY, screenRect.width(), bandY),
		QRect(screenRect.left(), screenRect.bottom() + 1, screenRect.width(), bandY),
		QRect(screenRect.left() - bandX, screenRect.top(), bandX, screenRect.height()),
		QRect(screenRect.right() + 1, screenRect.top(), bandX, screenRect.height())
	};
	QVector3D sum;
	int count = 0;
	for (const QRect &rect : haloRects) {
		const QRect valid = rect.intersected(image.rect());
		if (!valid.isEmpty()) {
			sum += averageRect(image, valid) - averageRect(black, valid);
			++count;
		}
	}
	result.halo = count ? sum / count : QVector3D();
	return result;
}

void CameraCalibrationDialog::finishCalibration()
{
	m_target->hide();
	show();
	raise();
	activateWindow();
	if (m_primarySamples.size() != 6) {
		m_status->setText(tr("Not enough measurements were captured."));
		m_start->setEnabled(true);
		return;
	}

	double efficiency[3]{};
	for (int channel = 0; channel < 3; ++channel) {
		const QVector3D &screen = m_primarySamples[channel].screen;
		const QVector3D &halo = m_primarySamples[channel].halo;
		const double screenValue = std::max(1.0, static_cast<double>(screen[channel]));
		efficiency[channel] = std::max(0.001, static_cast<double>(halo[channel]) / screenValue);
	}
	const double weakest = std::min({ efficiency[0], efficiency[1], efficiency[2] });
	double primaryScale[3] = {
		std::clamp(weakest / efficiency[0], 0.45, 1.0),
		std::clamp(weakest / efficiency[1], 0.45, 1.0),
		std::clamp(weakest / efficiency[2], 0.45, 1.0)
	};

	// Pure RGB tests reveal channel efficiency, but the final white test is the
	// authority for neutral colors. Comparing halo/screen in the same channel
	// largely cancels the webcam's own white balance.
	const Sample &white = m_primarySamples[5];
	double whiteEfficiency[3]{};
	for (int channel = 0; channel < 3; ++channel) {
		whiteEfficiency[channel] = std::max(0.001,
			static_cast<double>(white.halo[channel]) /
			std::max(1.0, static_cast<double>(white.screen[channel])));
	}
	const double weakestWhite = std::min({ whiteEfficiency[0], whiteEfficiency[1], whiteEfficiency[2] });
	double scale[3]{};
	for (int channel = 0; channel < 3; ++channel) {
		const double whiteScale = std::clamp(weakestWhite / whiteEfficiency[channel], 0.40, 1.0);
		// Final white is authoritative. Pure primaries only stabilize the estimate.
		scale[channel] = std::pow(primaryScale[channel], 0.10) * std::pow(whiteScale, 0.90);
	}

	for (int i = 0; i < m_oldRed.size(); ++i) {
		SettingsScope::Settings::setLedCoefRed(i, scale[0]);
		SettingsScope::Settings::setLedCoefGreen(i, scale[1]);
		SettingsScope::Settings::setLedCoefBlue(i, scale[2]);
	}

	auto luminance = [](const QVector3D &color) {
		return std::max(0.01, color.x() * 0.2126 + color.y() * 0.7152 + color.z() * 0.0722);
	};
	double gammaMismatchSum = 0.0;
	int gammaSamples = 0;
	for (int sampleIndex : { 3, 4 }) {
		const double screenRatio = std::clamp(luminance(m_primarySamples[sampleIndex].screen) / luminance(white.screen), 0.03, 0.95);
		const double haloRatio = std::clamp(luminance(m_primarySamples[sampleIndex].halo) / luminance(white.halo), 0.01, 0.95);
		const double mismatch = std::log(haloRatio) / std::log(screenRatio);
		if (std::isfinite(mismatch) && mismatch > 0.35 && mismatch < 3.0) {
			gammaMismatchSum += mismatch;
			++gammaSamples;
		}
	}
	double calibratedGamma = m_oldGamma;
	if (gammaSamples) {
		const double mismatch = gammaMismatchSum / gammaSamples;
		const double suggested = m_oldGamma / mismatch;
		calibratedGamma = std::clamp(suggested, std::max(0.5, m_oldGamma - 0.4), std::min(4.0, m_oldGamma + 0.4));
		SettingsScope::Settings::setDeviceGamma(calibratedGamma);
	}

	// If full white grows much faster than the camera-observed screen reference,
	// softly reduce only the peak cap. Normal colors remain bright.
	const Sample &mid = m_primarySamples[4];
	const double haloWhiteJump = luminance(white.halo) / luminance(mid.halo);
	const double screenWhiteJump = luminance(white.screen) / luminance(mid.screen);
	const double peakOvershoot = haloWhiteJump / std::max(0.1, screenWhiteJump);
	int calibratedCap = m_oldBrightnessCap;
	if (peakOvershoot > 1.12) {
		calibratedCap = std::clamp(qRound(m_oldBrightnessCap / std::sqrt(peakOvershoot)), 65, m_oldBrightnessCap);
		SettingsScope::Settings::setDeviceBrightnessCap(calibratedCap);
	}
	m_resultApplied = true;
	m_progress->setValue(8);
	m_status->setText(tr("Calibration applied: R %1%, G %2%, B %3%, gamma %4, peak cap %5%. White, midtones and bright peaks are now calibrated together. Keep it or cancel to restore everything.")
		.arg(qRound(scale[0] * 100)).arg(qRound(scale[1] * 100)).arg(qRound(scale[2] * 100))
		.arg(calibratedGamma, 0, 'f', 2).arg(calibratedCap));
	m_keep->setEnabled(true);
	m_start->setEnabled(true);
	m_previewTimer->start();
}

void CameraCalibrationDialog::restoreOriginalCalibration()
{
	if (!m_resultApplied)
		return;
	for (int i = 0; i < m_oldRed.size(); ++i) {
		SettingsScope::Settings::setLedCoefRed(i, m_oldRed[i]);
		SettingsScope::Settings::setLedCoefGreen(i, m_oldGreen[i]);
		SettingsScope::Settings::setLedCoefBlue(i, m_oldBlue[i]);
	}
	SettingsScope::Settings::setDeviceGamma(m_oldGamma);
	SettingsScope::Settings::setDeviceBrightnessCap(m_oldBrightnessCap);
	m_resultApplied = false;
}

void CameraCalibrationDialog::reject()
{
	restoreOriginalCalibration();
	QDialog::reject();
}
