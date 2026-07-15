#include "WgcGrabber.hpp"

#ifdef WGC_GRAB_SUPPORT

#include "GrabberContext.hpp"
#include "src/debug.h"

#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.h>
#include <windows.graphics.directx.direct3d11.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include <cstring>

namespace ABI {
    namespace Windows {
        namespace Graphics {
            namespace DirectX {
                namespace Direct3D11 {
                    interface IDirect3DDevice;
                    interface IDirect3DSurface;
                }
            }
            namespace Capture {
                interface IGraphicsCaptureItem;
                interface IDirect3D11CaptureFrame;
                interface IDirect3D11CaptureFramePool;
                interface IDirect3D11CaptureFramePoolStatics2;
                interface IGraphicsCaptureSession;
                interface IGraphicsCaptureSession2;
                interface IGraphicsCaptureSession3;
                interface IGraphicsCaptureSessionStatics;
            }
        }
    }
}

struct IDirect3DDxgiInterfaceAccess : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void **p) = 0;
};

static const IID IID_IDirect3DDxgiInterfaceAccess =
{ 0xA9B3D012, 0x3DF2, 0x4EE3, { 0xB8, 0xD1, 0x86, 0x95, 0xF4, 0x57, 0xD3, 0xC1 } };

typedef HRESULT (WINAPI *CreateDirect3D11DeviceFromDXGIDeviceFunc)(IDXGIDevice *dxgiDevice, IInspectable **graphicsDevice);

namespace {
using ABI::Windows::Graphics::Capture::IDirect3D11CaptureFrame;
using ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool;
using ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePoolStatics2;
using ABI::Windows::Graphics::Capture::IGraphicsCaptureItem;
using ABI::Windows::Graphics::Capture::IGraphicsCaptureSession;
using ABI::Windows::Graphics::Capture::IGraphicsCaptureSession2;
using ABI::Windows::Graphics::Capture::IGraphicsCaptureSession3;
using ABI::Windows::Graphics::Capture::IGraphicsCaptureSessionStatics;
using ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface;

template<typename T>
void safeRelease(T*& ptr)
{
	if (ptr) {
		ptr->Release();
		ptr = nullptr;
	}
}

HRESULT createReference(PCWSTR runtimeClassName, HSTRING_HEADER &header, HSTRING &result)
{
	return WindowsCreateStringReference(runtimeClassName, static_cast<UINT32>(wcslen(runtimeClassName)), &header, &result);
}

template<typename T>
HRESULT getActivationFactory(PCWSTR runtimeClassName, T **factory)
{
	HSTRING_HEADER header;
	HSTRING className;
	HRESULT hr = createReference(runtimeClassName, header, className);
	if (FAILED(hr))
		return hr;
	return RoGetActivationFactory(className, __uuidof(T), reinterpret_cast<void **>(factory));
}

BufferFormat mapFormat(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		// DXGI names channels by significance, while BufferFormat names the
		// packed 32-bit pixel. In little-endian memory this is B, G, R, A,
		// which Prismatik calls ARGB (same mapping as DDuplGrabber).
		return BufferFormatArgb;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UINT:
		return BufferFormatAbgr;
	default:
		return BufferFormatUnknown;
	}
}

struct WgcScreenData
{
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	IDirect3DDevice *winrtDevice = nullptr;
	IGraphicsCaptureItem *item = nullptr;
	IDirect3D11CaptureFramePool *framePool = nullptr;
	IGraphicsCaptureSession *session = nullptr;
	ID3D11Texture2D *stagingTexture = nullptr;
	std::vector<unsigned char> cpuBuffer;
	BufferFormat bufferFormat = BufferFormatUnknown;

	~WgcScreenData()
	{
		safeRelease(stagingTexture);
		safeRelease(session);
		safeRelease(framePool);
		safeRelease(item);
		safeRelease(winrtDevice);
		safeRelease(context);
		safeRelease(device);
	}
};

