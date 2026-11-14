/*
 * Camera HAL for Samsung Galaxy S Duos (GT-S7562 / kylessopen)
 * Built against stock liboemcamera.so ABI (Qualcomm MSM7x27A)
 */

#define LOG_TAG "QualcommCameraHardware"
#include <utils/Log.h>
#include <utils/Timers.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "QualcommCameraHardware.h"

extern "C" {
#include "jpeglib.h"
}

#define ALIGN4(x) (((x) + 3) & ~3)

namespace android {

/* Static state & liboemcamera.so function pointers */
static Mutex sHalLock;
static wp<QualcommCameraHardware> sSingleton;
static QualcommCameraHardware *sCurrentInstance = NULL;

static void *sLibOemCamera = NULL;
static mm_camera_info_t sCameraInfo[4];
static int sNumCameras = 0;
static bool sCameraInfoQueried = false;

static mm_camera_config_t sCamConfig;
static mm_camera_notify_t sCamNotify;
static mm_camera_ops_t sCamOps;

/* Function pointers resolved from liboemcamera.so */
static int32_t (*LINK_mm_camera_get_camera_info)(mm_camera_info_t *info, int *num_cameras) = NULL;
static int32_t (*LINK_mm_camera_init)(mm_camera_config_t *cfg, mm_camera_notify_t *notify,
                                      mm_camera_ops_t *ops, int is_v4l2) = NULL;
static int32_t (*LINK_mm_camera_exec)(void) = NULL;
static int32_t (*LINK_mm_camera_deinit)(void) = NULL;
static int32_t (*LINK_mm_camera_destroy)(void) = NULL;

static void *(*LINK_cam_frame)(void *parms) = NULL;
static void (*LINK_cam_frame_set_exit_flag)(int flag) = NULL;
static void (*LINK_wait_cam_frame_thread_ready)(void) = NULL;
static int32_t (*LINK_camframe_add_frame)(cam_frame_type_t type, struct msm_frame *frame) = NULL;
static int32_t (*LINK_camframe_release_all_frames)(cam_frame_type_t type) = NULL;
static void (*LINK_camframe_terminate)(void) = NULL;
static int32_t (*LINK_mm_camera_ext_control)(int32_t dummy, void *ext_ctrl) = NULL;

struct pcam_ext_ctrl_t {
    uint16_t mode;
    uint16_t address;
    uint16_t value_1;
    uint16_t value_2;
    uint16_t value_3;
};

static void sendExtCmd(uint16_t cmd, uint16_t v1, uint16_t v2) {
    if (!LINK_mm_camera_ext_control) return;
    pcam_ext_ctrl_t ext;
    ext.mode = cmd;
    ext.address = 0;
    ext.value_1 = v1;
    ext.value_2 = v2;
    ext.value_3 = 0;
    int32_t rc = LINK_mm_camera_ext_control(0, &ext);
    if (rc != 0) {
        LOGW("mm_camera_ext_control(cmd=%u, v1=%u) rc=%d", cmd, v1, rc);
    }
}

static bool loadOemCameraLib() {
    if (sLibOemCamera != NULL)
        return true;

    sLibOemCamera = dlopen("liboemcamera.so", RTLD_NOW);
    if (!sLibOemCamera) {
        LOGE("dlopen(liboemcamera.so) failed: %s", dlerror());
        return false;
    }

    #define LOAD_SYM(name) do { \
        *(void **)(&LINK_##name) = dlsym(sLibOemCamera, #name); \
        if (!LINK_##name) { \
            LOGE("dlsym(%s) failed: %s", #name, dlerror()); \
            dlclose(sLibOemCamera); \
            sLibOemCamera = NULL; \
            return false; \
        } \
    } while (0)

    LOAD_SYM(mm_camera_get_camera_info);
    LOAD_SYM(mm_camera_init);
    LOAD_SYM(mm_camera_exec);
    LOAD_SYM(mm_camera_deinit);
    LOAD_SYM(mm_camera_destroy);
    LOAD_SYM(cam_frame);
    LOAD_SYM(cam_frame_set_exit_flag);
    LOAD_SYM(wait_cam_frame_thread_ready);
    LOAD_SYM(camframe_add_frame);
    LOAD_SYM(camframe_release_all_frames);
    LOAD_SYM(camframe_terminate);

    #undef LOAD_SYM

    *(void **)(&LINK_mm_camera_ext_control) = dlsym(sLibOemCamera, "mm_camera_ext_control");
    LOGI("mm_camera_ext_control=%p", LINK_mm_camera_ext_control);

    LOGI("Loaded liboemcamera.so successfully (sizeof msm_frame=%u, msm_pmem_info=%u, dim=%u)",
         (unsigned)sizeof(struct msm_frame),
         (unsigned)sizeof(struct msm_pmem_info),
         (unsigned)sizeof(cam_ctrl_dimension_t));
    return true;
}

/* C entry points required by CameraService */
extern "C" int HAL_getNumberOfCameras() {
    Mutex::Autolock lock(sHalLock);
    if (!sCameraInfoQueried) {
        if (loadOemCameraLib()) {
            memset(sCameraInfo, 0, sizeof(sCameraInfo));
            sNumCameras = 0;
            int32_t rc = LINK_mm_camera_get_camera_info(sCameraInfo, &sNumCameras);
            LOGI("HAL_getNumberOfCameras: rc=%d num=%d", rc, sNumCameras);
            if (rc == 0 && sNumCameras > 0) {
                sCameraInfoQueried = true;
            }
        }
        if (!sCameraInfoQueried) {
            /* Fallback for GT-S7562 */
            sNumCameras = 2;
            sCameraInfo[0].camera_id = 0;
            sCameraInfo[0].position = 0;
            sCameraInfo[0].sensor_mount_angle = 90;
            sCameraInfo[1].camera_id = 1;
            sCameraInfo[1].position = 1;
            sCameraInfo[1].sensor_mount_angle = 270;
            sCameraInfoQueried = true;
        }
    }
    return sNumCameras;
}

extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo* cameraInfo) {
    Mutex::Autolock lock(sHalLock);
    if (!sCameraInfoQueried) {
        sHalLock.unlock();
        HAL_getNumberOfCameras();
        sHalLock.lock();
    }
    if (cameraId >= 0 && cameraId < sNumCameras) {
        cameraInfo->facing = (sCameraInfo[cameraId].position == 1)
                             ? CAMERA_FACING_FRONT : CAMERA_FACING_BACK;
        cameraInfo->orientation = sCameraInfo[cameraId].sensor_mount_angle;
    } else {
        cameraInfo->facing = CAMERA_FACING_BACK;
        cameraInfo->orientation = 90;
    }
}

extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId) {
    return QualcommCameraHardware::createInstance(cameraId);
}

