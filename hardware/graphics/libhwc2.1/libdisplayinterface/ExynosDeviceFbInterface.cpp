#include "ExynosDeviceFbInterface.h"
#include "ExynosDevice.h"
#include "ExynosDisplay.h"
#include "ExynosExternalDisplayModule.h"
#include "ExynosHWCHelper.h"
#include "ExynosHWCDebug.h"
#include "ExynosResourceRestriction.h"

extern update_time_info updateTimeInfo;
#ifndef USE_MODULE_ATTR
extern feature_support_t feature_table[];
#endif

void handle_vsync_event(ExynosDevice *dev, ExynosDisplay *display) {

    int err = 0;

    if ((dev == NULL) || (display == NULL)
            || (dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].funcPointer == NULL))
        return;

    dev->compareVsyncPeriod();

    hwc2_callback_data_t callbackData =
        dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].callbackData;
    HWC2_PFN_VSYNC callbackFunc =
        (HWC2_PFN_VSYNC)dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].funcPointer;

    err = lseek(display->mVsyncFd, 0, SEEK_SET);

    if (err < 0 ) {
        ExynosDisplay *display = (ExynosDisplay*)dev->getDisplay(getDisplayId(HWC_DISPLAY_PRIMARY, 0));
        if (display->mVsyncState == HWC2_VSYNC_ENABLE)
            ALOGE("error seeking to vsync timestamp: %s", strerror(errno));
        return;
    }

    if (callbackData != NULL && callbackFunc != NULL) {
        /** Vsync read **/
        char buf[4096];
        err = read(display->mVsyncFd , buf, sizeof(buf));
        if (err < 0) {
            ALOGE("error reading vsync timestamp: %s", strerror(errno));
            return;
        }

        if (dev->mVsyncDisplayId != display->mDisplayId)
            return;

        dev->mTimestamp = strtoull(buf, NULL, 0);

        gettimeofday(&updateTimeInfo.lastUeventTime, NULL);
        /** Vsync callback **/
        callbackFunc(callbackData, getDisplayId(HWC_DISPLAY_PRIMARY, 0), dev->mTimestamp);
    }
}

void *hwc_eventHndler_thread(void *data) {

    android::Vector< ExynosDisplay* > display_list;

    /** uevent init **/
    char uevent_desc[4096];
    memset(uevent_desc, 0, sizeof(uevent_desc));

    ExynosDevice *dev = (ExynosDevice*)data;

    setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);

    uevent_init();

    int32_t cnt_of_event = 1; // uevent is default
    for (size_t i = 0; i < DISPLAY_COUNT; i++) {
        exynos_display_t display_t = AVAILABLE_DISPLAY_UNITS[i];
        if (display_t.type != HWC_DISPLAY_VIRTUAL)
            cnt_of_event++;
    }

    struct pollfd fds[cnt_of_event];
    fds[0].fd = uevent_get_fd();
    fds[0].events = POLLIN;

    /** Set external display's uevent name **/
    char ueventname_ext[MAX_DEV_NAME + 1];
    ueventname_ext[MAX_DEV_NAME] = '\0';
    sprintf(ueventname_ext, DP_UEVENT_NAME, DP_LINK_NAME);
    ALOGI("uevent name of ext: %s", ueventname_ext);
    int32_t event_index = 0;

    /** Vsync init. **/
    for (size_t i = 0; i < DISPLAY_COUNT; i++) {
        exynos_display_t display_t = AVAILABLE_DISPLAY_UNITS[i];
        if (display_t.type == HWC_DISPLAY_VIRTUAL) continue;

        char devname[MAX_DEV_NAME + 1];
        devname[MAX_DEV_NAME] = '\0';
        strncpy(devname, VSYNC_DEV_PREFIX, MAX_DEV_NAME);
        strlcat(devname, display_t.vsync_node_name, MAX_DEV_NAME);
        ExynosDisplay *display = dev->getDisplay(getDisplayId(display_t.type,display_t.index));
        display->mVsyncFd = open(devname, O_RDONLY);
        if (display->mVsyncFd < 0)
            ALOGI("Failed to open vsync attribute at %s", devname);

        fds[event_index + 1].fd = display->mVsyncFd;
        fds[event_index + 1].events = POLLPRI;
        event_index++;

        display_list.add(display);
    }

    /** Polling events **/
    while (true) {
        int err = poll(fds, cnt_of_event, -1);

        if (err > 0) {
            if (fds[0].revents & POLLIN) {
                uevent_next_event(uevent_desc, sizeof(uevent_desc) - 2);
                bool dp_status = !strcmp(uevent_desc, ueventname_ext);

                /**
                 * If uevent for external display's hotplug is detected,
                 * read the cable state and process the hotplug
                 * if the state is different from the current state that HWC has.
                 **/
                if (dp_status) {
                    for (size_t i = 0; i < display_list.size(); i++) {
                        if (display_list[i]->mType == HWC_DISPLAY_EXTERNAL) {
                            ExynosExternalDisplayModule *display = (ExynosExternalDisplayModule *)display_list[i];
                            if (display->checkHotplugEventUpdated())
                                display->handleHotplugEvent();
                        }
                    }
                }
            } else {
                for (size_t i = 0; i < display_list.size(); i++) {
                    if (fds[i + 1].revents & POLLPRI) {
                        handle_vsync_event((ExynosDevice*)dev, display_list[i]);
                    }
                }
            }
        }
        else if (err == -1) {
            if (errno == EINTR)
                break;
            ALOGE("error in event thread: %s", strerror(errno));
        }
    }
    return NULL;
}

