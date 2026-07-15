#pragma once

#include <QDialog>
#include <QImage>
#include <QList>
#include <QRect>
#include <QVector3D>

class QLabel;
class QPushButton;
class QProgressBar;
class QTimer;
class QWidget;

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class CameraCalibrationDialog final : public QDialog
{
public:
	explicit CameraCalibrationDialog(QWidget *parent = nullptr);
	~CameraCalibrationDialog() override;

protected:
	void resizeEvent(QResizeEvent *event) override;
	void reject() override;

private:
	struct Sample {
		QVector3D screen;
		QVector3D halo;
	};

	bool openCamera();
	void closeCamera();
	QImage captureFrame();
	void startCalibration();
	void advanceCalibration();
	void finishCalibration();
	void restoreOriginalCalibration();
	QRect detectScreen(const QImage &black, const QImage &white) const;
	Sample measure(const QImage &image, const QRect &screenRect, const QImage &black) const;
	void showTarget(const QColor &color, const QString &caption);
	void updatePreview();

	QLabel *m_title{};
	QLabel *m_status{};
	QLabel *m_preview{};
	QProgressBar *m_progress{};
	QPushButton *m_start{};
	QPushButton *m_keep{};
	QPushButton *m_cancel{};
	QTimer *m_previewTimer{};
	QTimer *m_stepTimer{};
	QWidget *m_target{};
	int m_step{-1};
	QImage m_blackFrame;
	QImage m_whiteFrame;
	QRect m_screenRect;
	QList<Sample> m_primarySamples;
	QList<double> m_oldRed;
	QList<double> m_oldGreen;
	QList<double> m_oldBlue;
	double m_oldGamma{2.0};
	int m_oldBrightnessCap{100};
	bool m_resultApplied{false};

#ifdef Q_OS_WIN
	HWND m_captureWindow{};
#endif
};
