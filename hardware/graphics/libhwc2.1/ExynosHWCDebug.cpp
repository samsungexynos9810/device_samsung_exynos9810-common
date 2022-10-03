/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "ExynosHWCDebug.h"
#include "ExynosDisplay.h"
#include <android/sync.h>
#include "exynos_sync.h"

uint32_t mErrLogSize = 0;
uint32_t mFenceLogSize = 0;

extern struct exynos_hwc_control exynosHWCControl;

int32_t saveErrorLog(const String8 &errString, ExynosDisplay *display)
{
    int32_t ret = NO_ERROR;
    if (mErrLogSize >= ERR_LOG_SIZE)
        return -1;

    FILE *pFile = NULL;
    char filePath[128];
    sprintf(filePath, "%s/hwc_error_log.txt", ERROR_LOG_PATH0);
    pFile = fopen(filePath, "a");
    if (pFile == NULL) {
        ALOGE("Fail to open file %s/hwc_error_log.txt, error: %s", ERROR_LOG_PATH0, strerror(errno));
        sprintf(filePath, "%s/hwc_error_log.txt", ERROR_LOG_PATH1);
        pFile = fopen(filePath, "a");
    }
    if (pFile == NULL) {
        ALOGE("Fail to open file %s/hwc_error_log.txt, error: %s", ERROR_LOG_PATH1, strerror(errno));
        return -errno;
    }

    mErrLogSize = ftell(pFile);
    if (mErrLogSize >= ERR_LOG_SIZE) {
        if (pFile != NULL)
            fclose(pFile);
        return -1;
    }

    String8 saveString;
    struct timeval tv;
    struct tm* localTime;
    gettimeofday(&tv, NULL);
    localTime = (struct tm*)localtime((time_t*)&tv.tv_sec);

    if (display != NULL) {
        saveString.appendFormat("%02d-%02d %02d:%02d:%02d.%03lu(%lu) %s %" PRIu64 ": %s\n",
                localTime->tm_mon+1, localTime->tm_mday,
                localTime->tm_hour, localTime->tm_min,
                localTime->tm_sec, tv.tv_usec/1000, ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)),
                display->mDisplayName.string(), display->mErrorFrameCount,
                errString.string());
    } else {
        saveString.appendFormat("%02d-%02d %02d:%02d:%02d.%03lu(%lu) : %s\n",
                localTime->tm_mon+1, localTime->tm_mday,
                localTime->tm_hour, localTime->tm_min,
                localTime->tm_sec, tv.tv_usec/1000, ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)),
                errString.string());
    }

    if (pFile != NULL) {
        fwrite(saveString.string(), 1, saveString.size(), pFile);
        mErrLogSize = (uint32_t)ftell(pFile);
        ret = mErrLogSize;
        fclose(pFile);
    }
    return ret;
}

int32_t saveFenceTrace(ExynosDisplay *display) {
    int32_t ret = NO_ERROR;

    if (mFenceLogSize >= FENCE_ERR_LOG_SIZE)
        return -1;

    FILE *pFile = NULL;
    char filePath[128];
    String8 saveString;
    ExynosDevice *device = NULL;

    if (display == NULL)
        return 0;
    else
        device = display->mDevice;

    if (device == NULL)
        return 0;

    sprintf(filePath, "%s/hwc_fence_state.txt", ERROR_LOG_PATH0);
    pFile = fopen(filePath, "a");

    if (pFile == NULL) {
        ALOGE("Fail to open file %s/hwc_fence_state.txt, error: %s", ERROR_LOG_PATH0, strerror(errno));
        sprintf(filePath, "%s/hwc_fence_state.txt", ERROR_LOG_PATH1);
        pFile = fopen(filePath, "a");
    }
    if (pFile == NULL) {
        ALOGE("Fail to open file %s, error: %s", ERROR_LOG_PATH1, strerror(errno));
        return -errno;
    }

    mFenceLogSize = ftell(pFile);
    if (mFenceLogSize >= FENCE_ERR_LOG_SIZE) {
        fclose(pFile);
        return -1;
    }

    struct timeval tv;
    struct tm* localTime;
    gettimeofday(&tv, NULL);
    localTime = (struct tm*)localtime((time_t*)&tv.tv_sec);

    saveString.appendFormat("\nerrFrameNumber: %" PRId64 " time:%02d-%02d %02d:%02d:%02d.%03lu(%lu)\n",
            display->mErrorFrameCount,
            localTime->tm_mon+1, localTime->tm_mday,
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, tv.tv_usec/1000,
            ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)));

    if (device != NULL) {
        for (auto it = device->mFenceInfo.begin(); it != device->mFenceInfo.end(); ++it) {
            uint32_t i = it->first;

            hwc_fence_info_t info = device->mFenceInfo.at(i);

            if (info.usage >= 1) {
                saveString.appendFormat("FD hwc : %d, usage %d, pending : %d\n", i, info.usage, (int)info.pendingAllowed);
                for (int j = 0; j < MAX_FENCE_SEQUENCE; j++) {
                    fenceTrace_t *seq = &info.seq[j];
                    saveString.appendFormat("    %s(%s)(%s)(cur:%d)(usage:%d)(last:%d)",
                            GET_STRING(fence_dir_map, seq->dir),
                            GET_STRING(fence_ip_map, seq->ip), GET_STRING(fence_type_map, seq->type),
                            seq->curFlag, seq->usage, (int)seq->isLast);

                    tv = seq->time;
                    localTime = (struct tm*)localtime((time_t*)&tv.tv_sec);

                    saveString.appendFormat(" - time:%02d-%02d %02d:%02d:%02d.%03lu(%lu)\n",
                            localTime->tm_mon+1, localTime->tm_mday,
                            localTime->tm_hour, localTime->tm_min,
                            localTime->tm_sec, tv.tv_usec/1000,
                            ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)));
                }
            }
        }
    }

    if (pFile != NULL) {
        fwrite(saveString.string(), 1, saveString.size(), pFile);
        mFenceLogSize = (uint32_t)ftell(pFile);
        ret = mFenceLogSize;
        fclose(pFile);
    }

    return ret;
}
