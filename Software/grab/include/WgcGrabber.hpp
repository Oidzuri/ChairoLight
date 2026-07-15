#pragma once

#include <GrabberBase.hpp>

#ifdef WGC_GRAB_SUPPORT

#if !defined NOMINMAX
#define NOMINMAX
#endif

#if !defined WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

class WgcGrabber : public GrabberBase
{
	Q_OBJECT
public:
	WgcGrabber(QObject * parent, GrabberContext *context);
	virtual ~WgcGrabber();

	DECLARE_GRABBER_NAME("WgcGrabber")

protected slots:
	virtual GrabResult grabScreens();
	virtual bool reallocate(const QList< ScreenInfo > &grabScreens);
	virtual QList< ScreenInfo > * screensWithWidgets(QList< ScreenInfo > * result, const QList<GrabWidget *> &grabWidgets);
	virtual bool isReallocationNeeded(const QList< ScreenInfo > &grabScreens) const;

protected:
	bool initWinRt();
	bool isSupported() const;
	void freeScreens();

private:
	bool m_isInitialized;
	bool m_isSupported;
	HMODULE m_d3d11Dll;
};

#endif
