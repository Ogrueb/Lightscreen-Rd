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
    #include <dwmapi.h>
    #include <dxgi1_6.h>
    #include <roapi.h>
    #include <windows.graphics.capture.h>
    #include <windows.graphics.capture.interop.h>
    #include <windows.security.authorization.appcapabilityaccess.h>
    #include <windows.graphics.directx.direct3d11.interop.h>
    #include <wrl/client.h>
    #include <wrl/wrappers/corewrappers.h>
#endif

#include <tools/hdrcapture.h>
#include <tools/os.h>

#include <QElapsedTimer>
#include <QFloat16>
#include <QGuiApplication>
#include <QHash>
#include <QRect>
#include <QScreen>
#include <QStringList>
#include <QTransform>

#include <algorithm>
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

QString HdrCapture::describeScRgb(const void *pixels, int width, int height, int bytesPerLine)
{
    if (!pixels || width <= 0 || height <= 0 || bytesPerLine < width * 8) {
        return QStringLiteral("  (no pixels)");
    }

    QHash<quint64, int> counts;
    float maxChannel = 0;
    std::vector<float> row(size_t(width) * 4);

    for (int y = 0; y < height; ++y) {
        const auto *source = reinterpret_cast<const qfloat16 *>(static_cast<const uchar *>(pixels) + qsizetype(y) * bytesPerLine);
        qFloatFromFloat16(row.data(), source, qsizetype(width) * 4);

        for (int x = 0; x < width; ++x) {
            const float *p = row.data() + x * 4;
            maxChannel = qMax(maxChannel, qMax(p[0], qMax(p[1], p[2])));
            // The raw 16-bit values identify a colour exactly.
            const quint16 *raw = reinterpret_cast<const quint16 *>(source + x * 4);
            ++counts[quint64(raw[0]) | (quint64(raw[1]) << 16) | (quint64(raw[2]) << 32)];
        }
    }

    QList<QPair<int, quint64>> sorted;

    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        sorted.append({ it.value(), it.key() });
    }

    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

    QStringList lines;
    lines << QStringLiteral("  max channel %1; most common colours (scRGB, share of pixels):").arg(maxChannel);

    for (int i = 0; i < qMin(8, int(sorted.size())); ++i) {
        const quint64 key = sorted.at(i).second;
        float rgb[3];

        for (int c = 0; c < 3; ++c) {
            const quint16 bits = quint16(key >> (16 * c));
            qfloat16 value;
            std::memcpy(&value, &bits, sizeof(bits));
            rgb[c] = float(value);
        }

        lines << QStringLiteral("    %1 %2 %3  %4%")
                     .arg(rgb[0], 0, 'f', 4)
                     .arg(rgb[1], 0, 'f', 4)
                     .arg(rgb[2], 0, 'f', 4)
                     .arg(100.0 * sorted.at(i).first / (qint64(width) * height), 0, 'f', 1);
    }

    return lines.join(QLatin1Char('\n'));
}