ExynosDeviceFbInterface::ExynosDeviceFbInterface(ExynosDevice *exynosDevice)
{
    mUseQuery = false;
    mExynosDevice = exynosDevice;
    mEventHandlerThread = 0;
    mDisplayFd = -1;
}

ExynosDeviceFbInterface::~ExynosDeviceFbInterface()
{
    /* TODO kill threads here */
    pthread_kill(mEventHandlerThread, SIGTERM);
    pthread_join(mEventHandlerThread, NULL);
}

void ExynosDeviceFbInterface::init(ExynosDevice *exynosDevice)
{
    int ret = 0;
    mExynosDevice = exynosDevice;

    ExynosDisplay *primaryDisplay = (ExynosDisplay*)mExynosDevice->getDisplay(HWC_DISPLAY_PRIMARY);
    ExynosDisplayInterface *displayInterface = primaryDisplay->mDisplayInterface;
    mDisplayFd = displayInterface->getDisplayFd();
    updateRestrictions();

    /** Event handler thread creation **/
    ret = pthread_create(&mEventHandlerThread, NULL, hwc_eventHndler_thread, mExynosDevice);
    if (ret) {
        ALOGE("failed to start vsync thread: %s", strerror(ret));
        ret = -ret;
    }
}

int32_t ExynosDeviceFbInterface::makeDPURestrictions() {
    int i, j, cnt = 0;
    int32_t ret = 0;

    struct dpp_restrictions_info *dpuInfo = &mDPUInfo.dpuInfo;
    HDEBUGLOGD(eDebugAttrSetting, "DPP ver : %d, cnt : %d", dpuInfo->ver, dpuInfo->dpp_cnt);
    ExynosResourceManager *resourceManager = mExynosDevice->mResourceManager;

    /* format resctriction */
    for (i = 0; i < dpuInfo->dpp_cnt; i++){
        dpp_restriction r = dpuInfo->dpp_ch[i].restriction;
        HDEBUGLOGD(eDebugAttrSetting, "id : %d, format count : %d", i, r.format_cnt);
    }

    restriction_key_t queried_format_table[1024];

    /* Check attribute overlap */
    for (i = 0; i < dpuInfo->dpp_cnt; i++){
        for (j = 0; j < dpuInfo->dpp_cnt; j++){
            if (i >= j) continue;
            dpp_ch_restriction r1 = dpuInfo->dpp_ch[i];
            dpp_ch_restriction r2 = dpuInfo->dpp_ch[j];
            /* If attribute is same, will not be added to table */
            if (r1.attr == r2.attr) {
                mDPUInfo.overlap[j] = true;
            }
        }
        HDEBUGLOGD(eDebugAttrSetting, "Index : %d, overlap %d", i, mDPUInfo.overlap[i]);
    }

    for (i = 0; i < dpuInfo->dpp_cnt; i++){
        if (mDPUInfo.overlap[i]) continue;
        dpp_restriction r = dpuInfo->dpp_ch[i].restriction;
        mpp_phycal_type_t hwType = resourceManager->getPhysicalType(i);
        for (j = 0; j < r.format_cnt; j++){
            if (DpuFormatToHalFormat(r.format[j]) != HAL_PIXEL_FORMAT_EXYNOS_UNDEFINED) {
                queried_format_table[cnt].hwType = hwType;
                queried_format_table[cnt].nodeType = NODE_NONE;
                queried_format_table[cnt].format = DpuFormatToHalFormat(r.format[j]);
                queried_format_table[cnt].reserved = 0;
                resourceManager->makeFormatRestrictions(queried_format_table[cnt], r.format[j]);
                cnt++;
            }
            HDEBUGLOGD(eDebugAttrSetting, "%s : %d", getMPPStr(hwType).string(), r.format[j]);
        }
    }

    /* Size restriction */
    restriction_size rSize;

    for (i = 0; i < dpuInfo->dpp_cnt; i++){
        if (mDPUInfo.overlap[i]) continue;
        dpp_restriction r = dpuInfo->dpp_ch[i].restriction;

        /* RGB size restrictions */
        rSize.maxDownScale = r.scale_down;
        rSize.maxUpScale = r.scale_up;
        rSize.maxFullWidth = r.dst_f_w.max;
        rSize.maxFullHeight = r.dst_f_h.max;
        rSize.minFullWidth = r.dst_f_w.min;
        rSize.minFullHeight = r.dst_f_h.min;;
        rSize.fullWidthAlign = r.dst_x_align;
        rSize.fullHeightAlign = r.dst_y_align;;
        rSize.maxCropWidth = r.src_w.max;
        rSize.maxCropHeight = r.src_h.max;
        rSize.minCropWidth = r.src_w.min;
        rSize.minCropHeight = r.src_h.min;
        rSize.cropXAlign = r.src_x_align;
        rSize.cropYAlign = r.src_y_align;
        rSize.cropWidthAlign = r.blk_x_align;
        rSize.cropHeightAlign = r.blk_y_align;

        mpp_phycal_type_t hwType = resourceManager->getPhysicalType(i);
        resourceManager->makeSizeRestrictions(hwType, rSize, RESTRICTION_RGB);

        /* YUV size restrictions */
        rSize.minCropWidth = 32; //r.src_w.min;
        rSize.minCropHeight = 32; //r.src_h.min;
        rSize.fullWidthAlign = max(r.dst_x_align, YUV_CHROMA_H_SUBSAMPLE);
        rSize.fullHeightAlign = max(r.dst_y_align, YUV_CHROMA_V_SUBSAMPLE);
        rSize.cropXAlign = max(r.src_x_align, YUV_CHROMA_H_SUBSAMPLE);
        rSize.cropYAlign = max(r.src_y_align, YUV_CHROMA_V_SUBSAMPLE);
        rSize.cropWidthAlign = max(r.blk_x_align, YUV_CHROMA_H_SUBSAMPLE);
        rSize.cropHeightAlign = max(r.blk_y_align, YUV_CHROMA_V_SUBSAMPLE);

        resourceManager->makeSizeRestrictions(hwType, rSize, RESTRICTION_YUV);
    }
    return ret;
}

