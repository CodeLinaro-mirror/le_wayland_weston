/*
* Copyright (c) 2017, 2021 The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "sdm-service/sdm_display_debugger.h"
#include "sdm-service/sdm_display_buffer_sync_handler.h"
#include "utils/fence.h"
#include "libsync.h"

#define __CLASS__ "SdmDisplayBufferSyncHandler"

namespace sdm {

SdmDisplayBufferSyncHandler::SdmDisplayBufferSyncHandler() {
  Fence::Set(this);
  DLOGW("SdmDisplayBufferSyncHandler: set class point to fence");
}

int SdmDisplayBufferSyncHandler::SyncMerge(int fd1,
                                           int fd2,
                                           int *merged_fd) {
  DLOGI("SdmDisplayBufferSyncHandler::SyncMerge");
  // Caller owns fds, hence, if
  //  one of the fence fd is invalid, create dup of valid fd and set to merged fd.
  //  both fence fds are same, create dup of one of the fd and set to merged fd.
  *merged_fd = -1;
  if (fd1 < 0) {
    *merged_fd = dup(fd2);
  } else if ((fd2 < 0) || (fd1 == fd2)) {
    *merged_fd = dup(fd1);
  } else {
    *merged_fd = sync_merge("SyncMerge", fd1, fd2);
  }

  return kErrorNone;
}

int SdmDisplayBufferSyncHandler::SyncWait(int fd, int timeout)
{
  DLOGI("SdmDisplayBufferSyncHandler::SyncWait, fd = %d, timeout = %d", fd, timeout);
  // Assume invalid fd as signaled.
  if (fd < 0) {
    return 0;
  }

  int error = 0;
  if ((error = sync_wait(fd, timeout)) != 0) {
    if (errno == ETIME) {
      error = -ETIME;
    }
    if (timeout != 0) {
      DLOGW("sync_wait fd = %d, timeout = %d ms, err = %d : %s", fd, timeout, errno,
            strerror(errno));
    }
    return error;
  }

  return kErrorNone;
}

}  // namespace sdm


