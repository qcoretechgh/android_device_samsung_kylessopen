/*
 * Camera HAL for Samsung Galaxy S Duos (GT-S7562 / kylessopen)
 * Built against stock liboemcamera.so ABI (Qualcomm MSM7x27A)
 */

#ifndef ANDROID_HARDWARE_QUALCOMM_CAMERA_HARDWARE_H
#define ANDROID_HARDWARE_QUALCOMM_CAMERA_HARDWARE_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <utils/threads.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <binder/MemoryHeapPmem.h>
#include <camera/CameraHardwareInterface.h>
#include <camera/CameraParameters.h>

namespace android {

/* Kernel ABI structures matching stock GT-S7562 msm_camera.h */
#define MSM_PMEM_PREVIEW 15

struct fd_roi_info {
    void *info;
    int info_len;
};

struct ion_allocation_data_abi {
    size_t len;
    size_t align;
    unsigned int flags;
    void *handle;
};

struct ion_fd_data_abi {
    void *handle;
    int fd;
};

struct msm_frame {
    struct timespec ts;
    int path;
    int type;
    unsigned long buffer;
    uint32_t phy_offset;
    uint32_t y_off;
    uint32_t cbcr_off;
    uint32_t planar0_off;
    uint32_t planar1_off;
    uint32_t planar2_off;
    int fd;
    void *cropinfo;
    int croplen;
    uint32_t error_code;
    struct fd_roi_info roi_info;
    uint32_t frame_id;
    int stcam_quality_ind;
    uint32_t stcam_conv_value;
    struct ion_allocation_data_abi ion_alloc;
    struct ion_fd_data_abi fd_data;
}; /* 104 bytes */

struct msm_pmem_info {
    int type;
    int fd;
    void *vaddr;
    uint32_t offset;
    uint32_t len;
    uint32_t y_off;
    uint32_t cbcr_off;
    uint32_t planar0_off;
    uint32_t planar1_off;
    uint32_t planar2_off;
    uint8_t active;
}; /* 44 bytes */

/* ABI structures matching stock GT-S7562 liboemcamera.so */

typedef struct {
    int modes_supported;
    int8_t camera_id;
    int position;
    int sensor_mount_angle;
} mm_camera_info_t; /* 16 bytes */

typedef enum {
    MM_CAMERA_STATUS_SUCCESS = 0,
    MM_CAMERA_STATUS_ERR_GENERAL,
} mm_camera_status_t;

typedef enum {
    CAMERA_OPS_STREAMING_PREVIEW = 0,
    CAMERA_OPS_STREAMING_ZSL = 1,
    CAMERA_OPS_STREAMING_VIDEO = 2,
    CAMERA_OPS_CAPTURE = 3,
    CAMERA_OPS_CAPTURE_AND_ENCODE = 4,
    CAMERA_OPS_RAW_CAPTURE = 5,
    CAMERA_OPS_ENCODE = 6,
    CAMERA_OPS_ZSL_STREAMING_CB = 7,
    CAMERA_OPS_LIVESHOT = 8,
    CAMERA_OPS_RAW_LIVESHOT = 9,
    CAMERA_OPS_FOCUS = 10,
    CAMERA_OPS_REGISTER_BUFFER = 11,
    CAMERA_OPS_UNREGISTER_BUFFER = 12,
    CAMERA_OPS_PREPARE_SNAPSHOT = 13,
    CAMERA_OPS_MAX
} mm_camera_ops_type_t;

typedef enum {
    CAMERA_PARM_PICT_SIZE = 0,
    CAMERA_PARM_ZOOM_RATIO = 1,
    CAMERA_PARM_HISTOGRAM = 2,
    CAMERA_PARM_DIMENSION = 3,
    CAMERA_PARM_FPS = 4,
    CAMERA_PARM_FPS_MODE = 5,
    CAMERA_PARM_EFFECT = 6,
    CAMERA_PARM_EXPOSURE_COMPENSATION = 7,
    CAMERA_PARM_ISO = 8,
    CAMERA_PARM_WB = 9,
    CAMERA_PARM_ANTIBANDING = 10,
    CAMERA_PARM_CONTRAST = 11,
    CAMERA_PARM_BRIGHTNESS = 12,
    CAMERA_PARM_SATURATION = 13,
    CAMERA_PARM_SHARPNESS = 14,
    CAMERA_PARM_EXPOSURE = 15,
    CAMERA_PARM_AUTO_FOCUS = 16,
    CAMERA_PARM_LED_MODE = 23,
    CAMERA_PARM_ZOOM = 26,
    CAMERA_PARM_MODE = 31,
    CAMERA_PARM_SENSOR_INFO = 32,
    CAMERA_PARM_CAMERA_ID = 33,
    CAMERA_PARM_BESTSHOT_MODE = 41,
    CAMERA_PARM_ZSL_ENABLE = 42,
    CAMERA_PARM_MAX_ZOOM = 55,
} camera_parm_type_t;

typedef enum {
    CAM_VIDEO_FRAME = 0,
    CAM_SNAPSHOT_FRAME = 1,
    CAM_PREVIEW_FRAME = 2,
    CAM_ZSL_FRAME = 3,
} cam_frame_type_t;

typedef struct {
    uint32_t cammode;
    uint32_t mode;
} cam_frame_start_parms_t;

typedef union {
    struct {
        uint16_t picture_width;         /* +0 */
        uint16_t picture_height;        /* +2 */
        uint16_t raw_picture_width;     /* +4 */
        uint16_t raw_picture_height;    /* +6 */
        uint16_t display_width;         /* +8 */
        uint16_t display_height;        /* +10 */
        uint16_t orig_video_width;      /* +12 */
        uint16_t orig_video_height;     /* +14 */
        uint16_t video_width;           /* +16 */
        uint16_t video_height;          /* +18 */
        uint16_t ui_thumbnail_height;   /* +20 */
        uint16_t ui_thumbnail_width;    /* +22 */
        uint16_t thumbnail_width;       /* +24 */
        uint16_t thumbnail_height;      /* +26 */
        uint8_t  pad1[16];              /* +28..43 */
        uint32_t prev_format;           /* +44 */
        uint32_t enc_format;            /* +48 */
        uint32_t main_img_format;       /* +52 */
        uint32_t thumb_format;          /* +56 */
        uint8_t  pad2[16];              /* +60..75 */
        uint16_t display_luma_width;    /* +76 */
        uint16_t display_luma_height;   /* +78 */
        uint16_t display_chroma_width;  /* +80 */
        uint16_t display_chroma_height; /* +82 */
    };
    uint8_t raw[400];
} cam_ctrl_dimension_t; /* 400 bytes */

typedef struct {
    int event_type;
    union {
        int status;
        uint8_t data[64];
    };
} mm_camera_event_t;

typedef struct {
    void (*on_event)(mm_camera_event_t *evt);
    void (*video_frame_cb)(struct msm_frame *frame);
    void (*preview_frame_cb)(struct msm_frame *frame);
    void (*camframe_error_cb)(int err);
    void (*camstats_cb)(int type, void *hist);
    void (*jpegfragment_cb)(uint8_t *buf, uint32_t size);
    void (*jpeg_cb)(int event);
    void (*liveshot_cb)(int event);
} mm_camera_notify_t; /* 32 bytes */

typedef struct {
    int32_t (*mm_camera_query_parms)(int parm_type, void **pp_values, uint32_t *count);
    int32_t (*mm_camera_set_parms)(int parm_type, void *p_value);
    int32_t (*mm_camera_get_parms)(int parm_type, void *p_value);
    int8_t  (*mm_camera_is_parm_supported)(int parm_type);
    int8_t  (*mm_camera_is_subparm_supported)(int parm_type, int subparm);
} mm_camera_config_t; /* 20 bytes */

typedef struct {
    int32_t (*mm_camera_init)(int ops_type, void *arg1, void *arg2);
    int32_t (*mm_camera_start)(int ops_type, void *arg1, void *arg2);
    int32_t (*mm_camera_stop)(int ops_type, void *arg1, void *arg2);
    int32_t (*mm_camera_deinit)(int ops_type, void *arg1, void *arg2);
    int8_t  (*mm_camera_is_op_supported)(int ops_type);
    int32_t (*mm_camera_ext_control)(void *arg1, void *arg2, void *arg3);
} mm_camera_ops_t; /* 24 bytes */

class QualcommCameraHardware : public CameraHardwareInterface {
public:
    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual void setCallbacks(notify_callback notify_cb,
                              data_callback data_cb,
                              data_callback_timestamp data_cb_timestamp,
                              void* user);

