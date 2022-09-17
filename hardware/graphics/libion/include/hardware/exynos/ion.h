/*
 *  hardware/exynos/ion.h
 *
 *   Copyright 2018 Samsung Electronics Co., Ltd.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef __HARDWARE_EXYNOS_ION_H__
#define __HARDWARE_EXYNOS_ION_H__

#define ION_EXYNOS_HEAP_ID_SYSTEM        0
#define ION_EXYNOS_HEAP_ID_CRYPTO        1
#define ION_EXYNOS_HEAP_ID_VIDEO_STREAM  3
#define ION_EXYNOS_HEAP_ID_VIDEO_FRAME   5
#define ION_EXYNOS_HEAP_ID_VIDEO_SCALER  6
#define ION_EXYNOS_HEAP_ID_GPU_CRC       8
#define ION_EXYNOS_HEAP_ID_GPU_BUFFER    9
#define ION_EXYNOS_HEAP_ID_CAMERA        10
#define ION_EXYNOS_HEAP_ID_SECURE_CAMERA 11
#define ION_EXYNOS_HEAP_ID_EXT_UI        13

#define EXYNOS_ION_HEAP_SYSTEM_MASK         (1 << ION_EXYNOS_HEAP_ID_SYSTEM)
#define EXYNOS_ION_HEAP_CRYPTO_MASK         (1 << ION_EXYNOS_HEAP_ID_CRYPTO)
#define EXYNOS_ION_HEAP_VIDEO_STREAM_MASK   (1 << ION_EXYNOS_HEAP_ID_VIDEO_STREAM)
#define EXYNOS_ION_HEAP_VIDEO_FRAME_MASK    (1 << ION_EXYNOS_HEAP_ID_VIDEO_FRAME)
#define EXYNOS_ION_HEAP_VIDEO_SCALER_MASK   (1 << ION_EXYNOS_HEAP_ID_VIDEO_SCALER)
#define EXYNOS_ION_HEAP_GPU_CRC_MASK        (1 << ION_EXYNOS_HEAP_ID_GPU_CRC)
#define EXYNOS_ION_HEAP_GPU_BUFFER_MASK     (1 << ION_EXYNOS_HEAP_ID_GPU_BUFFER)
#define EXYNOS_ION_HEAP_CAMERA_MASK         (1 << ION_EXYNOS_HEAP_ID_CAMERA)
#define EXYNOS_ION_HEAP_SECURE_CAMERA_MASK  (1 << ION_EXYNOS_HEAP_ID_SECURE_CAMERA)
#define EXYNOS_ION_HEAP_EXT_UI_MASK         (1 << ION_EXYNOS_HEAP_ID_EXT_UI)

#define ION_FLAG_CACHED 1
#define ION_FLAG_CACHED_NEEDS_SYNC 2 /* deprecated in the modern ion */
#define ION_FLAG_NOZEROED 8
#define ION_FLAG_PROTECTED 16
#define ION_FLAG_SYNC_FORCE 32
#define ION_FLAG_MAY_HWRENDER 64

#define ION_SYNC_READ      (1 << 0)
#define ION_SYNC_WRITE     (2 << 0)

__BEGIN_DECLS

int exynos_ion_open();
int exynos_ion_close(int fd);
int exynos_ion_alloc(int ion_fd, size_t len,
                      unsigned int heap_mask, unsigned int flags);
int exynos_ion_import_handle(int ion_fd, int fd, int* handle);
int exynos_ion_free_handle(int ion_fd, int handle);
int exynos_ion_sync_fd(int ion_fd, int fd);
int exynos_ion_sync_fd_partial(int ion_fd, int fd, off_t offset, size_t len);

int exynos_ion_sync_start(int ion_fd, int fd, int direction);
int exynos_ion_sync_end(int ion_fd, int fd, int direction);

const char *exynos_ion_get_heap_name(unsigned int legacy_heap_id);

int exynos_ion_dma_buf_track(int fd);
int exynos_ion_dma_buf_untrack(int fd);

__END_DECLS

#endif /* __HARDWARE_EXYNOS_ION_H__ */