float HdrCapture::matchScale(const void *window, int width, int height, int bytesPerLine, const void *desktop,
                             int desktopWidth, int desktopHeight, int desktopBytesPerLine, const QPoint &offset)
{
    if (!window || !desktop || width <= 0 || height <= 0 || desktopWidth <= 0 || desktopHeight <= 0
        || bytesPerLine < width * 8 || desktopBytesPerLine < desktopWidth * 8) {
        return 0;
    }

    // The overlap of the window, placed at offset, with the desktop, in window coordinates.
    const QRect overlap = QRect(0, 0, width, height) & QRect(-offset, QSize(desktopWidth, desktopHeight));

    if (overlap.isEmpty()) {
        return 0;
    }

    // Per pixel, desktop / window brightness, binned on a log scale (1/32 of a stop); covered or changed pixels scatter
    // across the bins, the visible part of the window piles up in one. Dark pixels are too noisy to count.
    constexpr int BinsPerStop = 32;
    constexpr int Stops = 8; // ratios from 1/256 to 256
    std::vector<int> bins(size_t(2 * Stops * BinsPerStop + 1), 0);
    std::vector<double> sums(bins.size(), 0);
    std::vector<float> windowRow(size_t(width) * 4);
    std::vector<float> desktopRow(size_t(desktopWidth) * 4);
    int compared = 0;

    for (int y = overlap.top(); y <= overlap.bottom(); y += 2) {
        qFloatFromFloat16(windowRow.data(),
                          reinterpret_cast<const qfloat16 *>(static_cast<const uchar *>(window) + qsizetype(y) * bytesPerLine),
                          qsizetype(width) * 4);
        qFloatFromFloat16(desktopRow.data(),
                          reinterpret_cast<const qfloat16 *>(static_cast<const uchar *>(desktop)
                                                             + qsizetype(y + offset.y()) * desktopBytesPerLine),
                          qsizetype(desktopWidth) * 4);

        for (int x = overlap.left(); x <= overlap.right(); x += 2) {
            const float *w = windowRow.data() + x * 4;
            const float *d = desktopRow.data() + (x + offset.x()) * 4;
            const float windowLight = w[0] + w[1] + w[2];
            const float desktopLight = d[0] + d[1] + d[2];

            if (!(windowLight > 0.15f) || !(desktopLight > 0)) {
                continue;
            }

            const double ratio = double(desktopLight) / windowLight;
            const int bin = int(std::lround(std::log2(ratio) * BinsPerStop)) + Stops * BinsPerStop;

            if (bin < 0 || bin >= int(bins.size())) {
                continue;
            }

            ++bins[size_t(bin)];
            sums[size_t(bin)] += ratio;
            ++compared;
        }
    }

    if (compared < 50) {
        return 0;
    }

    // The fullest bin with its neighbours (a ratio near a bin edge splits across two).
    int best = 0;

    for (int i = 1; i < int(bins.size()); ++i) {
        if (bins[size_t(i)] > bins[size_t(best)]) {
            best = i;
        }
    }

    int count = 0;
    double sum = 0;

    for (int i = qMax(0, best - 1); i <= qMin(int(bins.size()) - 1, best + 1); ++i) {
        count += bins[size_t(i)];
        sum += sums[size_t(i)];
    }

    // Most of what was compared has to agree, or the window is mostly covered and the result would be a guess.
    if (count * 2 < compared) {
        return 0;
    }

    return float(sum / count);
}

#ifdef Q_OS_WIN
namespace {
using Microsoft::WRL::ComPtr;

// Set while HdrCapture::report() runs: every failed call and what each capture read is noted here.
QStringList *trace = nullptr;

void note(const QString &text)
{
    if (trace) {
        trace->append(text);
    }
}

bool failedTraced(HRESULT result, const char *call, int line)
{
    if (FAILED(result)) {
        note(QStringLiteral("  failed (line %1): %2 -> 0x%3")
                 .arg(line)
                 .arg(QLatin1String(call))
                 .arg(quint32(result), 8, 16, QLatin1Char('0')));
        return true;
    }

    return false;
}

#define TRACE_FAILED(call) failedTraced((call), #call, __LINE__)

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

