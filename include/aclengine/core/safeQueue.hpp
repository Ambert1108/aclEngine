// @brief: 线程安全队列
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.6.17]
// @version: V0.0.1
// @revision: [xxx@2024.6.17]

#pragma once

#include <mutex>
#include <queue>

template<typename T>
class SafeQueue {
public:

  /**
   * @brief SafeQueue constructor
   * @param [in] capacity: the queue capacity
   */
  SafeQueue(uint32_t capacity)
  {
    // check the input value: capacity is valid
    if (capacity >= kMinQueueCapacity && capacity <= kMaxQueueCapacity) {
      queueCapacity = capacity;
    }
    else { // the input value: capacity is invalid, set the default value
      queueCapacity = kDefaultQueueCapacity;
    }
  }

  /**
   * @brief ThreadSafeQueue constructor
   */
  SafeQueue()
  {
    queueCapacity = kDefaultQueueCapacity;
  }

  /**
   * @brief ThreadSafeQueue destructor
   */
  ~SafeQueue() = default;

  /**
   * @brief push data to queue
   * @param [in] input_value: the value will push to the queue
   * @return true: success to push data; false: fail to push data
   */
  bool Push(T input_value)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    // check current size is less than capacity
    if (queue_.size() < queueCapacity) {
      queue_.push(input_value);
      return true;
    }

    return false;
  }

  /**
   * @brief pop data from queue
   * @return true: success to pop data; false: fail to pop data
   */
  T Pop()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) { // check the queue is empty
      T tmp;
      return tmp;
    }

    T tmp_ptr = queue_.front();
    queue_.pop();
    return tmp_ptr;
  }

  /**
   * @brief check the queue is empty
   * @return true: the queue is empty; false: the queue is not empty
   */
  bool Empty()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
  }

  /**
   * @brief get the queue size
   * @return the queue size
   */
  uint32_t Size()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

  void ExtendCapacity(uint32_t newSize)
  {
    queueCapacity = newSize;
    kMaxQueueCapacity = newSize > kMaxQueueCapacity ? newSize : kMaxQueueCapacity;
  }

private:
  std::queue<T> queue_; // the queue
  uint32_t queueCapacity; // queue capacity
  mutable std::mutex mutex_; // the mutex value
  const uint32_t kMinQueueCapacity = 1; // the minimum queue capacity
  const uint32_t kMaxQueueCapacity = 10000; // the maximum queue capacity
  const uint32_t kDefaultQueueCapacity = 10; // default queue capacity
};