sp<CameraHardwareInterface> QualcommCameraHardware::createInstance(int cameraId) {
    Mutex::Autolock lock(sHalLock);
    sp<QualcommCameraHardware> hardware = sSingleton.promote();
    if (hardware != NULL) {
        LOGW("createInstance: returning existing instance");
        return hardware;
    }

    hardware = new QualcommCameraHardware(cameraId);
    if (!hardware->startCamera()) {
        LOGE("createInstance: startCamera() failed for camera %d", cameraId);
        hardware->release();
        return NULL;
    }

    sSingleton = hardware;
    sCurrentInstance = hardware.get();
    return hardware;
}

QualcommCameraHardware::QualcommCameraHardware(int cameraId)
    : mCameraId(cameraId),
      mCameraRunning(false),
      mRecordingRunning(false),
      mPreviewInitialized(false),
      mReleased(false),
      mTakePicturePending(false),
      mAutoFocusPending(false),
      mFlashSkipFrames(0),
      mFlashPendingOff(false),
      mZoom(0),
      mZoomSrcBuf(NULL),
      mZoomSrcBufSize(0),
      mZoomTempBuf(NULL),
      mZoomTempBufSize(0),
      mPreviewWidth(640),
      mPreviewHeight(480),
      mPictureWidth(cameraId == 0 ? 2560 : 640),
      mPictureHeight(cameraId == 0 ? 1920 : 480),
      mPreviewFrameSize(0),
      mPreviewCbCrOffset(0),
      mNotifyCb(NULL),
      mDataCb(NULL),
      mDataCbTimestamp(NULL),
      mCallbackCookie(NULL),
      mMsgEnabled(0),
      mPreviewThreadRunning(false),
      mPreviewThreadExit(false),
      mCamFrameThreadRunning(false)
{
    memset(mPreviewFrames, 0, sizeof(mPreviewFrames));
    memset(mPreviewBusy, 0, sizeof(mPreviewBusy));
    memset(&mCamFrameParms, 0, sizeof(mCamFrameParms));
    memset(&mDimension, 0, sizeof(mDimension));
    initDefaultParameters();
}

QualcommCameraHardware::~QualcommCameraHardware() {
    LOGI("~QualcommCameraHardware");
    if (!mReleased) {
        release();
    }
    if (mZoomSrcBuf) {
        free(mZoomSrcBuf);
        mZoomSrcBuf = NULL;
        mZoomSrcBufSize = 0;
    }
    if (mZoomTempBuf) {
        free(mZoomTempBuf);
        mZoomTempBuf = NULL;
        mZoomTempBufSize = 0;
    }
    Mutex::Autolock lock(sHalLock);
    if (sCurrentInstance == this) {
        sCurrentInstance = NULL;
    }
}

/* liboemcamera.so callbacks */
void QualcommCameraHardware::onEventCb(mm_camera_event_t *evt) {
    if (!evt) return;
    LOGD("onEventCb: event_type=%d status=%d", evt->event_type, evt->status);
    QualcommCameraHardware *obj = sCurrentInstance;
    if (!obj) return;

    if (evt->event_type == 1 /* AF */ && obj->mNotifyCb && (obj->mMsgEnabled & CAMERA_MSG_FOCUS)) {
        obj->mNotifyCb(CAMERA_MSG_FOCUS, evt->status ? 1 : 0, 0, obj->mCallbackCookie);
    }
}

void QualcommCameraHardware::previewFrameCb(struct msm_frame *frame) {
    QualcommCameraHardware *obj = sCurrentInstance;
    if (!obj || !frame) return;

    Mutex::Autolock lock(obj->mPreviewQueueLock);
    obj->mPreviewQueue.add(frame);
    obj->mPreviewQueueCond.signal();
}

void QualcommCameraHardware::videoFrameCb(struct msm_frame *frame) {
    (void)frame;
}

void QualcommCameraHardware::camframeErrorCb(int err) {
    LOGE("camframeErrorCb: err=%d", err);
    QualcommCameraHardware *obj = sCurrentInstance;
    if (obj && obj->mNotifyCb && (obj->mMsgEnabled & CAMERA_MSG_ERROR)) {
        obj->mNotifyCb(CAMERA_MSG_ERROR, err, 0, obj->mCallbackCookie);
    }
}

void QualcommCameraHardware::camstatsCb(int type, void *hist) {
    (void)type;
    (void)hist;
}

void QualcommCameraHardware::jpegFragmentCb(uint8_t *buf, uint32_t size) {
    (void)buf;
    (void)size;
}

void QualcommCameraHardware::jpegCb(int event) {
    LOGD("jpegCb: event=%d", event);
}

void QualcommCameraHardware::liveshotCb(int event) {
    (void)event;
}

