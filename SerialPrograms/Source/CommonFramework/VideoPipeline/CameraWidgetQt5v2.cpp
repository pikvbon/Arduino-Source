/*  Video Widget (Qt5)
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include <QtGlobal>
#if QT_VERSION_MAJOR == 5

#include <QApplication>
#include <QVBoxLayout>
#include <QCameraInfo>
#include <QCamera>
#include <QCameraImageCapture>
#include <QCameraViewfinder>
#include <QVideoProbe>
#include "Common/Cpp/Exceptions.h"
#include "CommonFramework/Logging/LoggerQt.h"
#include "CommonFramework/GlobalSettingsPanel.h"
#include "VideoToolsQt5.h"
#include "CameraInfo.h"
#include "CameraWidgetQt5v2.h"

namespace PokemonAutomation{
namespace CameraQt5QCameraViewfinderSeparateThread{



std::vector<CameraInfo> CameraBackend::get_all_cameras() const{
    return qt5_get_all_cameras();
}
std::string CameraBackend::get_camera_name(const CameraInfo& info) const{
    return qt5_get_camera_name(info);
}
std::unique_ptr<PokemonAutomation::Camera> CameraBackend::make_camera(
    Logger& logger,
    const CameraInfo& info,
    const Resolution& desired_resolution
) const{
    return std::make_unique<CameraHolder>(logger, info, desired_resolution);
}
PokemonAutomation::VideoWidget* CameraBackend::make_video_widget(QWidget& parent, PokemonAutomation::Camera& camera) const{
    CameraHolder* casted = dynamic_cast<CameraHolder*>(&camera);
    if (casted == nullptr){
        throw InternalProgramError(nullptr, PA_CURRENT_FUNCTION, "Mismatching camera session/widget types.");
    }
    return new VideoWidget(&parent, *casted);
}
PokemonAutomation::VideoWidget* CameraBackend::make_video_widget(
    QWidget& parent,
    Logger& logger,
    const CameraInfo& info,
    const Resolution& desired_resolution
) const{
    return new VideoWidget(&parent, logger, info, desired_resolution);
}




CameraHolder::CameraHolder(
    Logger& logger,
    const CameraInfo& info, const Resolution& desired_resolution
)
    : m_logger(logger)
    , m_camera(new QCamera(QCameraInfo(info.device_name().c_str()), this))
    , m_screenshotter(logger, *m_camera)
    , m_last_orientation_attempt(WallClock::min())
    , m_stats_conversion("ConvertFrame", "ms", 1000, std::chrono::seconds(10))
{
//    m_capture = new QCameraImageCapture(m_camera, this);
//    m_capture->setCaptureDestination(QCameraImageCapture::CaptureToBuffer);
//    m_camera->setCaptureMode(QCamera::CaptureStillImage);
    m_camera->start();

    for (const auto& size : m_camera->supportedViewfinderResolutions()){
        m_supported_resolutions.emplace_back(size.width(), size.height());
    }

    QCameraViewfinderSettings settings = m_camera->viewfinderSettings();
    m_max_frame_rate = settings.maximumFrameRate();
    m_frame_period = std::chrono::milliseconds(25);
    if (0 < m_max_frame_rate){
        m_frame_period = std::chrono::milliseconds((uint64_t)(1.0 / m_max_frame_rate * 1000));
    }
    {
        QSize resolution = settings.resolution();
        m_current_resolution = Resolution(resolution.width(), resolution.height());
    }


    //  Check if we can apply the recommended resolution.
    Resolution new_resolution = m_current_resolution;
    for (const Resolution& size : m_supported_resolutions){
        if (size == desired_resolution){
            new_resolution = desired_resolution;
            break;
        }
    }
    if (m_current_resolution != new_resolution){
        settings.setResolution(QSize(new_resolution.width, new_resolution.height));
        m_camera->setViewfinderSettings(settings);
        m_current_resolution = new_resolution;
    }

    m_probe = new QVideoProbe(m_camera);
    if (!m_probe->setSource(m_camera)){
        logger.log("Unable to initialize QVideoProbe() capture.", COLOR_RED);
        delete m_probe;
        m_probe = nullptr;
    }

    if (m_probe){
        connect(
            m_probe, &QVideoProbe::videoFrameProbed,
            this, [=](const QVideoFrame& frame){
//                static int c = 0;
//                cout << "asdf: " << c++ << endl;
//                std::terminate();
                WallClock now = current_time();
                SpinLockGuard lg(m_frame_lock);
                if (GlobalSettings::instance().ENABLE_FRAME_SCREENSHOTS){
                    m_last_frame = frame;
                }
                m_last_frame_timestamp = now;
                m_last_frame_seqnum++;
//                frame_to_image(m_logger, frame, true);
            },
            Qt::DirectConnection
        );
    }
    connect(
        this, &CameraHolder::stop,
        this, [=]{
            this->moveToThread(QApplication::instance()->thread());

            m_camera->setViewfinder((QVideoWidget*)nullptr);
            m_camera->stop();

            std::lock_guard<std::mutex> lg(m_state_lock);
            m_stopped = true;
            m_cv.notify_all();
        }
    );
}
CameraHolder::~CameraHolder(){
    //  Redispatch to the thread that owns the class.
//    m_camera->stop();
//    delete m_capture;

    emit this->stop();
    std::unique_lock<std::mutex> lg(m_state_lock);
    m_cv.wait(lg, [=]{ return m_stopped; });
}
void CameraHolder::set_resolution(const Resolution& size){
    std::unique_lock<std::mutex> lg(m_state_lock);
    QCameraViewfinderSettings settings = m_camera->viewfinderSettings();
    QSize resolution = settings.resolution();
    if (Resolution(resolution.width(), resolution.height()) == size){
        return;
    }
    settings.setResolution(QSize(size.width, size.height));
    m_camera->setViewfinderSettings(settings);
    m_current_resolution = size;
}
QImage CameraHolder::direct_snapshot_probe(bool flip_vertical){
//    std::lock_guard<std::mutex> lg(m_lock);
    if (m_camera == nullptr){
        return QImage();
    }

    QVideoFrame frame;
    {
        SpinLockGuard lg1(m_frame_lock);
        frame = m_last_frame;
    }

    return frame_to_image(m_logger, frame, flip_vertical);
}
bool CameraHolder::determine_frame_orientation(){
    //  Qt 5.12 is really shitty in that there's no way to figure out the
    //  orientation of a QVideoFrame. So here we'll try to figure it out
    //  the poor man's way. Snapshot using both QCameraImageCapture and
    //  QVideoProbe and compare them.

    //  This function cannot be called on the UI thread.
    //  This function must be called under the lock.

    std::shared_ptr<const ImageRGB32> reference = m_screenshotter.snapshot();
    QImage frame = direct_snapshot_probe(false);
    m_orientation_known = PokemonAutomation::determine_frame_orientation(m_logger, *reference, frame, m_flip_vertical);
    return m_orientation_known;
}
VideoSnapshot CameraHolder::snapshot_image(){
//    cout << "snapshot_image()" << endl;
//    std::unique_lock<std::mutex> lg(m_lock);

    auto now = current_time();

    //  Frame is already cached and is not stale.
    uint64_t frame_seqnum;
    {
        SpinLockGuard lg1(m_frame_lock);
        frame_seqnum = m_last_frame_seqnum;
        if (m_last_snapshot){
            if (m_probe){
                //  If we have the probe enabled, we know if a new frame has been pushed.
//                cout << now - m_last_snapshot.load(std::memory_order_acquire) << endl;
                if (m_last_image_seqnum == frame_seqnum){
//                    cout << "cached 0" << endl;
                    return m_last_snapshot;
                }
            }else{
                //  Otherwise, we have to use time.
//                cout << now - m_last_snapshot.load(std::memory_order_acquire) << endl;
                if (m_last_snapshot.timestamp + m_frame_period > now){
//                    cout << "cached 1" << endl;
                    return m_last_snapshot;
                }
            }
        }
    }

    m_last_snapshot = m_screenshotter.snapshot();
    m_last_image_seqnum = frame_seqnum;

    return m_last_snapshot;
}
VideoSnapshot CameraHolder::snapshot_probe(){
//    cout << "snapshot_probe()" << endl;
//    std::lock_guard<std::mutex> lg(m_lock);

    if (m_camera == nullptr){
        return VideoSnapshot{QImage(), current_time()};
    }

    //  Frame is already cached and is not stale.
    QVideoFrame frame;
    WallClock frame_timestamp;
    uint64_t frame_seqnum;
    {
        SpinLockGuard lg0(m_frame_lock);
        frame_seqnum = m_last_frame_seqnum;
        if (m_last_snapshot && m_last_image_seqnum == frame_seqnum){
            return m_last_snapshot;
        }
        frame = m_last_frame;
        frame_timestamp = m_last_frame_timestamp;
    }

    WallClock time0 = current_time();

    m_last_snapshot = VideoSnapshot(
        frame_to_image(m_logger, frame, m_flip_vertical),
        frame_timestamp
    );
    m_last_image_seqnum = frame_seqnum;

    WallClock time1 = current_time();
    m_stats_conversion.report_data(m_logger, std::chrono::duration_cast<std::chrono::microseconds>(time1 - time0).count());

    return m_last_snapshot;
}
VideoSnapshot CameraHolder::snapshot(){
    std::unique_lock<std::mutex> lg(m_state_lock);

    //  Frame screenshots are disabled.
    if (!GlobalSettings::instance().ENABLE_FRAME_SCREENSHOTS){
        return snapshot_image();
    }

    //  QVideoFrame is enabled and ready!
    if (m_probe && m_orientation_known){
        return snapshot_probe();
    }

    //  If probing is enabled and we don't know the frame orientation, try to
    //  figure it out. But don't try too often if we fail.
    if (m_probe && !m_orientation_known){
        WallClock now = current_time();
        if (m_last_orientation_attempt + std::chrono::seconds(10) < now){
            m_orientation_known = determine_frame_orientation();
            m_last_orientation_attempt = now;
        }
    }

    if (m_orientation_known){
        return snapshot_probe();
    }else{
        return snapshot_image();
    }
}








VideoWidget::VideoWidget(QWidget* parent, CameraHolder& camera)
    : PokemonAutomation::VideoWidget(parent)
    , m_logger(camera.m_logger)
    , m_holder(&camera)
{
    m_logger.log("Constructing VideoWidget: Backend = CameraQt5QCameraViewfinderSeparateThread");
    if (camera.m_camera == nullptr){
        return;
    }

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignTop);
    layout->setContentsMargins(0, 0, 0, 0);

    m_camera_view = new QCameraViewfinder(this);
    layout->addWidget(m_camera_view);
    m_camera_view->setMinimumSize(80, 45);
    m_holder->m_camera->setViewfinder(m_camera_view);
//    m_holder->m_camera->setViewfinder((QVideoWidget*)nullptr);

    m_holder->moveToThread(&m_thread);
//    connect(&m_thread, &QThread::finished, m_holder.get(), &QObject::deleteLater);
    m_thread.start();
    GlobalSettings::instance().REALTIME_THREAD_PRIORITY0.set_on_qthread(m_thread);

    connect(
        this, &VideoWidget::internal_set_resolution,
        m_holder, &CameraHolder::set_resolution
    );
}
VideoWidget::VideoWidget(
    QWidget* parent,
    Logger& logger,
    const CameraInfo& info, const Resolution& desired_resolution
)
    : PokemonAutomation::VideoWidget(parent)
    , m_logger(logger)
{
    logger.log("Constructing VideoWidget: Backend = CameraQt5QCameraViewfinderSeparateThread");
    if (!info){
        return;
    }

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignTop);
    layout->setContentsMargins(0, 0, 0, 0);

    m_backing = std::make_unique<CameraHolder>(logger, info, desired_resolution);
    m_holder = m_backing.get();

    m_camera_view = new QCameraViewfinder(this);
    layout->addWidget(m_camera_view);
    m_camera_view->setMinimumSize(80, 45);
    m_holder->m_camera->setViewfinder(m_camera_view);
//    m_holder->m_camera->setViewfinder((QVideoWidget*)nullptr);

    m_holder->moveToThread(&m_thread);
//    connect(&m_thread, &QThread::finished, m_holder.get(), &QObject::deleteLater);
    m_thread.start();
    GlobalSettings::instance().REALTIME_THREAD_PRIORITY0.set_on_qthread(m_thread);

    connect(
        this, &VideoWidget::internal_set_resolution,
        m_holder, &CameraHolder::set_resolution
    );
}
VideoWidget::~VideoWidget(){
    m_backing.reset();
    m_thread.quit();
    m_thread.wait();
}
void VideoWidget::resizeEvent(QResizeEvent* event){
    QWidget::resizeEvent(event);
    if (m_holder == nullptr){
        return;
    }
    m_camera_view->setFixedSize(this->size());
}





}
}
#endif