    if (TRACE_FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;

    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;

        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 description;

            if (TRACE_FAILED(output.As(&output6)) || TRACE_FAILED(output6->GetDesc1(&description))
                || QString::fromWCharArray(description.DeviceName) != deviceName) {
                continue;
            }

            // An SDR monitor keeps the old capture, as before.
            const float sdrWhite = sdrWhiteLevel(deviceName);
            note(QStringLiteral("  DXGI output %1: colour space %2 (12 = HDR), desktop %3,%4 %5x%6, bits %7, "
                                "SDR white level %8 (x80 nits), max luminance %9 nits")
                     .arg(deviceName)
                     .arg(int(description.ColorSpace))
                     .arg(description.DesktopCoordinates.left)
                     .arg(description.DesktopCoordinates.top)
                     .arg(description.DesktopCoordinates.right - description.DesktopCoordinates.left)
                     .arg(description.DesktopCoordinates.bottom - description.DesktopCoordinates.top)
                     .arg(description.BitsPerColor)
                     .arg(sdrWhite)
                     .arg(description.MaxLuminance));

            if (description.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                return false;
            }

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

// FP16 RGBA pixels copied off the GPU, rows width * 8 bytes apart.
struct Fp16Image {
    std::vector<qfloat16> pixels;
    int width = 0;
    int height = 0;

    bool isNull() const { return pixels.empty(); }
    int bytesPerLine() const { return width * 8; }
};

// Copies an FP16 texture to the CPU; only the top left contentSize when it is given.
Fp16Image readTexture(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *texture,
                      QSize contentSize = QSize())
{
    D3D11_TEXTURE2D_DESC description;
    texture->GetDesc(&description);

    if (description.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) {
        note(QStringLiteral("  texture format %1 is not FP16").arg(int(description.Format)));
        return Fp16Image();
    }

    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    description.MipLevels = 1;
    description.ArraySize = 1;

    ComPtr<ID3D11Texture2D> staging;

    if (TRACE_FAILED(device->CreateTexture2D(&description, nullptr, &staging))) {
        return Fp16Image();
    }

    context->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped;

    if (TRACE_FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return Fp16Image();
    }

    Fp16Image image;
    image.width = int(description.Width);
    image.height = int(description.Height);

    if (contentSize.isValid()) {
        image.width = qMin(image.width, contentSize.width());
        image.height = qMin(image.height, contentSize.height());
    }

    image.pixels.resize(size_t(image.width) * image.height * 4);

    for (int y = 0; y < image.height; ++y) {
        std::memcpy(image.pixels.data() + size_t(y) * image.width * 4,
                    static_cast<const uchar *>(mapped.pData) + size_t(y) * mapped.RowPitch, size_t(image.width) * 8);
    }

    context->Unmap(staging.Get(), 0);

    if (trace) {
        note(QStringLiteral("  texture %1x%2, used %3x%4").arg(description.Width).arg(description.Height)
                 .arg(image.width).arg(image.height));
        note(HdrCapture::describeScRgb(image.pixels.data(), image.width, image.height, image.bytesPerLine()));
    }

    return image;
}

// The output's composed desktop in FP16, in the panel's own orientation (see rotation).
Fp16Image duplicateFrame(const HdrOutput &hdr, DXGI_MODE_ROTATION *rotation)
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    if (TRACE_FAILED(D3D11CreateDevice(hdr.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                       D3D11_SDK_VERSION, &device, nullptr, &context))) {
        return Fp16Image();
    }

    const DXGI_FORMAT formats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
    ComPtr<IDXGIOutputDuplication> duplication;

    if (TRACE_FAILED(hdr.output->DuplicateOutput1(device.Get(), 0, 1, formats, &duplication))) {
        return Fp16Image();
    }

    DXGI_OUTDUPL_DESC duplicationDescription;
    duplication->GetDesc(&duplicationDescription);
    *rotation = duplicationDescription.Rotation;

    // The first frame of a new duplication holds the whole desktop; one that only moved the pointer has no image.
    Fp16Image image;

    for (int attempt = 0; attempt < 5 && image.isNull(); ++attempt) {
        DXGI_OUTDUPL_FRAME_INFO info = {};
        ComPtr<IDXGIResource> resource;
        const HRESULT result = duplication->AcquireNextFrame(500, &info, &resource);

        if (result == DXGI_ERROR_WAIT_TIMEOUT) {
            note(QStringLiteral("  AcquireNextFrame: timeout"));
            continue;
        }

        if (TRACE_FAILED(result)) {
            return Fp16Image();
        }

        ComPtr<ID3D11Texture2D> texture;
        note(QStringLiteral("  frame: LastPresentTime %1, AccumulatedFrames %2")
                 .arg(info.LastPresentTime.QuadPart)
                 .arg(info.AccumulatedFrames));

        if (info.LastPresentTime.QuadPart != 0 && SUCCEEDED(resource.As(&texture))) {
            image = readTexture(device.Get(), context.Get(), texture.Get());
        }

        duplication->ReleaseFrame();
    }

    return image;
}

QPixmap duplicate(const HdrOutput &hdr)
{
    DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY;
    const Fp16Image frame = duplicateFrame(hdr, &rotation);

    if (frame.isNull()) {
        return QPixmap();
    }

    QImage image = HdrCapture::scRgbToSrgb(frame.pixels.data(), frame.width, frame.height, frame.bytesPerLine(),
                                           hdr.sdrWhite);
    note(QStringLiteral("  divided by the SDR white level %1").arg(hdr.sdrWhite));

    // The frame is in the panel's own orientation; turn it to the desktop's.
    switch (rotation) {
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
        note(QStringLiteral("  size %1x%2 does not match the desktop").arg(image.width()).arg(image.height()));
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

// Windows 11 draws a yellow border around a window being captured unless the app has asked for borderless capture;
// asked once per run. Windows 10 has no such request (and no way to hide the border).
void requestBorderlessCapture()
{
    static bool asked = false;

    if (asked) {
        return;
    }

    asked = true;

    namespace Capture = ABI::Windows::Graphics::Capture;
    using AccessStatus = ABI::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus;
    using Microsoft::WRL::Wrappers::HStringReference;

    ComPtr<Capture::IGraphicsCaptureAccessStatics> access;

    if (TRACE_FAILED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureAccess).Get(),
                                            IID_PPV_ARGS(&access)))) {
        return;
    }