bool QualcommCameraHardware::startCamera() {
    LOGI("startCamera E (cameraId=%d)", mCameraId);

    if (!loadOemCameraLib()) {
        return false;
    }

    if (!sCameraInfoQueried) {
        memset(sCameraInfo, 0, sizeof(sCameraInfo));
        sNumCameras = 0;
        LINK_mm_camera_get_camera_info(sCameraInfo, &sNumCameras);
        sCameraInfoQueried = true;
    }

    memset(&sCamNotify, 0, sizeof(sCamNotify));
    sCamNotify.on_event = onEventCb;
    sCamNotify.video_frame_cb = videoFrameCb;
    sCamNotify.preview_frame_cb = previewFrameCb;
    sCamNotify.camframe_error_cb = camframeErrorCb;
    sCamNotify.camstats_cb = camstatsCb;
    sCamNotify.jpegfragment_cb = jpegFragmentCb;
    sCamNotify.jpeg_cb = jpegCb;
    sCamNotify.liveshot_cb = liveshotCb;

    memset(&sCamConfig, 0, sizeof(sCamConfig));
    memset(&sCamOps, 0, sizeof(sCamOps));

    int32_t rc = LINK_mm_camera_init(&sCamConfig, &sCamNotify, &sCamOps, 0);
    LOGI("mm_camera_init rc=%d", rc);
    if (rc != 0) {
        LOGE("mm_camera_init failed: %d", rc);
        return false;
    }

    uint8_t camId = (uint8_t)mCameraId;
    rc = sCamConfig.mm_camera_set_parms(CAMERA_PARM_CAMERA_ID, &camId);
    LOGI("set CAMERA_PARM_CAMERA_ID(%d) rc=%d", mCameraId, rc);

    int mode = 1; /* CAMERA_MODE_2D */
    rc = sCamConfig.mm_camera_set_parms(CAMERA_PARM_MODE, &mode);
    LOGI("set CAMERA_PARM_MODE(1) rc=%d", rc);

    rc = LINK_mm_camera_exec();
    LOGI("mm_camera_exec rc=%d", rc);
    if (rc != 0) {
        LOGE("mm_camera_exec failed: %d", rc);
        return false;
    }

    LOGI("startCamera X: success");
    return true;
}

void QualcommCameraHardware::initDefaultParameters() {
    CameraParameters p;

    p.setPreviewSize(640, 480);
    p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.setPreviewFrameRate(30);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, "640x480,352x288,320x240,176x144");
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, "yuv420sp");
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "30,15");
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(15000,30000)");
    p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "15000,30000");

    if (mCameraId == 0) {
        p.setPictureSize(2560, 1920);
        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
              "2560x1920,2048x1536,1600x1200,1280x960,640x480");
        p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "auto,infinity,macro");
        p.set(CameraParameters::KEY_FOCUS_MODE, "auto");
        p.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, "off,auto,on,torch");
        p.set(CameraParameters::KEY_FLASH_MODE, "off");
    } else {
        p.setPictureSize(640, 480);
        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, "640x480");
        p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "fixed");
        p.set(CameraParameters::KEY_FOCUS_MODE, "fixed");
    }

    p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS, "jpeg");
    p.set(CameraParameters::KEY_JPEG_QUALITY, "95");
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "320");
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "240");
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "90");
    p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES, "320x240,0x0");

    p.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
    p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE, "auto,incandescent,fluorescent,daylight,cloudy-daylight");
    p.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NONE);
    p.set(CameraParameters::KEY_SUPPORTED_EFFECTS, "none,mono,negative,sepia");
    p.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_AUTO);
    if (mCameraId == 0) {
        p.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,
              "auto,portrait,landscape,night,sports,party,beach,sunset,fireworks,candlelight");
        p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-4");
        p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "4");
        p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
        p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0.5");
    } else {
        p.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES, "auto");
    }
    p.set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);
    p.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, "off,50hz,60hz,auto");
    p.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");
    p.set(CameraParameters::KEY_SMOOTH_ZOOM_SUPPORTED, "false");
    p.set(CameraParameters::KEY_MAX_ZOOM, "10");
    p.set(CameraParameters::KEY_ZOOM_RATIOS, "100,120,140,160,180,200,220,240,260,280,300");
    p.set(CameraParameters::KEY_ZOOM, "0");

    mParameters = p;
}

bool QualcommCameraHardware::registerBuffer(int type, int fd, void *vaddr, uint32_t offset,
                                            uint32_t len, uint32_t cbcr_off, bool active, bool reg) {
    struct msm_pmem_info info;
    memset(&info, 0, sizeof(info));
    info.type = type;
    info.fd = fd;
    info.vaddr = vaddr;
    info.offset = offset;
    info.len = len;
    info.y_off = 0;
    info.cbcr_off = cbcr_off;
    info.planar0_off = 0;
    info.planar1_off = cbcr_off;
    info.planar2_off = 0;
    info.active = active ? 1 : 0;

    int op = reg ? CAMERA_OPS_REGISTER_BUFFER : CAMERA_OPS_UNREGISTER_BUFFER;
    int32_t rc = sCamOps.mm_camera_start(op, &info, NULL);
    if (rc != 0) {
        LOGE("registerBuffer(op=%d, fd=%d, off=0x%x, active=%d) failed: %d",
             op, fd, offset, (int)active, rc);
        return false;
    }
    return true;
}