bool anyWidgetOnThisMonitor(HMONITOR monitor, const QList<GrabWidget *> &grabWidgets)
{
	for (const GrabWidget *widget : grabWidgets) {
		HMONITOR widgetMonitor = MonitorFromWindow(reinterpret_cast<HWND>(widget->winId()), MONITOR_DEFAULTTONULL);
		if (widgetMonitor == monitor)
			return true;
	}
	return false;
}
}

WgcGrabber::WgcGrabber(QObject * parent, GrabberContext *context)
	: GrabberBase(parent, context)
	, m_isInitialized(false)
	, m_isSupported(false)
	, m_d3d11Dll(nullptr)
{
	m_isInitialized = initWinRt();
}

WgcGrabber::~WgcGrabber()
{
	freeScreens();
	if (m_d3d11Dll) {
		FreeLibrary(m_d3d11Dll);
		m_d3d11Dll = nullptr;
	}
	if (m_isInitialized)
		RoUninitialize();
}

bool WgcGrabber::initWinRt()
{
	HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
		qWarning() << Q_FUNC_INFO << "RoInitialize failed" << QString::number(hr, 16);
		return false;
	}

	IGraphicsCaptureSessionStatics *sessionStatics = nullptr;
	hr = getActivationFactory(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureSession, &sessionStatics);
	if (FAILED(hr)) {
		qWarning() << Q_FUNC_INFO << "GraphicsCaptureSession activation failed" << QString::number(hr, 16);
		return false;
	}

	boolean supported = false;
	hr = sessionStatics->IsSupported(&supported);
	sessionStatics->Release();
	if (FAILED(hr)) {
		qWarning() << Q_FUNC_INFO << "GraphicsCaptureSession::IsSupported failed" << QString::number(hr, 16);
		return false;
	}

	m_isSupported = supported;
	m_d3d11Dll = LoadLibraryW(L"d3d11.dll");
	return m_isSupported && m_d3d11Dll != nullptr;
}

bool WgcGrabber::isSupported() const
{
	return m_isInitialized && m_isSupported;
}

void WgcGrabber::freeScreens()
{
	for (GrabbedScreen &screen : _screensWithWidgets) {
		if (screen.associatedData) {
			delete reinterpret_cast<WgcScreenData *>(screen.associatedData);
			screen.associatedData = nullptr;
		}
		screen.imgData = nullptr;
		screen.imgDataSize = 0;
		screen.bytesPerRow = 0;
	}
}

QList< ScreenInfo > * WgcGrabber::screensWithWidgets(QList< ScreenInfo > * result, const QList<GrabWidget *> &grabWidgets)
{
	result->clear();
	for (int i = 0; i < grabWidgets.count(); ++i) {
		HMONITOR monitor = MonitorFromWindow(reinterpret_cast<HWND>(grabWidgets[i]->winId()), MONITOR_DEFAULTTONULL);
		if (monitor == nullptr)
			continue;

		bool alreadyPresent = false;
		for (const ScreenInfo &screenInfo : *result) {
			if (screenInfo.handle == monitor) {
				alreadyPresent = true;
				break;
			}
		}
		if (alreadyPresent)
			continue;

		MONITORINFOEX monitorInfo;
		std::memset(&monitorInfo, 0, sizeof(monitorInfo));
		monitorInfo.cbSize = sizeof(monitorInfo);
		if (!GetMonitorInfo(monitor, &monitorInfo))
			continue;

		ScreenInfo screenInfo;
		screenInfo.handle = monitor;
		screenInfo.rect = QRect(
			monitorInfo.rcMonitor.left,
			monitorInfo.rcMonitor.top,
			monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
			monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top);
		result->append(screenInfo);
	}
	return result;
}

bool WgcGrabber::isReallocationNeeded(const QList< ScreenInfo > &grabScreens) const
{
	if (GrabberBase::isReallocationNeeded(grabScreens))
		return true;

	for (const GrabbedScreen &screen : _screensWithWidgets) {
		if (!screen.associatedData)
			return true;
	}
	return false;
}

