/*
 * Copyright (C) 2026 Lightscreen Rd contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <QtGlobal>

// The Windows headers come before the other Qt headers, so Qt's signals/slots/emit macros never meet the WinRT ones.
#ifdef Q_OS_WIN
    #include <qt_windows.h>
    #include <d3d11.h>
    #include <dxgi1_6.h>
    #include <roapi.h>
    #include <windows.graphics.capture.h>
    #include <windows.graphics.capture.interop.h>
    #include <windows.graphics.directx.direct3d11.interop.h>
    #include <wrl/client.h>
    #include <wrl/wrappers/corewrappers.h>
#endif

#include <tools/hdrcapture.h>

#include <QFloat16>
#include <QTransform>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
constexpr int LutSize = 16384;

// Linear light in [0, 1], in LutSize steps, to the 8-bit sRGB code value.
const std::array<uchar, LutSize> &srgbLut()
{
    static const std::array<uchar, LutSize> lut = [] {
        std::array<uchar, LutSize> table {};

        for (int i = 0; i < LutSize; ++i) {
            const double linear = double(i) / (LutSize - 1);
            const double encoded = linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
            table[i] = uchar(std::lround(qBound(0.0, encoded, 1.0) * 255));
        }

        return table;
    }();

    return lut;
}

inline uchar encode(float linear, const std::array<uchar, LutSize> &lut)
{
    if (!(linear > 0)) { // also NaN
        return 0;
    }

    if (linear >= 1) {
        return 255;
    }

    return lut[int(linear * (LutSize - 1) + 0.5f)];
}
} // namespace

QImage HdrCapture::scRgbToSrgb(const void *pixels, int width, int height, int bytesPerLine, float sdrWhite)
{
    if (!pixels || width <= 0 || height <= 0 || bytesPerLine < width * 8 || !(sdrWhite > 0)) {
        return QImage();
    }

    QImage image(width, height, QImage::Format_RGB32);

    if (image.isNull()) {
        return QImage();
    }

    const auto &lut = srgbLut();
    const float scale = 1 / sdrWhite;
    std::vector<float> row(size_t(width) * 4);

    for (int y = 0; y < height; ++y) {
        const auto *source = reinterpret_cast<const qfloat16 *>(static_cast<const uchar *>(pixels) + qsizetype(y) * bytesPerLine);
        qFloatFromFloat16(row.data(), source, qsizetype(width) * 4);

        auto *target = reinterpret_cast<QRgb *>(image.scanLine(y));

        for (int x = 0; x < width; ++x) {
            const float *p = row.data() + x * 4;
            target[x] = qRgb(encode(p[0] * scale, lut), encode(p[1] * scale, lut), encode(p[2] * scale, lut));
        }
    }

    return image;
}

#ifdef Q_OS_WIN
namespace {
using Microsoft::WRL::ComPtr;

// The "SDR content brightness" of the monitor with this GDI device name, in scRGB units (1.0 = 80 nits), or 0.
float sdrWhiteLevel(const QString &deviceName)
{
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;

    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return 0;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr)
        != ERROR_SUCCESS) {
        return 0;
    }

    for (UINT32 i = 0; i < pathCount; ++i) {
        const DISPLAYCONFIG_PATH_INFO &path = paths[i];

        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;

        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS
            || QString::fromWCharArray(source.viewGdiDeviceName) != deviceName) {
            continue;
        }

        DISPLAYCONFIG_SDR_WHITE_LEVEL white = {};
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        white.header.size = sizeof(white);
        white.header.adapterId = path.targetInfo.adapterId;
        white.header.id = path.targetInfo.id;

        if (DisplayConfigGetDeviceInfo(&white.header) != ERROR_SUCCESS || white.SDRWhiteLevel == 0) {
            return 0;
        }

        return float(white.SDRWhiteLevel) / 1000;
    }

    return 0;
}

// The DXGI output (and its adapter) of the monitor with this GDI device name, when that monitor is in HDR mode,
// with its SDR white level. False for an SDR monitor, an unknown name or any failure.
struct HdrOutput {
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput6> output;
    DXGI_OUTPUT_DESC1 description = {};
    float sdrWhite = 0;
};

bool findHdrOutput(const QString &deviceName, HdrOutput &result)
{
    ComPtr<IDXGIFactory1> factory;

    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;

    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;

        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 description;

            if (FAILED(output.As(&output6)) || FAILED(output6->GetDesc1(&description))
                || QString::fromWCharArray(description.DeviceName) != deviceName) {
                continue;
            }

            // An SDR monitor keeps the old capture, as before.
            if (description.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                return false;
            }

            const float sdrWhite = sdrWhiteLevel(deviceName);

            if (!(sdrWhite > 0)) {
                return false;
            }

            result.adapter = adapter;
            result.output = output6;
            result.description = description;
            result.sdrWhite = sdrWhite;
            return true;
        }
    }

    return false;
}

// Copies an FP16 texture to the CPU and converts it; only the top left contentSize when it is given.
QImage readTexture(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *texture, float sdrWhite,
                   QSize contentSize = QSize())
{
    D3D11_TEXTURE2D_DESC description;
    texture->GetDesc(&description);

    if (description.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) {
        return QImage();
    }

    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    description.MipLevels = 1;
    description.ArraySize = 1;

    ComPtr<ID3D11Texture2D> staging;

    if (FAILED(device->CreateTexture2D(&description, nullptr, &staging))) {
        return QImage();
    }

    context->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped;

    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return QImage();
    }

    int width = int(description.Width);
    int height = int(description.Height);

    if (contentSize.isValid()) {
        width = qMin(width, contentSize.width());
        height = qMin(height, contentSize.height());
    }

    QImage image = HdrCapture::scRgbToSrgb(mapped.pData, width, height, int(mapped.RowPitch), sdrWhite);
    context->Unmap(staging.Get(), 0);
    return image;
}

QPixmap duplicate(const HdrOutput &hdr)
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    if (FAILED(D3D11CreateDevice(hdr.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &device, nullptr, &context))) {
        return QPixmap();
    }

    const DXGI_FORMAT formats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
    ComPtr<IDXGIOutputDuplication> duplication;

    if (FAILED(hdr.output->DuplicateOutput1(device.Get(), 0, 1, formats, &duplication))) {
        return QPixmap();
    }

    // The first frame of a new duplication holds the whole desktop; one that only moved the pointer has no image.
    QImage image;

    for (int attempt = 0; attempt < 5 && image.isNull(); ++attempt) {
        DXGI_OUTDUPL_FRAME_INFO info = {};
        ComPtr<IDXGIResource> resource;
        const HRESULT result = duplication->AcquireNextFrame(500, &info, &resource);

        if (result == DXGI_ERROR_WAIT_TIMEOUT) {
            continue;
        }

        if (FAILED(result)) {
            return QPixmap();
        }

        ComPtr<ID3D11Texture2D> texture;

        if (info.LastPresentTime.QuadPart != 0 && SUCCEEDED(resource.As(&texture))) {
            image = readTexture(device.Get(), context.Get(), texture.Get(), hdr.sdrWhite);
        }

        duplication->ReleaseFrame();
    }

    if (image.isNull()) {
        return QPixmap();
    }

    // The frame is in the panel's own orientation; turn it to the desktop's.
    DXGI_OUTDUPL_DESC duplicationDescription;
    duplication->GetDesc(&duplicationDescription);

    switch (duplicationDescription.Rotation) {
    case DXGI_MODE_ROTATION_ROTATE90:
        image = image.transformed(QTransform().rotate(90));
        break;
    case DXGI_MODE_ROTATION_ROTATE180:
        image = image.transformed(QTransform().rotate(180));
        break;
    case DXGI_MODE_ROTATION_ROTATE270:
        image = image.transformed(QTransform().rotate(270));
        break;
    default:
        break;
    }

    const RECT &desktop = hdr.description.DesktopCoordinates;

    if (image.size() != QSize(desktop.right - desktop.left, desktop.bottom - desktop.top)) {
        return QPixmap();
    }

    return QPixmap::fromImage(image);
}

template <typename T>
void closeObject(const ComPtr<T> &object)
{
    ComPtr<ABI::Windows::Foundation::IClosable> closable;

    if (object && SUCCEEDED(object.As(&closable))) {
        closable->Close();
    }
}

// One frame of the window through Windows.Graphics.Capture (Windows 10 1903 and later), in FP16.
QPixmap captureWindow(HWND window, const HdrOutput &hdr)
{
    namespace Capture = ABI::Windows::Graphics::Capture;
    namespace WinRtD3D = ABI::Windows::Graphics::DirectX::Direct3D11;
    using Microsoft::WRL::Wrappers::HStringReference;

    ComPtr<IGraphicsCaptureItemInterop> interop;

    if (FAILED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem).Get(),
                                      IID_PPV_ARGS(&interop)))) {
        return QPixmap();
    }

    ComPtr<Capture::IGraphicsCaptureItem> item;

    if (FAILED(interop->CreateForWindow(window, IID_PPV_ARGS(&item)))) {
        return QPixmap();
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    if (FAILED(D3D11CreateDevice(hdr.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) {
        return QPixmap();
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IInspectable> inspectable;
    ComPtr<WinRtD3D::IDirect3DDevice> winrtDevice;

    if (FAILED(device.As(&dxgiDevice)) || FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), &inspectable))
        || FAILED(inspectable.As(&winrtDevice))) {
        return QPixmap();
    }

    ComPtr<Capture::IDirect3D11CaptureFramePoolStatics2> poolStatics;

    if (FAILED(RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool).Get(),
            IID_PPV_ARGS(&poolStatics)))) {
        return QPixmap();
    }

    ABI::Windows::Graphics::SizeInt32 size = {};
    item->get_Size(&size);

    if (size.Width <= 0 || size.Height <= 0) {
        return QPixmap();
    }

    ComPtr<Capture::IDirect3D11CaptureFramePool> pool;

    if (FAILED(poolStatics->CreateFreeThreaded(winrtDevice.Get(),
                                               ABI::Windows::Graphics::DirectX::DirectXPixelFormat_R16G16B16A16Float, 1,
                                               size, &pool))) {
        return QPixmap();
    }

    ComPtr<Capture::IGraphicsCaptureSession> session;

    if (FAILED(pool->CreateCaptureSession(item.Get(), &session))) {
        closeObject(pool);
        return QPixmap();
    }

    // No pointer in the capture (Windows 10 2004 and later), and no yellow border where Windows allows that.
    ComPtr<Capture::IGraphicsCaptureSession2> session2;

    if (SUCCEEDED(session.As(&session2))) {
        session2->put_IsCursorCaptureEnabled(false);
    }

    ComPtr<Capture::IGraphicsCaptureSession3> session3;

    if (SUCCEEDED(session.As(&session3))) {
        session3->put_IsBorderRequired(false);
    }

    QImage image;

    if (SUCCEEDED(session->StartCapture())) {
        // A free-threaded pool fills without a message loop; the first frame normally arrives within a few
        // milliseconds. A minimized window never sends one.
        for (int wait = 0; wait < 100 && image.isNull(); ++wait) {
            ComPtr<Capture::IDirect3D11CaptureFrame> frame;

            if (FAILED(pool->TryGetNextFrame(&frame))) {
                break;
            }

            if (!frame) {
                Sleep(10);
                continue;
            }

            ComPtr<WinRtD3D::IDirect3DSurface> surface;
            ComPtr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
            ComPtr<ID3D11Texture2D> texture;
            ABI::Windows::Graphics::SizeInt32 contentSize = {};
            frame->get_ContentSize(&contentSize);

            if (SUCCEEDED(frame->get_Surface(&surface)) && SUCCEEDED(surface.As(&access))
                && SUCCEEDED(access->GetInterface(IID_PPV_ARGS(&texture)))) {
                image = readTexture(device.Get(), context.Get(), texture.Get(), hdr.sdrWhite,
                                    QSize(contentSize.Width, contentSize.Height));
            }

            closeObject(frame);

            if (image.isNull()) {
                break;
            }
        }
    }

    closeObject(session);
    closeObject(pool);

    return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
}
} // namespace
#endif

QPixmap HdrCapture::grabScreen(const QString &deviceName)
{
#ifdef Q_OS_WIN
    HdrOutput hdr;
    return findHdrOutput(deviceName, hdr) ? duplicate(hdr) : QPixmap();
#else
    Q_UNUSED(deviceName)
    return QPixmap();
#endif
}

QPixmap HdrCapture::grabWindow(WId window)
{
#ifdef Q_OS_WIN
    HMONITOR monitor = MonitorFromWindow(HWND(window), MONITOR_DEFAULTTONULL);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);

    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        return QPixmap();
    }

    HdrOutput hdr;
    return findHdrOutput(QString::fromWCharArray(info.szDevice), hdr) ? captureWindow(HWND(window), hdr) : QPixmap();
#else
    Q_UNUSED(window)
    return QPixmap();
#endif
}
