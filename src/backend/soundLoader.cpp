#include "stdafx.h"
#include "soundLoader.h"
#include "internal/internalObjects.h"
#include <sndfile.h>

boost::ptr_map<std::string, YSE::soundFile> YSE::SoundFiles;
Bool YSE::KeepLoading; // set in System.init and system.close
std::condition_variable YSE::LoadSoundCondition;
std::deque<void *> & YSE::LoadList() {
  static std::deque<void *> list;
  return list;
}

void YSE::LoadSoundFiles() {
  std::unique_lock<std::mutex> lock(LoadSoundMutex);

  while(KeepLoading) {
    {
      // parentheses above are important to free the lock below before putting the thread in wait condition
      YSE::lock l(SFMTX); // lock for file list
      while (YSE::LoadList().size()) {
        YSE::soundFile* d = (YSE::soundFile *)YSE::LoadList().front();
        YSE::LoadList().pop_front();

        if (sf_error(d->_file)) {
          Error.emit(E_LIBSNDFILE, sf_strerror(d->_file));
        } else {
          Int size = (Int)d->length() * d->channels();
          d->_buffer.resize(size);
	        Long read = sf_read_float(d->_file, d->_buffer.getBuffer(), size);
          if (read != size) {
            Error.emit(E_FILE_BYTE_COUNT);
            sf_close(d->_file);
          } else {
            if (sf_close(d->_file)) {
              Error.emit(E_LIBSNDFILE, sf_strerror(d->_file));
            } else {
	            d->_state = YSE::READY;
            }
          }
        }
      }
    }

    LoadSoundCondition.wait(lock);
  }

}