    ComPtr<ABI::Windows::Foundation::IAsyncOperation<AccessStatus>> operation;

    if (TRACE_FAILED(access->RequestAccessAsync(Capture::GraphicsCaptureAccessKind_Borderless, &operation))) {
        return;
    }

    ComPtr<ABI::Windows::Foundation::IAsyncInfo> info;

    if (TRACE_FAILED(operation.As(&info))) {
        return;
    }

    ABI::Windows::Foundation::AsyncStatus status = ABI::Windows::Foundation::AsyncStatus::Started;

    for (int wait = 0; wait < 100 && status == ABI::Windows::Foundation::AsyncStatus::Started; ++wait) {
        if (TRACE_FAILED(info->get_Status(&status))) {
            return;
        }

        if (status == ABI::Windows::Foundation::AsyncStatus::Started) {
            Sleep(10);
        }
    }

    AccessStatus result = AccessStatus::AppCapabilityAccessStatus_DeniedBySystem;

    if (status == ABI::Windows::Foundation::AsyncStatus::Completed) {
        operation->GetResults(&result);
    }

    note(QStringLiteral("  borderless capture: async status %1, access %2").arg(int(status)).arg(int(result)));
}

// One frame of the window through Windows.Graphics.Capture (Windows 10 1903 and later), in FP16. Its scale is the
// window's own: an SDR window comes with white at some level of Windows' choosing (2.0 on the owner's 400-nit
// setting), a browser with its own HDR surface at another (5.0 there), so captureWindow calibrates it.
Fp16Image windowFrame(HWND window, const HdrOutput &hdr)
{
    namespace Capture = ABI::Windows::Graphics::Capture;
    namespace WinRtD3D = ABI::Windows::Graphics::DirectX::Direct3D11;
    using Microsoft::WRL::Wrappers::HStringReference;

    requestBorderlessCapture();

    ComPtr<IGraphicsCaptureItemInterop> interop;

    if (TRACE_FAILED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem).Get(),
                                            IID_PPV_ARGS(&interop)))) {
        return Fp16Image();
    }

    ComPtr<Capture::IGraphicsCaptureItem> item;

    if (TRACE_FAILED(interop->CreateForWindow(window, IID_PPV_ARGS(&item)))) {
        return Fp16Image();
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    if (TRACE_FAILED(D3D11CreateDevice(hdr.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device,
                                       nullptr, &context))) {
        return Fp16Image();
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IInspectable> inspectable;
    ComPtr<WinRtD3D::IDirect3DDevice> winrtDevice;

    if (TRACE_FAILED(device.As(&dxgiDevice))
        || TRACE_FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), &inspectable))
        || TRACE_FAILED(inspectable.As(&winrtDevice))) {
        return Fp16Image();
    }

    ComPtr<Capture::IDirect3D11CaptureFramePoolStatics2> poolStatics;

    if (TRACE_FAILED(RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool).Get(),
            IID_PPV_ARGS(&poolStatics)))) {
        return Fp16Image();
    }

    ABI::Windows::Graphics::SizeInt32 size = {};
    item->get_Size(&size);

    if (size.Width <= 0 || size.Height <= 0) {
        note(QStringLiteral("  empty capture item"));
        return Fp16Image();
    }

    ComPtr<Capture::IDirect3D11CaptureFramePool> pool;

    if (TRACE_FAILED(poolStatics->CreateFreeThreaded(winrtDevice.Get(),
                                                     ABI::Windows::Graphics::DirectX::DirectXPixelFormat_R16G16B16A16Float,
                                                     1, size, &pool))) {
        return Fp16Image();
    }

    ComPtr<Capture::IGraphicsCaptureSession> session;

    if (TRACE_FAILED(pool->CreateCaptureSession(item.Get(), &session))) {
        closeObject(pool);
        return Fp16Image();
    }

    // No pointer in the capture (Windows 10 2004 and later), and no yellow border where Windows allows that.
    ComPtr<Capture::IGraphicsCaptureSession2> session2;

    if (SUCCEEDED(session.As(&session2))) {
        session2->put_IsCursorCaptureEnabled(false);
    }

    ComPtr<Capture::IGraphicsCaptureSession3> session3;

    if (SUCCEEDED(session.As(&session3))) {
        TRACE_FAILED(session3->put_IsBorderRequired(false));
    }

    Fp16Image image;

    if (!TRACE_FAILED(session->StartCapture())) {
        // A free-threaded pool fills without a message loop; the first frame normally arrives within a few
        // milliseconds. A minimized window never sends one.
        for (int wait = 0; wait < 100 && image.isNull(); ++wait) {
            ComPtr<Capture::IDirect3D11CaptureFrame> frame;

            if (TRACE_FAILED(pool->TryGetNextFrame(&frame))) {
                break;
            }

            if (!frame) {
                if (wait == 99) {
                    note(QStringLiteral("  no frame within a second"));
                }

                Sleep(10);
                continue;
            }

            ComPtr<WinRtD3D::IDirect3DSurface> surface;
            ComPtr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
            ComPtr<ID3D11Texture2D> texture;
            ABI::Windows::Graphics::SizeInt32 contentSize = {};
            frame->get_ContentSize(&contentSize);
            note(QStringLiteral("  frame content %1x%2").arg(contentSize.Width).arg(contentSize.Height));

            if (!TRACE_FAILED(frame->get_Surface(&surface)) && !TRACE_FAILED(surface.As(&access))
                && !TRACE_FAILED(access->GetInterface(IID_PPV_ARGS(&texture)))) {
                image = readTexture(device.Get(), context.Get(), texture.Get(),
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
    return image;
}

// The window through Windows.Graphics.Capture, brought to the desktop's brightness: the part of the window that is on
// screen is compared with the composed desktop (which has SDR white at the SDR white level, see duplicate()), and the
// frame is scaled by what that comparison finds. Null when nothing of the window can be compared (covered, off
// screen, rotated panel) or the comparison is not consistent, so the caller keeps PrintWindow.
QPixmap captureWindow(HWND window, const HdrOutput &hdr)
{
    const Fp16Image frame = windowFrame(window, hdr);

    if (frame.isNull()) {
        return QPixmap();
    }

    DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY;
    note(QStringLiteral(" Desktop duplication, for comparison:"));
    const Fp16Image desktop = duplicateFrame(hdr, &rotation);

    if (desktop.isNull() || (rotation != DXGI_MODE_ROTATION_IDENTITY && rotation != DXGI_MODE_ROTATION_UNSPECIFIED)) {
        note(QStringLiteral("  no desktop frame to compare with (rotation %1)").arg(int(rotation)));
        return QPixmap();
    }

    // Where the frame sits on this output: at the visible frame DWM reports, or at the window rectangle when the
    // frame has that rectangle's size.
    RECT bounds;
    RECT full;
    GetWindowRect(window, &full);

    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))) {
        bounds = full;
    }

    if (frame.width == full.right - full.left && frame.height == full.bottom - full.top) {
        bounds = full;
    }

    const RECT &origin = hdr.description.DesktopCoordinates;
    const QPoint offset(bounds.left - origin.left, bounds.top - origin.top);
    const float scale = HdrCapture::matchScale(frame.pixels.data(), frame.width, frame.height, frame.bytesPerLine(),
                                               desktop.pixels.data(), desktop.width, desktop.height,
                                               desktop.bytesPerLine(), offset);
    note(QStringLiteral("  window at %1,%2 on the output; desktop / window = %3")
             .arg(offset.x()).arg(offset.y()).arg(scale));

    if (!(scale > 0)) {
        return QPixmap();
    }

    const float white = hdr.sdrWhite / scale;
    note(QStringLiteral("  divided by %1").arg(white));
    const QImage image = HdrCapture::scRgbToSrgb(frame.pixels.data(), frame.width, frame.height, frame.bytesPerLine(),
                                                 white);
    return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
}
} // namespace
#endif