    virtual void enableMsgType(int32_t msgType);
    virtual void disableMsgType(int32_t msgType);
    virtual bool msgTypeEnabled(int32_t msgType);

    virtual status_t startPreview();
    virtual void stopPreview();
    virtual bool previewEnabled();

#ifdef USE_GETBUFFERINFO
    virtual status_t getBufferInfo(sp<IMemory>& Frame, size_t *alignedSize);
#endif
#ifdef CAF_CAMERA_GB_REL
    virtual void encodeData();
#endif

    virtual status_t startRecording();
    virtual void stopRecording();
    virtual bool recordingEnabled();
    virtual void releaseRecordingFrame(const sp<IMemory>& mem);

    virtual status_t autoFocus();
    virtual status_t cancelAutoFocus();
    virtual status_t takePicture();
    virtual status_t cancelPicture();

    virtual status_t setParameters(const CameraParameters& params);
    virtual CameraParameters getParameters() const;
    virtual status_t sendCommand(int32_t cmd, int32_t arg1, int32_t arg2);
    virtual void release();
    virtual status_t dump(int fd, const Vector<String16>& args) const;

    static sp<CameraHardwareInterface> createInstance(int cameraId);

private:
    QualcommCameraHardware(int cameraId);
    virtual ~QualcommCameraHardware();

