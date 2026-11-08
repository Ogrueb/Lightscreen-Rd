// Tests for tools/hdrcapture (bug 9): the scRGB to sRGB conversion behind desktop capture on a monitor in HDR mode.
// The DXGI capture itself needs an HDR display and is the owner's check (docs/windows-qa/).
#include <QtTest>

#include <cmath>
#include <vector>

#include <tools/hdrcapture.h>

namespace {
double toLinear(int code)
{
    const double c = code / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// One row of FP16 RGBA pixels, each colour channel given in scRGB units.
std::vector<qfloat16> row(const QList<QList<float>> &pixels, int padding = 0)
{
    std::vector<qfloat16> data;

    for (const auto &pixel : pixels) {
        data.push_back(qfloat16(pixel.at(0)));
        data.push_back(qfloat16(pixel.at(1)));
        data.push_back(qfloat16(pixel.at(2)));
        data.push_back(qfloat16(1.0f));
    }

    data.resize(data.size() + size_t(padding) * 4, qfloat16(9.0f));
    return data;
}

// An sRGB colour as an SDR window shows it on a monitor whose SDR white is sdrWhite in scRGB units.
QList<float> composed(QRgb colour, float sdrWhite)
{
    return { float(toLinear(qRed(colour)) * sdrWhite), float(toLinear(qGreen(colour)) * sdrWhite),
             float(toLinear(qBlue(colour)) * sdrWhite) };
}

bool near(QRgb a, QRgb b)
{
    return qAbs(qRed(a) - qRed(b)) <= 1 && qAbs(qGreen(a) - qGreen(b)) <= 1 && qAbs(qBlue(a) - qBlue(b)) <= 1;
}
}

class tst_HdrCapture : public QObject
{
    Q_OBJECT

private slots:
    void sdrWhiteBecomesWhite()
    {
        const auto data = row({ { 2.5f, 2.5f, 2.5f }, { 1.25f, 1.25f, 1.25f }, { 0, 0, 0 } });
        const QImage image = HdrCapture::scRgbToSrgb(data.data(), 3, 1, 3 * 8, 2.5f);

        QCOMPARE(image.size(), QSize(3, 1));
        QVERIFY(!image.hasAlphaChannel());
        QCOMPARE(image.pixel(0, 0), qRgb(255, 255, 255));
        QCOMPARE(image.pixel(1, 0), qRgb(188, 188, 188)); // half the light of white, sRGB-encoded
        QCOMPARE(image.pixel(2, 0), qRgb(0, 0, 0));
    }

    // The colours of the owner's capture (a GitHub page in Chrome): composed at SDR white 5.0 (400 nits), they come
    // back as they are. Read with white at 1.0 they come out as in the bug report: GitHub's green button #1f883d was
    // (72, 255, 134) there, its text #1f2328 (75, 82, 93).
    void sdrColoursRoundTrip()
    {
        const QList<QRgb> colours = { qRgb(0x1f, 0x88, 0x3d), qRgb(0x1f, 0x23, 0x28), qRgb(0x09, 0x69, 0xda),
                                      qRgb(0x59, 0x63, 0x6e), qRgb(1, 2, 3), qRgb(254, 128, 64) };
        QList<QList<float>> pixels;

        for (QRgb colour : colours) {
            pixels.append(composed(colour, 5.0f));
        }

        const auto data = row(pixels);
        const QImage right = HdrCapture::scRgbToSrgb(data.data(), int(colours.size()), 1, int(colours.size()) * 8, 5.0f);

        for (int i = 0; i < colours.size(); ++i) {
            QVERIFY2(near(right.pixel(i, 0), colours.at(i)), qPrintable(QString::number(i)));
        }

        const QImage overexposed = HdrCapture::scRgbToSrgb(data.data(), int(colours.size()), 1, int(colours.size()) * 8, 1.0f);
        QVERIFY(near(overexposed.pixel(0, 0), qRgb(75, 255, 133)));
        QVERIFY(near(overexposed.pixel(1, 0), qRgb(75, 83, 92)));
    }

    // HDR highlights above SDR white and colours outside sRGB (negative scRGB) are clipped, channel by channel.
    void outOfRangeClips()
    {
        const auto data = row({ { 10.0f, 2.0f, -0.5f }, { NAN, INFINITY, -INFINITY } });
        const QImage image = HdrCapture::scRgbToSrgb(data.data(), 2, 1, 2 * 8, 2.0f);

        QCOMPARE(image.pixel(0, 0), qRgb(255, 255, 0));
        QCOMPARE(image.pixel(1, 0), qRgb(0, 255, 0));
    }