bool QualcommCameraHardware::initPreview() {
    LOGI("initPreview E (%dx%d)", mPreviewWidth, mPreviewHeight);

    mPreviewCbCrOffset = ALIGN4(mPreviewWidth * mPreviewHeight);
    mPreviewFrameSize = mPreviewCbCrOffset + ALIGN4((mPreviewWidth * mPreviewHeight) / 2);

    memset(&mDimension, 0, sizeof(mDimension));
    mDimension.picture_width = (uint16_t)mPictureWidth;
    mDimension.picture_height = (uint16_t)mPictureHeight;
    mDimension.raw_picture_width = (uint16_t)mPictureWidth;
    mDimension.raw_picture_height = (uint16_t)mPictureHeight;
    mDimension.display_width = (uint16_t)mPreviewWidth;
    mDimension.display_height = (uint16_t)mPreviewHeight;
    mDimension.orig_video_width = (uint16_t)mPreviewWidth;
    mDimension.orig_video_height = (uint16_t)mPreviewHeight;
    mDimension.video_width = (uint16_t)mPreviewWidth;
    mDimension.video_height = (uint16_t)mPreviewHeight;
    mDimension.ui_thumbnail_width = 320;
    mDimension.ui_thumbnail_height = 240;
    mDimension.thumbnail_width = 320;
    mDimension.thumbnail_height = 240;
    mDimension.prev_format = 1; /* CAMERA_YUV_420_NV21 */
    mDimension.display_luma_width = (uint16_t)mPreviewWidth;
    mDimension.display_luma_height = (uint16_t)mPreviewHeight;
    mDimension.display_chroma_width = (uint16_t)mPreviewWidth;
    mDimension.display_chroma_height = (uint16_t)mPreviewHeight;

    int32_t rc = sCamConfig.mm_camera_set_parms(CAMERA_PARM_DIMENSION, &mDimension);
    LOGI("set CAMERA_PARM_DIMENSION rc=%d", rc);
    if (rc != 0) {
        LOGE("CAMERA_PARM_DIMENSION failed: %d", rc);
        return false;
    }

    size_t totalSize = (size_t)mPreviewFrameSize * kPreviewBufferCount;
    mPreviewMasterHeap = new MemoryHeapBase("/dev/pmem_adsp", totalSize, MemoryHeapBase::NO_CACHING);
    if (mPreviewMasterHeap->heapID() < 0) {
        LOGE("Failed to open /dev/pmem_adsp for preview (size=%u)", (unsigned)totalSize);
        mPreviewMasterHeap.clear();
        return false;
    }

    mPreviewPmemHeap = new MemoryHeapPmem(mPreviewMasterHeap, 0);
    mPreviewPmemHeap->slap();

    int masterFd = mPreviewMasterHeap->getHeapID();
    uint8_t *base = (uint8_t *)mPreviewMasterHeap->base();

    for (int i = 0; i < kPreviewBufferCount; i++) {
        uint32_t offset = (uint32_t)(mPreviewFrameSize * i);
        void *vaddr = base + offset;
        mPreviewBuffers[i] = new MemoryBase(mPreviewPmemHeap, offset, mPreviewFrameSize);

        memset(&mPreviewFrames[i], 0, sizeof(struct msm_frame));
        mPreviewFrames[i].path = MSM_PMEM_PREVIEW;
        mPreviewFrames[i].type = 2;
        mPreviewFrames[i].buffer = (unsigned long)vaddr;
        mPreviewFrames[i].phy_offset = offset;
        mPreviewFrames[i].y_off = 0;
        mPreviewFrames[i].cbcr_off = (uint32_t)mPreviewCbCrOffset;
        mPreviewFrames[i].planar0_off = 0;
        mPreviewFrames[i].planar1_off = (uint32_t)mPreviewCbCrOffset;
        mPreviewFrames[i].planar2_off = 0;
        mPreviewFrames[i].fd = masterFd;

        bool active = (i < 3);
        if (!registerBuffer(MSM_PMEM_PREVIEW, masterFd, vaddr, offset,
                            mPreviewFrameSize, mPreviewCbCrOffset, active, true)) {
            deinitPreview();
            return false;
        }
        if (!active) {
            LINK_camframe_add_frame(CAM_PREVIEW_FRAME, &mPreviewFrames[i]);
        }
    }

    mPreviewThreadExit = false;
    if (pthread_create(&mPreviewThread, NULL, previewThreadWrapper, this) == 0) {
        mPreviewThreadRunning = true;
    } else {
        LOGE("Failed to create previewThread");
        deinitPreview();
        return false;
    }

    LINK_cam_frame_set_exit_flag(0);
    mCamFrameParms.cammode = 0;
    mCamFrameParms.mode = 1;
    if (pthread_create(&mCamFrameThread, NULL, LINK_cam_frame, &mCamFrameParms) == 0) {
        mCamFrameThreadRunning = true;
        LINK_wait_cam_frame_thread_ready();
        LOGI("cam_frame thread ready");
    } else {
        LOGE("Failed to create cam_frame thread");
        deinitPreview();
        return false;
    }

    mPreviewInitialized = true;
    LOGI("initPreview X: success");
    return true;
}

void QualcommCameraHardware::deinitPreview() {
    LOGI("deinitPreview E");

    if (mCamFrameThreadRunning) {
        LINK_camframe_terminate();
        pthread_join(mCamFrameThread, NULL);
        mCamFrameThreadRunning = false;
    }

    LINK_camframe_release_all_frames(CAM_PREVIEW_FRAME);

    if (mPreviewThreadRunning) {
        {
            Mutex::Autolock qlock(mPreviewQueueLock);
            mPreviewThreadExit = true;
            mPreviewQueueCond.signal();
        }
        pthread_join(mPreviewThread, NULL);
        mPreviewThreadRunning = false;
    }

    if (mPreviewMasterHeap != NULL && mPreviewMasterHeap->heapID() >= 0) {
        int masterFd = mPreviewMasterHeap->getHeapID();
        uint8_t *base = (uint8_t *)mPreviewMasterHeap->base();
        for (int i = 0; i < kPreviewBufferCount; i++) {
            uint32_t offset = (uint32_t)(mPreviewFrameSize * i);
            registerBuffer(MSM_PMEM_PREVIEW, masterFd, base + offset, offset,
                           mPreviewFrameSize, mPreviewCbCrOffset, false, false);
            mPreviewBuffers[i].clear();
        }
    }

    if (mPreviewPmemHeap != NULL) {
        mPreviewPmemHeap->revoke();
        mPreviewPmemHeap.clear();
    }
    mPreviewMasterHeap.clear();
    mPreviewInitialized = false;
    LOGI("deinitPreview X");
}

void *QualcommCameraHardware::previewThreadWrapper(void *user) {
    QualcommCameraHardware *obj = (QualcommCameraHardware *)user;
    obj->runPreviewThread();
    return NULL;
}

void QualcommCameraHardware::runPreviewThread() {
    LOGI("previewThread started");
    while (true) {
        struct msm_frame *frame = NULL;
        {
            Mutex::Autolock lock(mPreviewQueueLock);
            while (mPreviewQueue.isEmpty() && !mPreviewThreadExit) {
                mPreviewQueueCond.wait(mPreviewQueueLock);
            }
            if (mPreviewThreadExit) {
                mPreviewQueue.clear();
                break;
            }
            frame = mPreviewQueue[0];
            mPreviewQueue.removeAt(0);
        }
        if (frame) {
            receivePreviewFrame(frame);
        }
    }
    LOGI("previewThread exiting");
}