    bool startCamera();
    void initDefaultParameters();
    bool initPreview();
    void deinitPreview();
    status_t startPreviewInternal();
    void stopPreviewInternal();

    bool registerBuffer(int type, int fd, void *vaddr, uint32_t offset,
                        uint32_t len, uint32_t cbcr_off, bool active, bool reg);

    static void onEventCb(mm_camera_event_t *evt);
    static void previewFrameCb(struct msm_frame *frame);
    static void videoFrameCb(struct msm_frame *frame);
    static void camframeErrorCb(int err);
    static void camstatsCb(int type, void *hist);
    static void jpegFragmentCb(uint8_t *buf, uint32_t size);
    static void jpegCb(int event);
    static void liveshotCb(int event);

    static void *previewThreadWrapper(void *user);
    void runPreviewThread();
    void receivePreviewFrame(struct msm_frame *frame);
    void encodePreviewToJpeg(const uint8_t *yuv, int w, int h, int quality);
    void applyDigitalZoom(uint8_t *nv21, int w, int h, int zoom, int maxZoom);
    void applyExtSettings(const CameraParameters& params);

    static const int kPreviewBufferCount = 6;

    int mCameraId;
    bool mCameraRunning;
    bool mRecordingRunning;
    bool mPreviewInitialized;
    bool mReleased;
    volatile bool mTakePicturePending;
    volatile bool mAutoFocusPending;
    volatile int mFlashSkipFrames;
    bool mFlashPendingOff;
    int mZoom;
    uint8_t *mZoomSrcBuf;
    size_t mZoomSrcBufSize;
    uint8_t *mZoomTempBuf;
    size_t mZoomTempBufSize;

    mutable Mutex mLock;
    CameraParameters mParameters;
    int mPreviewWidth;
    int mPreviewHeight;
    int mPictureWidth;
    int mPictureHeight;
    int mPreviewFrameSize;
    int mPreviewCbCrOffset;

    notify_callback mNotifyCb;
    data_callback mDataCb;
    data_callback_timestamp mDataCbTimestamp;
    void *mCallbackCookie;
    int32_t mMsgEnabled;

    sp<MemoryHeapBase> mPreviewMasterHeap;
    sp<MemoryHeapPmem> mPreviewPmemHeap;
    sp<MemoryBase> mPreviewBuffers[kPreviewBufferCount];
    struct msm_frame mPreviewFrames[kPreviewBufferCount];
    bool mPreviewBusy[kPreviewBufferCount];

    /* Preview frame queue */
    Mutex mPreviewQueueLock;
    Condition mPreviewQueueCond;
    Vector<struct msm_frame *> mPreviewQueue;
    bool mPreviewThreadRunning;
    bool mPreviewThreadExit;
    pthread_t mPreviewThread;

    /* liboemcamera cam_frame thread */
    bool mCamFrameThreadRunning;
    pthread_t mCamFrameThread;
    cam_frame_start_parms_t mCamFrameParms;

    cam_ctrl_dimension_t mDimension;
};

}; // namespace android

#endif