bool WgcGrabber::reallocate(const QList< ScreenInfo > &grabScreens)
{
	freeScreens();
	_screensWithWidgets.clear();

	if (!isSupported()) {
		qWarning() << Q_FUNC_INFO << "Windows Graphics Capture is not supported on this system";
		return false;
	}

	auto createWinRtDevice = reinterpret_cast<CreateDirect3D11DeviceFromDXGIDeviceFunc>(
		GetProcAddress(m_d3d11Dll, "CreateDirect3D11DeviceFromDXGIDevice"));
	if (!createWinRtDevice) {
		qWarning() << Q_FUNC_INFO << "CreateDirect3D11DeviceFromDXGIDevice not found";
		return false;
	}

	IGraphicsCaptureItemInterop *itemInterop = nullptr;
	IDirect3D11CaptureFramePoolStatics2 *framePoolStatics = nullptr;
	HRESULT hr = getActivationFactory(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem, &itemInterop);
	if (FAILED(hr))
		return false;
	hr = getActivationFactory(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool, &framePoolStatics);
	if (FAILED(hr)) {
		itemInterop->Release();
		return false;
	}

	for (const ScreenInfo &screenInfo : grabScreens) {
		WgcScreenData *data = new WgcScreenData();

		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
		hr = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			&featureLevel,
			1,
			D3D11_SDK_VERSION,
			&data->device,
			nullptr,
			&data->context);
		if (FAILED(hr)) {
			delete data;
			continue;
		}

		IDXGIDevice *dxgiDevice = nullptr;
		hr = data->device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgiDevice));
		if (FAILED(hr)) {
			delete data;
			continue;
		}

		IInspectable *inspectableDevice = nullptr;
		hr = createWinRtDevice(dxgiDevice, &inspectableDevice);
		dxgiDevice->Release();
		if (FAILED(hr)) {
			delete data;
			continue;
		}
		hr = inspectableDevice->QueryInterface(__uuidof(IDirect3DDevice), reinterpret_cast<void **>(&data->winrtDevice));
		inspectableDevice->Release();
		if (FAILED(hr)) {
			delete data;
			continue;
		}

		hr = itemInterop->CreateForMonitor(reinterpret_cast<HMONITOR>(screenInfo.handle), __uuidof(IGraphicsCaptureItem), reinterpret_cast<void **>(&data->item));
		if (FAILED(hr)) {
			delete data;
			continue;
		}

		ABI::Windows::Graphics::SizeInt32 itemSize;
		hr = data->item->get_Size(&itemSize);
		if (FAILED(hr) || itemSize.Width <= 0 || itemSize.Height <= 0) {
			delete data;
			continue;
		}

		hr = framePoolStatics->CreateFreeThreaded(
			data->winrtDevice,
			ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized,
			2,
			itemSize,
			&data->framePool);
		if (FAILED(hr)) {
			delete data;
			continue;
		}

		hr = data->framePool->CreateCaptureSession(data->item, &data->session);
		if (FAILED(hr)) {
			delete data;
			continue;
		}

		IGraphicsCaptureSession2 *session2 = nullptr;
		if (SUCCEEDED(data->session->QueryInterface(__uuidof(IGraphicsCaptureSession2), reinterpret_cast<void **>(&session2)))) {
			session2->put_IsCursorCaptureEnabled(false);
			session2->Release();
		}

		IGraphicsCaptureSession3 *session3 = nullptr;
		if (SUCCEEDED(data->session->QueryInterface(__uuidof(IGraphicsCaptureSession3), reinterpret_cast<void **>(&session3)))) {
			session3->put_IsBorderRequired(false);
			session3->Release();
		}

		hr = data->session->StartCapture();
		if (FAILED(hr)) {
			delete data;
			continue;
		}

		GrabbedScreen grabbedScreen;
		grabbedScreen.screenInfo = screenInfo;
		grabbedScreen.associatedData = data;
		grabbedScreen.imgFormat = BufferFormatArgb;
		grabbedScreen.bytesPerRow = static_cast<size_t>(itemSize.Width) * 4;
		data->cpuBuffer.resize(static_cast<size_t>(itemSize.Width) * static_cast<size_t>(itemSize.Height) * 4);
		grabbedScreen.imgData = data->cpuBuffer.data();
		grabbedScreen.imgDataSize = data->cpuBuffer.size();
		_screensWithWidgets.append(grabbedScreen);
	}

	itemInterop->Release();
	framePoolStatics->Release();
	return !_screensWithWidgets.isEmpty();
}