struct MemJpegDest {
    struct jpeg_destination_mgr pub;
    Vector<uint8_t> *out;
    uint8_t buf[4096];
};

static void memInitDest(j_compress_ptr cinfo) {
    MemJpegDest *dest = (MemJpegDest *)cinfo->dest;
    dest->pub.next_output_byte = dest->buf;
    dest->pub.free_in_buffer = sizeof(dest->buf);
}

static boolean memEmptyOut(j_compress_ptr cinfo) {
    MemJpegDest *dest = (MemJpegDest *)cinfo->dest;
    dest->out->appendArray(dest->buf, sizeof(dest->buf));
    dest->pub.next_output_byte = dest->buf;
    dest->pub.free_in_buffer = sizeof(dest->buf);
    return TRUE;
}

static void memTermDest(j_compress_ptr cinfo) {
    MemJpegDest *dest = (MemJpegDest *)cinfo->dest;
    size_t count = sizeof(dest->buf) - dest->pub.free_in_buffer;
    if (count > 0) {
        dest->out->appendArray(dest->buf, count);
    }
}

void QualcommCameraHardware::encodePreviewToJpeg(const uint8_t *yuv, int w, int h, int quality) {
    if (!yuv || w <= 0 || h <= 0) return;
    if (quality < 1 || quality > 100) quality = 90;

    Vector<uint8_t> jpegData;
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    MemJpegDest dest;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    dest.out = &jpegData;
    dest.pub.init_destination = memInitDest;
    dest.pub.empty_output_buffer = memEmptyOut;
    dest.pub.term_destination = memTermDest;
    cinfo.dest = &dest.pub;

    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    uint8_t *rowBuf = (uint8_t *)malloc(w * 3);
    if (rowBuf) {
        const uint8_t *yPlane = yuv;
        const uint8_t *uvPlane = yuv + (w * h);
        for (int y = 0; y < h; y++) {
            const uint8_t *yRow = yPlane + y * w;
            const uint8_t *uvRow = uvPlane + (y >> 1) * w;
            for (int x = 0; x < w; x++) {
                int Y = yRow[x];
                int uvIdx = x & ~1;
                int V = (int)uvRow[uvIdx] - 128;
                int U = (int)uvRow[uvIdx + 1] - 128;
                int r = Y + ((359 * V) >> 8);
                int g = Y - ((88 * U + 183 * V) >> 8);
                int b = Y + ((454 * U) >> 8);
                rowBuf[x * 3 + 0] = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
                rowBuf[x * 3 + 1] = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
                rowBuf[x * 3 + 2] = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
            }
            JSAMPROW rowPtr[1] = { rowBuf };
            jpeg_write_scanlines(&cinfo, rowPtr, 1);
        }
        free(rowBuf);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    if (jpegData.size() > 0 && mDataCb && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
        sp<MemoryHeapBase> heap = new MemoryHeapBase(jpegData.size(), 0, "JpegPicture");
        if (heap->heapID() >= 0) {
            memcpy(heap->base(), jpegData.array(), jpegData.size());
            sp<MemoryBase> mem = new MemoryBase(heap, 0, jpegData.size());
            LOGI("takePicture: encoded JPEG %dx%d (%u bytes)", w, h, (unsigned)jpegData.size());
            mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, mem, mCallbackCookie);
        }
    }
}

void QualcommCameraHardware::receivePreviewFrame(struct msm_frame *frame) {
    int idx = -1;
    for (int i = 0; i < kPreviewBufferCount; i++) {
        if (mPreviewFrames[i].buffer == frame->buffer ||
            (frame->phy_offset != 0 && mPreviewFrames[i].phy_offset == frame->phy_offset)) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        LOGW("receivePreviewFrame: unknown frame buffer=0x%lx", frame->buffer);
        return;
    }

    if (mAutoFocusPending) {
        mAutoFocusPending = false;
        if (mNotifyCb && (mMsgEnabled & CAMERA_MSG_FOCUS)) {
            mNotifyCb(CAMERA_MSG_FOCUS, 1, 0, mCallbackCookie);
        }
    }

    if (mZoom > 0 && mPreviewFrames[idx].buffer) {
        applyDigitalZoom((uint8_t *)mPreviewFrames[idx].buffer,
                         mPreviewWidth, mPreviewHeight, mZoom, 10);
    }

    if (mTakePicturePending) {
        if (mFlashSkipFrames > 0) {
            mFlashSkipFrames--;
            if (mCameraRunning) {
                LINK_camframe_add_frame(CAM_PREVIEW_FRAME, &mPreviewFrames[idx]);
            }
            return;
        }
        mTakePicturePending = false;
        mCameraRunning = false;
        if (mNotifyCb && (mMsgEnabled & CAMERA_MSG_SHUTTER)) {
            mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);
        }
        int quality = mParameters.getInt(CameraParameters::KEY_JPEG_QUALITY);
        if (quality <= 0) quality = 95;
        encodePreviewToJpeg((const uint8_t *)mPreviewFrames[idx].buffer,
                            mPreviewWidth, mPreviewHeight, quality);
        if (mFlashPendingOff) {
            mFlashPendingOff = false;
            if (mCameraId == 0) {
                sendExtCmd(1 /* EXT_CFG_SET_FLASH_MODE */, 4 /* EXT_CFG_FLASH_TURN_OFF */, 0);
            }
        }
        return;
    }

    if (mCameraRunning && mDataCb && (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)) {
        mDataCb(CAMERA_MSG_PREVIEW_FRAME, mPreviewBuffers[idx], mCallbackCookie);
    }

    if (mCameraRunning) {
        if (mRecordingRunning && mDataCbTimestamp && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
            mPreviewBusy[idx] = true;
            mDataCbTimestamp(systemTime(), CAMERA_MSG_VIDEO_FRAME, mPreviewBuffers[idx], mCallbackCookie);
        } else {
            LINK_camframe_add_frame(CAM_PREVIEW_FRAME, &mPreviewFrames[idx]);
        }
    }
}

