#include "modules/shared/usb_camera_frame_source.h"

#define NO_DSHOW_STRSAFE
#include <dshow.h>
#include <qedit.h>

#include <algorithm>
#include <cstring>

#include <QByteArray>

namespace panthera::modules {
namespace {

template <typename T>
void safeRelease(T*& pointer)
{
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

void freeMediaType(AM_MEDIA_TYPE& mediaType)
{
    if (mediaType.cbFormat != 0) {
        CoTaskMemFree(mediaType.pbFormat);
        mediaType.cbFormat = 0;
        mediaType.pbFormat = nullptr;
    }
    safeRelease(mediaType.pUnk);
}

QString hresultText(HRESULT result, const QString& fallback)
{
    return QStringLiteral("%1 (HRESULT 0x%2)")
        .arg(fallback)
        .arg(static_cast<qulonglong>(static_cast<unsigned long>(result)), 8, 16, QLatin1Char('0'));
}

QString variantBstrToString(const VARIANT& value)
{
    if (value.vt != VT_BSTR || value.bstrVal == nullptr) {
        return {};
    }

    return QString::fromWCharArray(value.bstrVal);
}

bool isMatchedDeviceName(const QString& candidate, const QString& preferredDescription)
{
    const QString trimmedPreferred = preferredDescription.trimmed();
    if (trimmedPreferred.isEmpty()) {
        return false;
    }

    return candidate.compare(trimmedPreferred, Qt::CaseInsensitive) == 0
        || candidate.contains(trimmedPreferred, Qt::CaseInsensitive);
}

const CLSID PanThera_CLSID_SampleGrabber = {
    0xc1f400a0,
    0x3f08,
    0x11d3,
    {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37}
};

const CLSID PanThera_CLSID_NullRenderer = {
    0xc1f400a4,
    0x3f08,
    0x11d3,
    {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37}
};

QImage imageFromDibBuffer(const QByteArray& buffer, int width, int signedHeight, int bitCount)
{
    const int height = std::abs(signedHeight);
    if (width <= 0 || height <= 0 || buffer.isEmpty()) {
        return {};
    }
    if (bitCount != 24 && bitCount != 32) {
        return {};
    }

    const int bytesPerPixel = bitCount / 8;
    const int sourceStride = ((width * bitCount + 31) / 32) * 4;
    const int requiredBytes = sourceStride * height;
    if (buffer.size() < requiredBytes) {
        return {};
    }

    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        return {};
    }

    const bool bottomUp = signedHeight > 0;
    for (int y = 0; y < height; ++y) {
        const int sourceY = bottomUp ? (height - 1 - y) : y;
        const uchar* source = reinterpret_cast<const uchar*>(buffer.constData()) + sourceY * sourceStride;
        uchar* destination = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const uchar* pixel = source + x * bytesPerPixel;
            destination[x * 3] = pixel[2];
            destination[x * 3 + 1] = pixel[1];
            destination[x * 3 + 2] = pixel[0];
        }
    }

    return image;
}

QImage correctCapturedFrameOrientation(const QImage& image)
{
    // The USB3 PLUS Video grabber presents the HDMI feed vertically inverted through DirectShow.
    return image.mirrored(false, true);
}

}  // namespace

struct UsbCameraFrameSource::DirectShowState {
    IGraphBuilder* graph {nullptr};
    ICaptureGraphBuilder2* captureBuilder {nullptr};
    IMediaControl* mediaControl {nullptr};
    IBaseFilter* cameraFilter {nullptr};
    IBaseFilter* sampleGrabberFilter {nullptr};
    IBaseFilter* nullRenderer {nullptr};
    ISampleGrabber* sampleGrabber {nullptr};
    int width {0};
    int height {0};
    int bitCount {0};
    bool comInitialized {false};
};

UsbCameraFrameSource::UsbCameraFrameSource(QObject* parent)
    : QObject(parent)
{
    m_frameTimer.setInterval(33);
    connect(&m_frameTimer, &QTimer::timeout, this, &UsbCameraFrameSource::pollFrame);
}

