#include <mutex>

#include "notify_consumer.h"

NotifyConsumer::NotifyConsumer(std::mutex &lock)
  : lv_lock(lock) {
}

NotifyConsumer::~NotifyConsumer() {
}
