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
#include <tools/hdrcapture.h>

#include <QFloat16>
#include <QTransform>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
    #include <qt_windows.h>
    #include <d3d11.h>
    #include <dxgi1_6.h>
    #include <wrl/client.h>
#endif

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

QPixmap duplicate(IDXGIAdapter1 *adapter, IDXGIOutput6 *output, const RECT &desktop, float sdrWhite)
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                 &device, nullptr, &context))) {
        return QPixmap();
    }

    const DXGI_FORMAT formats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT };
    ComPtr<IDXGIOutputDuplication> duplication;

    if (FAILED(output->DuplicateOutput1(device.Get(), 0, 1, formats, &duplication))) {
        return QPixmap();
    }

    // The first frame of a new duplication holds the whole desktop; one that only moved the pointer has no image.
    ComPtr<ID3D11Texture2D> frame;

    for (int attempt = 0; attempt < 5 && !frame; ++attempt) {
        DXGI_OUTDUPL_FRAME_INFO info = {};
        ComPtr<IDXGIResource> resource;
        const HRESULT result = duplication->AcquireNextFrame(500, &info, &resource);

        if (result == DXGI_ERROR_WAIT_TIMEOUT) {
            continue;
        }

        if (FAILED(result)) {
            return QPixmap();
        }

        if (info.LastPresentTime.QuadPart != 0) {
            ComPtr<ID3D11Texture2D> texture;

            if (SUCCEEDED(resource.As(&texture))) {
                D3D11_TEXTURE2D_DESC description;
                texture->GetDesc(&description);
                description.Usage = D3D11_USAGE_STAGING;
                description.BindFlags = 0;
                description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                description.MiscFlags = 0;
                description.MipLevels = 1;
                description.ArraySize = 1;

                if (description.Format == DXGI_FORMAT_R16G16B16A16_FLOAT
                    && SUCCEEDED(device->CreateTexture2D(&description, nullptr, &frame))) {
                    context->CopyResource(frame.Get(), texture.Get());
                }
            }
        }

        duplication->ReleaseFrame();
    }

    if (!frame) {
        return QPixmap();
    }

    D3D11_TEXTURE2D_DESC description;
    frame->GetDesc(&description);

    D3D11_MAPPED_SUBRESOURCE mapped;

    if (FAILED(context->Map(frame.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return QPixmap();
    }

    QImage image = HdrCapture::scRgbToSrgb(mapped.pData, int(description.Width), int(description.Height),
                                           int(mapped.RowPitch), sdrWhite);
    context->Unmap(frame.Get(), 0);

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

    if (image.size() != QSize(desktop.right - desktop.left, desktop.bottom - desktop.top)) {
        return QPixmap();
    }

    return QPixmap::fromImage(image);
}
} // namespace
#endif

QPixmap HdrCapture::grabScreen(const QString &deviceName)
{
#ifdef Q_OS_WIN
    ComPtr<IDXGIFactory1> factory;

    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return QPixmap();
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

            // An SDR monitor keeps Qt's grab, as before.
            if (description.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                return QPixmap();
            }

            const float sdrWhite = sdrWhiteLevel(deviceName);

            if (!(sdrWhite > 0)) {
                return QPixmap();
            }

            return duplicate(adapter.Get(), output6.Get(), description.DesktopCoordinates, sdrWhite);
        }
    }

    return QPixmap();
#else
    Q_UNUSED(deviceName)
    return QPixmap();
#endif
}