UsbCameraFrameSource::~UsbCameraFrameSource()
{
    stop();
}

bool UsbCameraFrameSource::start(const QString& preferredDescription)
{
    stop();

    m_preferredDescription = preferredDescription.trimmed().isEmpty()
        ? QStringLiteral("USB3 PLUS Video")
        : preferredDescription.trimmed();

    QString errorMessage;
    if (!initializeDirectShow(m_preferredDescription, &errorMessage)) {
        emit errorOccurred(errorMessage);
        releaseDirectShow();
        return false;
    }

    m_announcedFirstFrame = false;
    m_frameTimer.start();
    emit statusChanged(QStringLiteral("正在接入B超摄像头：%1").arg(m_activeCameraDescription));
    return true;
}

void UsbCameraFrameSource::stop()
{
    m_frameTimer.stop();
    releaseDirectShow();
    m_activeCameraDescription.clear();
    m_announcedFirstFrame = false;
}

bool UsbCameraFrameSource::isActive() const
{
    return m_state != nullptr && m_frameTimer.isActive();
}

QString UsbCameraFrameSource::activeCameraDescription() const
{
    return m_activeCameraDescription;
}

bool UsbCameraFrameSource::initializeDirectShow(const QString& preferredDescription, QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    m_state = new DirectShowState;

    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        return fail(hresultText(result, QStringLiteral("DirectShow 初始化失败")));
    }
    m_state->comInitialized = result != RPC_E_CHANGED_MODE;

    ICreateDevEnum* deviceEnumerator = nullptr;
    result = CoCreateInstance(
        CLSID_SystemDeviceEnum,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ICreateDevEnum,
        reinterpret_cast<void**>(&deviceEnumerator));
    if (FAILED(result) || deviceEnumerator == nullptr) {
        return fail(hresultText(result, QStringLiteral("无法创建视频设备枚举器")));
    }

    IEnumMoniker* monikerEnumerator = nullptr;
    result = deviceEnumerator->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &monikerEnumerator, 0);
    safeRelease(deviceEnumerator);
    if (result != S_OK || monikerEnumerator == nullptr) {
        return fail(QStringLiteral("未找到任何视频输入设备"));
    }

    IMoniker* selectedMoniker = nullptr;
    QString selectedName;
    IMoniker* firstMoniker = nullptr;
    QString firstName;

    IMoniker* moniker = nullptr;
    while (monikerEnumerator->Next(1, &moniker, nullptr) == S_OK) {
        IPropertyBag* propertyBag = nullptr;
        QString friendlyName;
        result = moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, reinterpret_cast<void**>(&propertyBag));
        if (SUCCEEDED(result) && propertyBag != nullptr) {
            VARIANT value;
            VariantInit(&value);
            if (SUCCEEDED(propertyBag->Read(L"FriendlyName", &value, nullptr))) {
                friendlyName = variantBstrToString(value);
            }
            VariantClear(&value);
        }
        safeRelease(propertyBag);

        if (firstMoniker == nullptr) {
            firstMoniker = moniker;
            firstMoniker->AddRef();
            firstName = friendlyName;
        }

        if (isMatchedDeviceName(friendlyName, preferredDescription)) {
            selectedMoniker = moniker;
            selectedMoniker->AddRef();
            selectedName = friendlyName;
            safeRelease(moniker);
            break;
        }

        safeRelease(moniker);
    }
    safeRelease(monikerEnumerator);

    if (selectedMoniker == nullptr) {
        selectedMoniker = firstMoniker;
        firstMoniker = nullptr;
        selectedName = firstName;
    } else {
        safeRelease(firstMoniker);
    }

    if (selectedMoniker == nullptr) {
        return fail(QStringLiteral("未找到可用摄像头：%1").arg(preferredDescription));
    }

    result = selectedMoniker->BindToObject(
        nullptr,
        nullptr,
        IID_IBaseFilter,
        reinterpret_cast<void**>(&m_state->cameraFilter));
    safeRelease(selectedMoniker);
    if (FAILED(result) || m_state->cameraFilter == nullptr) {
        return fail(hresultText(result, QStringLiteral("无法打开摄像头设备")));
    }

    m_activeCameraDescription = selectedName.trimmed().isEmpty()
        ? QStringLiteral("视频输入设备")
        : selectedName.trimmed();

    result = CoCreateInstance(
        CLSID_FilterGraph,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IGraphBuilder,
        reinterpret_cast<void**>(&m_state->graph));
    if (FAILED(result) || m_state->graph == nullptr) {
        return fail(hresultText(result, QStringLiteral("无法创建 DirectShow 图")));
    }

    result = CoCreateInstance(
        CLSID_CaptureGraphBuilder2,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ICaptureGraphBuilder2,
        reinterpret_cast<void**>(&m_state->captureBuilder));
    if (FAILED(result) || m_state->captureBuilder == nullptr) {
        return fail(hresultText(result, QStringLiteral("无法创建 DirectShow 采集图")));
    }

    result = m_state->captureBuilder->SetFiltergraph(m_state->graph);
    if (FAILED(result)) {
        return fail(hresultText(result, QStringLiteral("无法绑定 DirectShow 采集图")));
    }

    result = m_state->graph->AddFilter(m_state->cameraFilter, L"PanThera USB Camera");
    if (FAILED(result)) {
        return fail(hresultText(result, QStringLiteral("无法添加摄像头过滤器")));
    }

    result = CoCreateInstance(
        PanThera_CLSID_SampleGrabber,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IBaseFilter,
        reinterpret_cast<void**>(&m_state->sampleGrabberFilter));
    if (FAILED(result) || m_state->sampleGrabberFilter == nullptr) {
        return fail(hresultText(result, QStringLiteral("无法创建图像采样器")));
    }

    result = m_state->sampleGrabberFilter->QueryInterface(
        IID_ISampleGrabber,
        reinterpret_cast<void**>(&m_state->sampleGrabber));
    if (FAILED(result) || m_state->sampleGrabber == nullptr) {
        return fail(hresultText(result, QStringLiteral("无法打开图像采样器接口")));
    }

    AM_MEDIA_TYPE requestedType;
    std::memset(&requestedType, 0, sizeof(requestedType));
    requestedType.majortype = MEDIATYPE_Video;
    requestedType.subtype = MEDIASUBTYPE_RGB24;
    requestedType.formattype = FORMAT_VideoInfo;
    result = m_state->sampleGrabber->SetMediaType(&requestedType);
    if (FAILED(result)) {
        return fail(hresultText(result, QStringLiteral("无法设置摄像头输出格式")));
    }

    result = m_state->graph->AddFilter(m_state->sampleGrabberFilter, L"PanThera Frame Grabber");
    if (FAILED(result)) {
        return fail(hresultText(result, QStringLiteral("无法添加图像采样器")));
    }

    result = CoCreateInstance(
        PanThera_CLSID_NullRenderer,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IBaseFilter,
        reinterpret_cast<void**>(&m_state->nullRenderer));
    if (FAILED(result) || m_state->nullRenderer == nullptr) {
        return fail(hresultText(result, QStringLiteral("无法创建空渲染器")));
    }

    result = m_state->graph->AddFilter(m_state->nullRenderer, L"PanThera Null Renderer");
    if (FAILED(result)) {
        return fail(hresultText(result, QStringLiteral("无法添加空渲染器")));
    }

    result = m_state->captureBuilder->RenderStream(
        &PIN_CATEGORY_PREVIEW,
        &MEDIATYPE_Video,
        m_state->cameraFilter,
        m_state->sampleGrabberFilter,
        m_state->nullRenderer);
    if (FAILED(result)) {
        result = m_state->captureBuilder->RenderStream(
            &PIN_CATEGORY_CAPTURE,
            &MEDIATYPE_Video,
            m_state->cameraFilter,
            m_state->sampleGrabberFilter,
            m_state->nullRenderer);
    }
    if (FAILED(result)) {
        return fail(hresultText(result, QStringLiteral("无法连接摄像头视频流")));
    }

    AM_MEDIA_TYPE connectedType;
    std::memset(&connectedType, 0, sizeof(connectedType));
    result = m_state->sampleGrabber->GetConnectedMediaType(&connectedType);
    if (FAILED(result)) {
        return fail(hresultText(result, QStringLiteral("无法读取摄像头视频格式")));
    }
    if (connectedType.formattype != FORMAT_VideoInfo || connectedType.pbFormat == nullptr) {
        freeMediaType(connectedType);
        return fail(QStringLiteral("摄像头输出格式不是 VideoInfo"));
    }

    const auto* videoInfo = reinterpret_cast<const VIDEOINFOHEADER*>(connectedType.pbFormat);
    m_state->width = videoInfo->bmiHeader.biWidth;
    m_state->height = videoInfo->bmiHeader.biHeight;
    m_state->bitCount = videoInfo->bmiHeader.biBitCount;
    const GUID subtype = connectedType.subtype;
    freeMediaType(connectedType);

    if (m_state->width <= 0 || m_state->height == 0 || (m_state->bitCount != 24 && m_state->bitCount != 32)) {
        return fail(QStringLiteral("摄像头输出格式暂不支持：%1x%2, %3 bit")
                        .arg(m_state->width)
                        .arg(m_state->height)
                        .arg(m_state->bitCount));
    }
    if (subtype != MEDIASUBTYPE_RGB24 && subtype != MEDIASUBTYPE_RGB32) {
        return fail(QStringLiteral("摄像头输出像素格式暂不支持"));
    }

    m_state->sampleGrabber->SetOneShot(FALSE);
    m_state->sampleGrabber->SetBufferSamples(TRUE);

    result = m_state->graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&m_state->mediaControl));
    if (FAILED(result) || m_state->mediaControl == nullptr) {
        return fail(hresultText(result, QStringLiteral("无法打开视频控制接口")));
    }

    result = m_state->mediaControl->Run();
    if (FAILED(result)) {
        return fail(hresultText(result, QStringLiteral("无法启动摄像头视频流")));
    }

    return true;
}