    // Rows are bytesPerLine apart, as in a mapped texture, which pads them.
    void rowPitch()
    {
        std::vector<qfloat16> data = row({ { 1, 0, 0 }, { 0, 1, 0 } }, 3);
        const std::vector<qfloat16> second = row({ { 0, 0, 1 }, { 1, 1, 1 } }, 3);
        data.insert(data.end(), second.begin(), second.end());

        const QImage image = HdrCapture::scRgbToSrgb(data.data(), 2, 2, 5 * 8, 1.0f);

        QCOMPARE(image.size(), QSize(2, 2));
        QCOMPARE(image.pixel(0, 0), qRgb(255, 0, 0));
        QCOMPARE(image.pixel(1, 0), qRgb(0, 255, 0));
        QCOMPARE(image.pixel(0, 1), qRgb(0, 0, 255));
        QCOMPARE(image.pixel(1, 1), qRgb(255, 255, 255));
    }

    void invalidInput()
    {
        const auto data = row({ { 1, 1, 1 } });

        QVERIFY(HdrCapture::scRgbToSrgb(nullptr, 1, 1, 8, 1).isNull());
        QVERIFY(HdrCapture::scRgbToSrgb(data.data(), 0, 1, 8, 1).isNull());
        QVERIFY(HdrCapture::scRgbToSrgb(data.data(), 1, 1, 4, 1).isNull()); // pitch shorter than a row
        QVERIFY(HdrCapture::scRgbToSrgb(data.data(), 1, 1, 8, 0).isNull());
    }

    // Window capture calibration: a window frame with its own scale (white at 2.0) placed on a desktop where the same
    // content is 2.5 times brighter (white at 5.0), partly covered by something else.
    void matchScale()
    {
        const int width = 40;
        const int height = 30;
        std::vector<qfloat16> window(size_t(width) * height * 4);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float value = (x + y) % 3 == 0 ? 0.6f : 2.0f;
                qfloat16 *p = window.data() + (size_t(y) * width + x) * 4;
                p[0] = p[1] = p[2] = qfloat16(value);
                p[3] = qfloat16(1.0f);
            }
        }

        const int desktopWidth = 100;
        const int desktopHeight = 80;
        const QPoint offset(30, 20);
        // Behind it, other content: a gradient.
        std::vector<qfloat16> desktop(size_t(desktopWidth) * desktopHeight * 4);

        for (int y = 0; y < desktopHeight; ++y) {
            for (int x = 0; x < desktopWidth; ++x) {
                qfloat16 *d = desktop.data() + (size_t(y) * desktopWidth + x) * 4;
                d[0] = d[1] = d[2] = qfloat16(0.05f * float(x + y + 1));
                d[3] = qfloat16(1.0f);
            }
        }

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                qfloat16 *d = desktop.data() + (size_t(y + offset.y()) * desktopWidth + x + offset.x()) * 4;
                const qfloat16 *w = window.data() + (size_t(y) * width + x) * 4;
                // A third of the window is covered by another window (a different value).
                const bool covered = x < width / 3;

                for (int c = 0; c < 3; ++c) {
                    d[c] = covered ? qfloat16(4.0f) : qfloat16(float(w[c]) * 2.5f);
                }
            }
        }

        const float scale = HdrCapture::matchScale(window.data(), width, height, width * 8, desktop.data(), desktopWidth,
                                                   desktopHeight, desktopWidth * 8, offset);
        QVERIFY2(qAbs(scale - 2.5f) < 0.01f, qPrintable(QString::number(scale)));

        // The same window placed over the gradient, where nothing matches: no consistent factor.
        QCOMPARE(HdrCapture::matchScale(window.data(), width, height, width * 8, desktop.data(), desktopWidth,
                                        desktopHeight, desktopWidth * 8, QPoint(0, 0)),
                 0.0f);
        // Entirely off the desktop.
        QCOMPARE(HdrCapture::matchScale(window.data(), width, height, width * 8, desktop.data(), desktopWidth,
                                        desktopHeight, desktopWidth * 8, QPoint(200, 0)),
                 0.0f);
    }

    void describeScRgb()
    {
        const auto data = row({ { 2.5f, 2.5f, 2.5f }, { 2.5f, 2.5f, 2.5f }, { 0.5f, 0.25f, 6.0f } });
        const QString text = HdrCapture::describeScRgb(data.data(), 3, 1, 3 * 8);

        QVERIFY2(text.contains(QStringLiteral("max channel 6")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("2.5000 2.5000 2.5000  66.7%")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("0.5000 0.2500 6.0000  33.3%")), qPrintable(text));
    }

    // Prints what the HDR path sees on this machine (bug 9 diagnostics); with Chrome open on an HDR monitor, run
    // "tst_hdrcapture report" and send the output.
    void report()
    {
        const QString text = HdrCapture::report();
        QVERIFY(!text.isEmpty());
        qInfo().noquote() << "\n" << text;
    }

    // Off Windows, or on an SDR monitor, there is no HDR capture and the caller keeps Qt's grab.
    void grabScreenFallsBack()
    {
#ifndef Q_OS_WIN
        QVERIFY(HdrCapture::grabScreen(QStringLiteral(":0")).isNull());
#endif
        QVERIFY(HdrCapture::grabScreen(QStringLiteral("no such screen")).isNull());
    }
};

QTEST_MAIN(tst_HdrCapture)

#include "tst_hdrcapture.moc"