int32_t ExynosDeviceFbInterface::updateFeatureTable() {

    struct dpp_restrictions_info *dpuInfo = &mDPUInfo.dpuInfo;
    ExynosResourceManager *resourceManager = mExynosDevice->mResourceManager;
    uint32_t featureTableCnt = resourceManager->getFeatureTableSize();
    int attrMapCnt = sizeof(dpu_attr_map_table)/sizeof(dpu_attr_map_t);
    int dpp_cnt = dpuInfo->dpp_cnt;
    int32_t ret = 0;

    HDEBUGLOGD(eDebugAttrSetting, "Before");
    for (uint32_t j = 0; j < featureTableCnt; j++){
        HDEBUGLOGD(eDebugAttrSetting, "type : %d, feature : 0x%lx",
            feature_table[j].hwType,
            (unsigned long)feature_table[j].attr);
    }

    // dpp count
    for (int i = 0; i < dpp_cnt; i++){
        dpp_ch_restriction c_r = dpuInfo->dpp_ch[i];
        if (mDPUInfo.overlap[i]) continue;
        HDEBUGLOGD(eDebugAttrSetting, "DPU attr : (ch:%d), 0x%lx", i, (unsigned long)c_r.attr);
        mpp_phycal_type_t hwType = resourceManager->getPhysicalType(i);
        // feature table count
        for (uint32_t j = 0; j < featureTableCnt; j++){
            if (feature_table[j].hwType == hwType) {
                // dpp attr count
                for (int k = 0; k < attrMapCnt; k++) {
                    if (c_r.attr & (1 << dpu_attr_map_table[k].dpp_attr)) {
                        feature_table[j].attr |= dpu_attr_map_table[k].hwc_attr;
                    }
                }
            }
        }
    }

    HDEBUGLOGD(eDebugAttrSetting, "After");
    for (uint32_t j = 0; j < featureTableCnt; j++){
        HDEBUGLOGD(eDebugAttrSetting, "type : %d, feature : 0x%lx",
            feature_table[j].hwType,
            (unsigned long)feature_table[j].attr);
        resourceManager->mMPPAttrs.insert(std::make_pair((uint32_t)feature_table[j].hwType,
                    (uint64_t)feature_table[j].attr));
    }
    return ret;
}

void ExynosDeviceFbInterface::updateRestrictions()
{
    struct dpp_restrictions_info *dpuInfo = &mDPUInfo.dpuInfo;
    int32_t ret = 0;

    if ((ret = ioctl(mDisplayFd, EXYNOS_DISP_RESTRICTIONS, dpuInfo)) < 0) {
        ALOGI("EXYNOS_DISP_RESTRICTIONS ioctl failed: %s", strerror(errno));
        mUseQuery = false;
        return;
    }
    if ((ret = makeDPURestrictions()) != NO_ERROR) {
        ALOGE("makeDPURestrictions fail");
    } else if ((ret = updateFeatureTable()) != NO_ERROR) {
        ALOGE("updateFeatureTable fail");
    }

    if (ret == NO_ERROR)
        mUseQuery = true;
    else
        mUseQuery = false;

    return;
}