void UsbCameraFrameSource::releaseDirectShow()
{
    if (m_state == nullptr) {
        return;
    }

    if (m_state->mediaControl != nullptr) {
        m_state->mediaControl->Stop();
    }

    safeRelease(m_state->mediaControl);
    safeRelease(m_state->sampleGrabber);
    safeRelease(m_state->nullRenderer);
    safeRelease(m_state->sampleGrabberFilter);
    safeRelease(m_state->cameraFilter);
    safeRelease(m_state->captureBuilder);
    safeRelease(m_state->graph);

    if (m_state->comInitialized) {
        CoUninitialize();
    }

    delete m_state;
    m_state = nullptr;
}

void UsbCameraFrameSource::pollFrame()
{
    if (m_state == nullptr || m_state->sampleGrabber == nullptr) {
        return;
    }

    LONG bufferSize = 0;
    HRESULT result = m_state->sampleGrabber->GetCurrentBuffer(&bufferSize, nullptr);
    if (FAILED(result) || bufferSize <= 0) {
        return;
    }

    QByteArray buffer;
    buffer.resize(bufferSize);
    result = m_state->sampleGrabber->GetCurrentBuffer(&bufferSize, reinterpret_cast<LONG*>(buffer.data()));
    if (FAILED(result) || bufferSize <= 0) {
        return;
    }
    buffer.resize(bufferSize);

    const QImage image = correctCapturedFrameOrientation(
        imageFromDibBuffer(buffer, m_state->width, m_state->height, m_state->bitCount));
    if (image.isNull()) {
        return;
    }

    emit frameAvailable(image);
    if (!m_announcedFirstFrame) {
        m_announcedFirstFrame = true;
        emit statusChanged(QStringLiteral("B超摄像头画面已接入：%1").arg(m_activeCameraDescription));
    }
}

}  // namespace panthera::modules