status_t QualcommCameraHardware::startPreviewInternal() {
    if (mCameraRunning) {
        return NO_ERROR;
    }

    if (mPreviewInitialized) {
        sCamOps.mm_camera_stop(CAMERA_OPS_STREAMING_PREVIEW, NULL, NULL);
        deinitPreview();
    }

    if (!initPreview()) {
        LOGE("startPreviewInternal: initPreview failed");
        return UNKNOWN_ERROR;
    }

    int32_t rc = sCamOps.mm_camera_start(CAMERA_OPS_STREAMING_PREVIEW, NULL, NULL);
    LOGI("CAMERA_OPS_STREAMING_PREVIEW start rc=%d", rc);
    if (rc != 0) {
        LOGE("startPreviewInternal: CAMERA_OPS_STREAMING_PREVIEW failed: %d", rc);
        deinitPreview();
        return UNKNOWN_ERROR;
    }

    mCameraRunning = true;
    applyExtSettings(mParameters);
    return NO_ERROR;
}

void QualcommCameraHardware::stopPreviewInternal() {
    if (!mCameraRunning) {
        return;
    }

    mCameraRunning = false;
    if (mCameraId == 0) {
        sendExtCmd(1 /* EXT_CFG_SET_FLASH_MODE */, 4 /* EXT_CFG_FLASH_TURN_OFF */, 0);
        mFlashPendingOff = false;
    }
    int32_t rc = sCamOps.mm_camera_stop(CAMERA_OPS_STREAMING_PREVIEW, NULL, NULL);
    LOGI("CAMERA_OPS_STREAMING_PREVIEW stop rc=%d", rc);
    deinitPreview();
}

sp<IMemoryHeap> QualcommCameraHardware::getPreviewHeap() const {
    return mPreviewPmemHeap;
}

sp<IMemoryHeap> QualcommCameraHardware::getRawHeap() const {
    return NULL;
}

void QualcommCameraHardware::setCallbacks(notify_callback notify_cb,
                                          data_callback data_cb,
                                          data_callback_timestamp data_cb_timestamp,
                                          void* user) {
    Mutex::Autolock lock(mLock);
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mCallbackCookie = user;
}

void QualcommCameraHardware::enableMsgType(int32_t msgType) {
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
}

void QualcommCameraHardware::disableMsgType(int32_t msgType) {
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
}

bool QualcommCameraHardware::msgTypeEnabled(int32_t msgType) {
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType) == msgType;
}

status_t QualcommCameraHardware::startPreview() {
    Mutex::Autolock lock(mLock);
    return startPreviewInternal();
}

void QualcommCameraHardware::stopPreview() {
    Mutex::Autolock lock(mLock);
    stopPreviewInternal();
}

bool QualcommCameraHardware::previewEnabled() {
    Mutex::Autolock lock(mLock);
    return mCameraRunning;
}

#ifdef USE_GETBUFFERINFO
status_t QualcommCameraHardware::getBufferInfo(sp<IMemory>& Frame, size_t *alignedSize) {
    Mutex::Autolock lock(mLock);
    if (mPreviewBuffers[0] == 0) {
        if (alignedSize) *alignedSize = 0;
        return INVALID_OPERATION;
    }
    Frame = mPreviewBuffers[0];
    if (alignedSize) *alignedSize = mPreviewFrameSize;
    return NO_ERROR;
}
#endif

#ifdef CAF_CAMERA_GB_REL
void QualcommCameraHardware::encodeData() {
}
#endif

status_t QualcommCameraHardware::startRecording() {
    Mutex::Autolock lock(mLock);
    if (mRecordingRunning) {
        return NO_ERROR;
    }
    if (!mCameraRunning) {
        status_t err = startPreviewInternal();
        if (err != NO_ERROR) {
            return err;
        }
    }
    mRecordingRunning = true;
    LOGI("startRecording: started");
    return NO_ERROR;
}

void QualcommCameraHardware::stopRecording() {
    Mutex::Autolock lock(mLock);
    if (!mRecordingRunning) {
        return;
    }
    mRecordingRunning = false;
    for (int i = 0; i < kPreviewBufferCount; i++) {
        if (mPreviewBusy[i]) {
            mPreviewBusy[i] = false;
            if (mCameraRunning && LINK_camframe_add_frame) {
                LINK_camframe_add_frame(CAM_PREVIEW_FRAME, &mPreviewFrames[i]);
            }
        }
    }
    LOGI("stopRecording: stopped");
}

bool QualcommCameraHardware::recordingEnabled() {
    Mutex::Autolock lock(mLock);
    return mRecordingRunning;
}

void QualcommCameraHardware::releaseRecordingFrame(const sp<IMemory>& mem) {
    Mutex::Autolock lock(mLock);
    if (mem == 0) return;
    for (int i = 0; i < kPreviewBufferCount; i++) {
        if (mPreviewBuffers[i] == mem) {
            mPreviewBusy[i] = false;
            if (mCameraRunning && LINK_camframe_add_frame) {
                LINK_camframe_add_frame(CAM_PREVIEW_FRAME, &mPreviewFrames[i]);
            }
            return;
        }
    }
}

status_t QualcommCameraHardware::autoFocus() {
    Mutex::Autolock lock(mLock);
    int afMode = 0; /* AF_MODE_NORMAL */
    int32_t rc = sCamOps.mm_camera_start(CAMERA_OPS_FOCUS, &afMode, NULL);
    LOGI("autoFocus: CAMERA_OPS_FOCUS rc=%d", rc);
    mAutoFocusPending = true;
    return NO_ERROR;
}

status_t QualcommCameraHardware::cancelAutoFocus() {
    Mutex::Autolock lock(mLock);
    sCamOps.mm_camera_stop(CAMERA_OPS_FOCUS, NULL, NULL);
    return NO_ERROR;
}