GrabResult WgcGrabber::grabScreens()
{
	if (_screensWithWidgets.isEmpty())
		return GrabResultError;

	bool anyFrame = false;

	for (GrabbedScreen &screen : _screensWithWidgets) {
		WgcScreenData *data = reinterpret_cast<WgcScreenData *>(screen.associatedData);
		if (!data || !data->framePool)
			continue;

		IDirect3D11CaptureFrame *frame = nullptr;
		HRESULT hr = data->framePool->TryGetNextFrame(&frame);
		if (FAILED(hr) || frame == nullptr)
			continue;

		IDirect3DSurface *surface = nullptr;
		hr = frame->get_Surface(&surface);
		if (FAILED(hr) || surface == nullptr) {
			safeRelease(frame);
			continue;
		}

		IDirect3DDxgiInterfaceAccess *access = nullptr;
		hr = surface->QueryInterface(IID_IDirect3DDxgiInterfaceAccess, reinterpret_cast<void **>(&access));
		if (FAILED(hr) || access == nullptr) {
			safeRelease(surface);
			safeRelease(frame);
			continue;
		}

		ID3D11Texture2D *sourceTexture = nullptr;
		hr = access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&sourceTexture));
		access->Release();
		safeRelease(surface);
		if (FAILED(hr) || sourceTexture == nullptr) {
			safeRelease(frame);
			continue;
		}

		D3D11_TEXTURE2D_DESC desc;
		sourceTexture->GetDesc(&desc);
		const size_t bytesPerRow = static_cast<size_t>(desc.Width) * 4;
		const size_t totalSize = bytesPerRow * desc.Height;

		if (!data->stagingTexture
			|| screen.bytesPerRow != bytesPerRow
			|| screen.imgFormat != mapFormat(desc.Format)
			|| data->cpuBuffer.size() != totalSize) {
			safeRelease(data->stagingTexture);

			D3D11_TEXTURE2D_DESC stagingDesc = desc;
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.BindFlags = 0;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			stagingDesc.MiscFlags = 0;
			stagingDesc.ArraySize = 1;
			stagingDesc.MipLevels = 1;
			hr = data->device->CreateTexture2D(&stagingDesc, nullptr, &data->stagingTexture);
			if (FAILED(hr)) {
				safeRelease(sourceTexture);
				safeRelease(frame);
				continue;
			}

			data->cpuBuffer.resize(totalSize);
			screen.imgData = data->cpuBuffer.data();
			screen.imgDataSize = totalSize;
			screen.bytesPerRow = bytesPerRow;
			screen.imgFormat = mapFormat(desc.Format);
		}

		data->context->CopyResource(data->stagingTexture, sourceTexture);
		safeRelease(sourceTexture);

		D3D11_MAPPED_SUBRESOURCE mapped;
		hr = data->context->Map(data->stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
		if (SUCCEEDED(hr)) {
			for (UINT row = 0; row < desc.Height; ++row) {
				std::memcpy(
					data->cpuBuffer.data() + static_cast<size_t>(row) * bytesPerRow,
					static_cast<const unsigned char *>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch,
					bytesPerRow);
			}
			data->context->Unmap(data->stagingTexture, 0);
			anyFrame = true;
		}

		safeRelease(frame);
	}

	return anyFrame ? GrabResultOk : GrabResultFrameNotReady;
}

#endif