QString HdrCapture::deviceName(const QScreen *screen)
{
    if (!screen) {
        return QString();
    }

#ifdef Q_OS_WIN
    // QScreen::name() is the monitor's model name in Qt 6 ("VP2786-4K"), not the GDI device name DXGI uses. A screen's
    // logical top left is its native top left, so the monitor there is this screen.
    const QPoint corner = screen->geometry().topLeft();
    HMONITOR monitor = MonitorFromPoint(POINT { corner.x(), corner.y() }, MONITOR_DEFAULTTONULL);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);

    if (monitor && GetMonitorInfoW(monitor, &info)) {
        return QString::fromWCharArray(info.szDevice);
    }
#endif

    return screen->name();
}

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

QString HdrCapture::report()
{
    QStringList lines;

#ifdef Q_OS_WIN
    trace = &lines;

    for (QScreen *screen : QGuiApplication::screens()) {
        const QString device = deviceName(screen);
        lines << QStringLiteral("Screen %1 (%7), Qt geometry %2,%3 %4x%5 at %6")
                     .arg(screen->name())
                     .arg(screen->geometry().x())
                     .arg(screen->geometry().y())
                     .arg(screen->geometry().width())
                     .arg(screen->geometry().height())
                     .arg(screen->devicePixelRatio())
                     .arg(device);

        // What Qt's (GDI) grab gives, for comparison: the most common 8-bit colours.
        {
            const QImage qtGrab = screen->grabWindow(0).toImage().convertToFormat(QImage::Format_RGB32);
            QHash<QRgb, int> counts;

            for (int y = 0; y < qtGrab.height(); ++y) {
                const QRgb *line = reinterpret_cast<const QRgb *>(qtGrab.constScanLine(y));

                for (int x = 0; x < qtGrab.width(); ++x) {
                    ++counts[line[x] & 0xFFFFFF];
                }
            }

            QList<QPair<int, QRgb>> sorted;

            for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
                sorted.append({ it.value(), it.key() });
            }

            std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
            lines << QStringLiteral(" Qt grab %1x%2, most common colours (8-bit):").arg(qtGrab.width()).arg(qtGrab.height());

            for (int i = 0; i < qMin(8, int(sorted.size())); ++i) {
                const QRgb c = sorted.at(i).second;
                lines << QStringLiteral("    %1 %2 %3  %4%")
                             .arg(qRed(c))
                             .arg(qGreen(c))
                             .arg(qBlue(c))
                             .arg(100.0 * sorted.at(i).first / (qint64(qtGrab.width()) * qtGrab.height()), 0, 'f', 1);
            }
        }

        HdrOutput hdr;

        if (!findHdrOutput(device, hdr)) {
            lines << QStringLiteral("  not HDR (or not found): Qt's grab is used");
            continue;
        }

        lines << QStringLiteral(" Desktop duplication:");
        QElapsedTimer timer;
        timer.start();
        const QPixmap grab = duplicate(hdr);
        const qint64 hdrTime = timer.elapsed();
        timer.restart();
        screen->grabWindow(0);
        lines << (grab.isNull() ? QStringLiteral("  -> failed, Qt's grab is used") : QStringLiteral("  -> used"));
        lines << QStringLiteral("  time: %1 ms (Qt's grab: %2 ms)").arg(hdrTime).arg(timer.elapsed());
    }

    // Every visible Chromium browser, Explorer and Notepad window: what Windows.Graphics.Capture hands over, so a
    // browser can be compared with ordinary SDR windows.
    EnumWindows(
        [](HWND window, LPARAM) -> BOOL {
            wchar_t className[64] = {};
            GetClassNameW(window, className, 64);

            if (!IsWindowVisible(window) || IsIconic(window)
                || (QString::fromWCharArray(className) != QLatin1String("Chrome_WidgetWin_1")
                    && QString::fromWCharArray(className) != QLatin1String("CabinetWClass")
                    && QString::fromWCharArray(className) != QLatin1String("Notepad"))
                || GetWindowTextLengthW(window) == 0) {
                return TRUE;
            }

            wchar_t title[256] = {};
            GetWindowTextW(window, title, 256);
            note(QStringLiteral("Window \"%1\" (%2)").arg(QString::fromWCharArray(title).left(60), QString::fromWCharArray(className)));

            HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
            MONITORINFOEXW info = {};
            info.cbSize = sizeof(info);
            HdrOutput hdr;

            if (!monitor || !GetMonitorInfoW(monitor, &info)
                || !findHdrOutput(QString::fromWCharArray(info.szDevice), hdr)) {
                note(QStringLiteral("  not on an HDR monitor"));
                return TRUE;
            }

            note(QStringLiteral(" Windows.Graphics.Capture:"));
            QElapsedTimer timer;
            timer.start();
            const QPixmap grab = captureWindow(window, hdr);
            const qint64 hdrTime = timer.elapsed();
            QStringList *const saved = trace;
            trace = nullptr; // the whole path again, untraced
            timer.restart();
            os::grabWindow(WId(window));
            trace = saved;
            note(grab.isNull() ? QStringLiteral("  -> failed, PrintWindow is used") : QStringLiteral("  -> used"));
            note(QStringLiteral("  time: %1 ms (whole os::grabWindow, HDR path included: %2 ms)").arg(hdrTime).arg(timer.elapsed()));
            return TRUE;
        },
        0);

    trace = nullptr;
#else
    lines << QStringLiteral("HDR capture is Windows only.");
#endif

    return lines.join(QLatin1Char('\n'));
}