status_t QualcommCameraHardware::takePicture() {
    Mutex::Autolock lock(mLock);
    if (!mCameraRunning) {
        status_t err = startPreviewInternal();
        if (err != NO_ERROR) {
            return err;
        }
    }
    mFlashSkipFrames = 0;
    mFlashPendingOff = false;
    if (mCameraId == 0 && LINK_mm_camera_ext_control) {
        const char *flash = mParameters.get(CameraParameters::KEY_FLASH_MODE);
        if (flash && (!strcmp(flash, CameraParameters::FLASH_MODE_ON) ||
                      !strcmp(flash, CameraParameters::FLASH_MODE_AUTO))) {
            sendExtCmd(1 /* EXT_CFG_SET_FLASH_MODE */, 3 /* EXT_CFG_FLASH_TURN_ON */, 0);
            mFlashSkipFrames = 4;
            mFlashPendingOff = true;
        }
    }
    mTakePicturePending = true;
    return NO_ERROR;
}

status_t QualcommCameraHardware::cancelPicture() {
    Mutex::Autolock lock(mLock);
    mTakePicturePending = false;
    return NO_ERROR;
}

void QualcommCameraHardware::applyDigitalZoom(uint8_t *nv21, int w, int h, int zoom, int maxZoom) {
    if (!nv21 || w <= 0 || h <= 0 || zoom <= 0 || maxZoom <= 0) {
        return;
    }
    if (zoom > maxZoom) {
        zoom = maxZoom;
    }

    size_t needed = (size_t)w * (size_t)h * 3 / 2;
    if (mZoomTempBufSize < needed) {
        uint8_t *nb = (uint8_t *)realloc(mZoomTempBuf, needed);
        if (!nb) {
            return;
        }
        mZoomTempBuf = nb;
        mZoomTempBufSize = needed;
    }

    /* Scale factor 1.0x .. 3.0x */
    int32_t scaleFp = (1 << 16) + ((2 << 16) * zoom) / maxZoom;
    int srcW = (int)(((int64_t)w << 16) / scaleFp) & ~1;
    int srcH = (int)(((int64_t)h << 16) / scaleFp) & ~1;
    if (srcW < 64) srcW = 64;
    if (srcH < 48) srcH = 48;
    int srcX = ((w - srcW) / 2) & ~1;
    int srcY = ((h - srcH) / 2) & ~1;

    /* Copy only the cropped Y & UV band from uncached PMEM into cached RAM once */
    size_t yBandBytes = (size_t)srcH * (size_t)w;
    size_t uvBandBytes = (size_t)(srcH / 2) * (size_t)w;
    size_t srcNeeded = yBandBytes + uvBandBytes;
    if (mZoomSrcBufSize < srcNeeded) {
        uint8_t *sb = (uint8_t *)realloc(mZoomSrcBuf, srcNeeded);
        if (!sb) {
            return;
        }
        mZoomSrcBuf = sb;
        mZoomSrcBufSize = srcNeeded;
    }

    memcpy(mZoomSrcBuf, nv21 + (size_t)srcY * w, yBandBytes);
    memcpy(mZoomSrcBuf + yBandBytes, nv21 + (size_t)w * h + (size_t)(srcY / 2) * w, uvBandBytes);

    uint32_t stepX = (uint32_t)(((int64_t)(srcW - 1) << 16) / (w - 1));
    uint32_t stepY = (uint32_t)(((int64_t)(srcH - 1) << 16) / (h - 1));

    const uint8_t *srcYPlane = mZoomSrcBuf;
    uint8_t *dstYPlane = mZoomTempBuf;

    for (int y = 0; y < h; y++) {
        uint32_t syFp = (uint32_t)y * stepY;
        int sy = (int)(syFp >> 16);
        uint32_t fy = (syFp >> 8) & 0xFF;
        if (sy >= srcH - 1) { sy = srcH - 2; fy = 255; }
        uint32_t ify = 256 - fy;
        const uint8_t *row0 = srcYPlane + sy * w;
        const uint8_t *row1 = row0 + w;
        uint8_t *dstRow = dstYPlane + y * w;
        uint32_t sxFp = ((uint32_t)srcX << 16);
        for (int x = 0; x < w; x++) {
            int sx = (int)(sxFp >> 16);
            uint32_t fx = (sxFp >> 8) & 0xFF;
            uint32_t ifx = 256 - fx;
            uint32_t top = row0[sx] * ifx + row0[sx + 1] * fx;
            uint32_t bot = row1[sx] * ifx + row1[sx + 1] * fx;
            dstRow[x] = (uint8_t)((top * ify + bot * fy) >> 16);
            sxFp += stepX;
        }
    }

    int uvW = w / 2;
    int uvH = h / 2;
    int srcUvH = srcH / 2;
    int srcUvX = srcX / 2;
    const uint8_t *srcUvPlane = mZoomSrcBuf + yBandBytes;
    uint8_t *dstUvPlane = mZoomTempBuf + (size_t)w * h;

    for (int y = 0; y < uvH; y++) {
        uint32_t syFp = (uint32_t)y * stepY;
        int sy = (int)(syFp >> 16);
        uint32_t fy = (syFp >> 8) & 0xFF;
        if (sy >= srcUvH - 1) { sy = srcUvH - 2; fy = 255; }
        uint32_t ify = 256 - fy;
        const uint8_t *row0 = srcUvPlane + sy * w;
        const uint8_t *row1 = row0 + w;
        uint8_t *dstRow = dstUvPlane + y * w;
        uint32_t sxFp = ((uint32_t)srcUvX << 16);
        for (int x = 0; x < uvW; x++) {
            int sx = (int)(sxFp >> 16);
            uint32_t fx = (sxFp >> 8) & 0xFF;
            uint32_t ifx = 256 - fx;
            int idx0 = sx * 2;
            uint32_t topV = row0[idx0] * ifx + row0[idx0 + 2] * fx;
            uint32_t botV = row1[idx0] * ifx + row1[idx0 + 2] * fx;
            uint32_t topU = row0[idx0 + 1] * ifx + row0[idx0 + 3] * fx;
            uint32_t botU = row1[idx0 + 1] * ifx + row1[idx0 + 3] * fx;
            dstRow[x * 2]     = (uint8_t)((topV * ify + botV * fy) >> 16);
            dstRow[x * 2 + 1] = (uint8_t)((topU * ify + botU * fy) >> 16);
            sxFp += stepX;
        }
    }

    memcpy(nv21, mZoomTempBuf, needed);
}

