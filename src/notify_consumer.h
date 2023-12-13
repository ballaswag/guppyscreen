#ifndef __NOTIFY_CONSUMER_H__
#define __NOTIFY_CONSUMER_H__

#include "json.hpp"

using json = nlohmann::json;

class NotifyConsumer {
 public:
  NotifyConsumer(std::mutex &lv_lock);
  ~NotifyConsumer();
  virtual void consume(nlohmann::json &data) = 0;
  // virtual void consume(std::string &str) = 0;
 protected:
  std::mutex &lv_lock;
};

#endif // __NOTIFY_CONSUMER_H__