void QualcommCameraHardware::applyExtSettings(const CameraParameters& params) {
    if (mCameraId != 0 || !mPreviewInitialized || !LINK_mm_camera_ext_control) {
        return;
    }

    /* Enable auto-contrast on s5k4ecgx for better dynamic range */
    sendExtCmd(2 /* EXT_CFG_SET_AUTO_CONTRAST */, 0 /* ON */, 0);

    const char *wb = params.get(CameraParameters::KEY_WHITE_BALANCE);
    if (wb) {
        uint16_t val = 0; /* EXT_CFG_WB_AUTO */
        if (!strcmp(wb, CameraParameters::WHITE_BALANCE_DAYLIGHT)) val = 1;
        else if (!strcmp(wb, CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT)) val = 2;
        else if (!strcmp(wb, CameraParameters::WHITE_BALANCE_FLUORESCENT)) val = 3;
        else if (!strcmp(wb, CameraParameters::WHITE_BALANCE_INCANDESCENT)) val = 4;
        sendExtCmd(56 /* EXT_CFG_WB_CONTROL */, val, 0);
    }

    const char *eff = params.get(CameraParameters::KEY_EFFECT);
    if (eff) {
        uint16_t val = 0; /* EXT_CFG_EFFECT_NORMAL */
        if (!strcmp(eff, CameraParameters::EFFECT_NEGATIVE)) val = 1;
        else if (!strcmp(eff, CameraParameters::EFFECT_MONO)) val = 2;
        else if (!strcmp(eff, CameraParameters::EFFECT_SEPIA)) val = 3;
        sendExtCmd(55 /* EXT_CFG_EFFECT_CONTROL */, val, 0);
    }

    int ev = params.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
    if (ev < -4) ev = -4;
    if (ev > 4) ev = 4;
    sendExtCmd(57 /* EXT_CFG_BR_CONTROL */, (uint16_t)(ev + 4), 0);

    const char *scene = params.get(CameraParameters::KEY_SCENE_MODE);
    if (scene) {
        uint16_t val = 0; /* EXT_CFG_SCENE_OFF */
        if (!strcmp(scene, CameraParameters::SCENE_MODE_PORTRAIT)) val = 1;
        else if (!strcmp(scene, CameraParameters::SCENE_MODE_LANDSCAPE)) val = 2;
        else if (!strcmp(scene, CameraParameters::SCENE_MODE_SPORTS)) val = 3;
        else if (!strcmp(scene, CameraParameters::SCENE_MODE_PARTY)) val = 4;
        else if (!strcmp(scene, CameraParameters::SCENE_MODE_BEACH)) val = 5;
        else if (!strcmp(scene, CameraParameters::SCENE_MODE_SUNSET)) val = 6;
        else if (!strcmp(scene, CameraParameters::SCENE_MODE_NIGHT)) val = 9;
        else if (!strcmp(scene, CameraParameters::SCENE_MODE_FIREWORKS)) val = 11;
        else if (!strcmp(scene, CameraParameters::SCENE_MODE_CANDLELIGHT)) val = 13;
        sendExtCmd(60 /* EXT_CFG_SCENE_CONTROL */, val, 0);
    }

    const char *flash = params.get(CameraParameters::KEY_FLASH_MODE);
    if (flash) {
        uint16_t val = 1; /* EXT_CFG_FLASH_OFF */
        if (!strcmp(flash, CameraParameters::FLASH_MODE_ON)) val = 0;
        else if (!strcmp(flash, CameraParameters::FLASH_MODE_AUTO)) val = 2;
        else if (!strcmp(flash, CameraParameters::FLASH_MODE_TORCH)) val = 5;
        sendExtCmd(1 /* EXT_CFG_SET_FLASH_MODE */, val, 0);
        if (val == 5 /* TORCH */) {
            sendExtCmd(1 /* EXT_CFG_SET_FLASH_MODE */, 3 /* EXT_CFG_FLASH_TURN_ON */, 0);
        } else {
            sendExtCmd(1 /* EXT_CFG_SET_FLASH_MODE */, 4 /* EXT_CFG_FLASH_TURN_OFF */, 0);
        }
    }
}

status_t QualcommCameraHardware::setParameters(const CameraParameters& params) {
    Mutex::Autolock lock(mLock);
    int w = 640, h = 480;
    params.getPreviewSize(&w, &h);
    if (w > 0 && h > 0) {
        mPreviewWidth = w;
        mPreviewHeight = h;
    }
    int pw = 2560, ph = 1920;
    params.getPictureSize(&pw, &ph);
    if (pw > 0 && ph > 0) {
        mPictureWidth = pw;
        mPictureHeight = ph;
    }

    int zoom = params.getInt(CameraParameters::KEY_ZOOM);
    if (zoom < 0) zoom = 0;
    if (zoom > 10) zoom = 10;
    mZoom = zoom;

    mParameters = params;
    applyExtSettings(mParameters);
    return NO_ERROR;
}

CameraParameters QualcommCameraHardware::getParameters() const {
    Mutex::Autolock lock(mLock);
    return mParameters;
}

status_t QualcommCameraHardware::sendCommand(int32_t cmd, int32_t arg1, int32_t arg2) {
    (void)cmd;
    (void)arg1;
    (void)arg2;
    return NO_ERROR;
}

void QualcommCameraHardware::release() {
    Mutex::Autolock lock(mLock);
    if (mReleased) return;
    LOGI("release E");
    stopPreviewInternal();
    if (LINK_mm_camera_deinit) {
        LINK_mm_camera_deinit();
    }
    if (LINK_mm_camera_destroy) {
        LINK_mm_camera_destroy();
    }
    mReleased = true;
    LOGI("release X");
}

status_t QualcommCameraHardware::dump(int fd, const Vector<String16>& args) const {
    (void)fd;
    (void)args;
    return NO_ERROR;
}

}; // namespace android
