// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "media/base/producer_consumer_queue.h"

#include "base/bind.h"
#include "base/synchronization/waitable_event.h"
#include "media/base/closure_thread.h"
#include "media/base/status_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
const size_t kCapacity = 10u;
const int64 kTimeout = 100;  // 0.1s.

// Check that the |delta| is approximately |time_in_milliseconds|.
bool CheckTimeApproxEqual(int64 time_in_milliseconds,
                          const base::TimeDelta& delta) {
  const int64 kOverhead = 10;   // 0.01s.
  return delta.InMilliseconds() >= time_in_milliseconds &&
         delta.InMilliseconds() <= time_in_milliseconds + kOverhead;
}

}  // namespace

namespace media {

TEST(ProducerConsumerQueueTest, CheckEmpty) {
  ProducerConsumerQueue<int> queue(kUnlimitedCapacity);
  EXPECT_EQ(0u, queue.Size());
  EXPECT_TRUE(queue.Empty());
  EXPECT_EQ(0u, queue.HeadPos());
}

TEST(ProducerConsumerQueueTest, PushPop) {
  ProducerConsumerQueue<size_t> queue(kCapacity);
  for (size_t i = 0; i < kCapacity; ++i)
    ASSERT_OK(queue.Push(i, kInfiniteTimeout));

  EXPECT_EQ(kCapacity, queue.Size());
  EXPECT_FALSE(queue.Empty());
  EXPECT_EQ(0u, queue.HeadPos());
  EXPECT_EQ(kCapacity - 1, queue.TailPos());

  for (size_t i = 0; i < kCapacity; ++i) {
    size_t val;
    ASSERT_OK(queue.Pop(&val, kInfiniteTimeout));
    EXPECT_EQ(i, val);
    EXPECT_EQ(i + 1, queue.HeadPos());
  }
}

TEST(ProducerConsumerQueueTest, Peek) {
  ProducerConsumerQueue<size_t> queue(kCapacity);
  for (size_t i = 0; i < kCapacity; ++i)
    ASSERT_OK(queue.Push(i, kInfiniteTimeout));
  for (size_t i = 0; i < kCapacity; ++i) {
    size_t val;
    ASSERT_OK(queue.Peek(i, &val, kInfiniteTimeout));
    EXPECT_EQ(i, val);
    // Expect head position to move along with peek position.
    EXPECT_EQ(i >= kCapacity / 2 ? i - kCapacity / 2 : 0, queue.HeadPos());
  }
  EXPECT_EQ(kCapacity - 1, queue.TailPos());
}

TEST(ProducerConsumerQueueTest, PeekOnPoppedElement) {
  ProducerConsumerQueue<size_t> queue(kCapacity);
  for (size_t i = 0; i < kCapacity; ++i)
    ASSERT_OK(queue.Push(i, kInfiniteTimeout));
  size_t val;
  ASSERT_OK(queue.Pop(&val, kInfiniteTimeout));
  ASSERT_OK(queue.Push(kCapacity, kInfiniteTimeout));

  ASSERT_OK(queue.Peek(kCapacity, &val, kInfiniteTimeout));
  EXPECT_EQ(kCapacity, val);

  // Expect head position to move along with peek position.
  EXPECT_EQ(kCapacity / 2, queue.HeadPos());
  ASSERT_OK(queue.Peek(kCapacity / 2, &val, kInfiniteTimeout));
  EXPECT_EQ(kCapacity / 2, val);

  ASSERT_EQ(error::INVALID_ARGUMENT,
            queue.Peek(kCapacity / 2 - 2, &val, kInfiniteTimeout).error_code());
}

TEST(ProducerConsumerQueueTest, PushWithTimeout) {
  scoped_ptr<base::ElapsedTimer> timer;
  ProducerConsumerQueue<int> queue(kCapacity);

  for (size_t i = 0; i < kCapacity; ++i) {
    timer.reset(new base::ElapsedTimer());
    ASSERT_OK(queue.Push(i, kTimeout));
    // Expect Push to return instantly without waiting.
    EXPECT_TRUE(CheckTimeApproxEqual(0, timer->Elapsed()));
  }

  timer.reset(new base::ElapsedTimer());
  ASSERT_EQ(error::TIME_OUT, queue.Push(0, kTimeout).error_code());
  // Expect elapsed time exceeds defined timeout.
  EXPECT_TRUE(CheckTimeApproxEqual(kTimeout, timer->Elapsed()));
}

TEST(ProducerConsumerQueueTest, PopWithTimeout) {
  scoped_ptr<base::ElapsedTimer> timer;
  ProducerConsumerQueue<size_t> queue(kCapacity);

  for (size_t i = 0; i < kCapacity; ++i)
    ASSERT_OK(queue.Push(i, kInfiniteTimeout));

  size_t val;
  for (size_t i = 0; i < kCapacity; ++i) {
    timer.reset(new base::ElapsedTimer());
    ASSERT_OK(queue.Pop(&val, kTimeout));
    // Expect Pop to return instantly without waiting.
    EXPECT_TRUE(CheckTimeApproxEqual(0, timer->Elapsed()));
    EXPECT_EQ(i, val);
  }

  timer.reset(new base::ElapsedTimer());
  ASSERT_EQ(error::TIME_OUT, queue.Pop(&val, kTimeout).error_code());
  // Expect elapsed time exceeds defined timeout.
  EXPECT_TRUE(CheckTimeApproxEqual(kTimeout, timer->Elapsed()));
}

TEST(ProducerConsumerQueueTest, PeekWithTimeout) {
  scoped_ptr<base::ElapsedTimer> timer;
  ProducerConsumerQueue<size_t> queue(kCapacity);

  for (size_t i = 0; i < kCapacity; ++i)
    ASSERT_OK(queue.Push(i, kInfiniteTimeout));

  size_t val;
  timer.reset(new base::ElapsedTimer());
  ASSERT_EQ(error::TIME_OUT,
            queue.Peek(kCapacity, &val, kTimeout).error_code());
  // Expect elapsed time exceeds defined timeout.
  EXPECT_TRUE(CheckTimeApproxEqual(kTimeout, timer->Elapsed()));

  for (size_t i = kCapacity / 2; i < kCapacity; ++i) {
    timer.reset(new base::ElapsedTimer());
    ASSERT_OK(queue.Peek(i, &val, kTimeout));
    // Expect Peek to return instantly without waiting.
    EXPECT_TRUE(CheckTimeApproxEqual(0, timer->Elapsed()));
    EXPECT_EQ(i, val);
  }
}

TEST(ProducerConsumerQueueTest, CheckStop) {
  scoped_ptr<base::ElapsedTimer> timer;
  ProducerConsumerQueue<int> queue(kUnlimitedCapacity);

  ASSERT_FALSE(queue.Stopped());
  queue.Stop();
  ASSERT_TRUE(queue.Stopped());

  EXPECT_EQ(error::STOPPED, queue.Push(0, kInfiniteTimeout).error_code());

  timer.reset(new base::ElapsedTimer());
  EXPECT_EQ(error::STOPPED, queue.Push(0, kTimeout).error_code());
  // Expect Push to return instantly without waiting.
  EXPECT_TRUE(CheckTimeApproxEqual(0, timer->Elapsed()));

  int val;
  EXPECT_EQ(error::STOPPED, queue.Pop(&val, kInfiniteTimeout).error_code());
  timer.reset(new base::ElapsedTimer());
  EXPECT_EQ(error::STOPPED, queue.Pop(&val, kTimeout).error_code());
  // Expect Pop to return instantly without waiting.
  EXPECT_TRUE(CheckTimeApproxEqual(0, timer->Elapsed()));

  EXPECT_EQ(error::STOPPED, queue.Peek(0, &val, kInfiniteTimeout).error_code());
  timer.reset(new base::ElapsedTimer());
  EXPECT_EQ(error::STOPPED, queue.Peek(0, &val, kTimeout).error_code());
  // Expect Pop to return instantly without waiting.
  EXPECT_TRUE(CheckTimeApproxEqual(0, timer->Elapsed()));
}

class MultiThreadProducerConsumerQueueTest : public ::testing::Test {
 public:
  MultiThreadProducerConsumerQueueTest()
      : thread_("My Push Thread",
                base::Bind(&MultiThreadProducerConsumerQueueTest::PushTask,
                           base::Unretained(this))),
        queue_(kCapacity) {}
  virtual ~MultiThreadProducerConsumerQueueTest() {}

 protected:
  virtual void SetUp() OVERRIDE { thread_.Start(); }
  virtual void TearDown() OVERRIDE { thread_.Join(); }

  void PushTask() {
    int val = 0;
    // Push elements to the queue until stopped.
    while (queue_.Push(val, kInfiniteTimeout).ok())
      ++val;
  }

  void SleepUntilQueueIsFull() {
    const size_t kMaxNumLoopsWaiting = 1000;
    const size_t kSleepDurationInMillisecondsPerLoop = 10;

    for (size_t i = 0; i < kMaxNumLoopsWaiting; i++) {
      if (queue_.Size() >= kCapacity)
        break;
      base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(
          kSleepDurationInMillisecondsPerLoop));
    }
  }

  ClosureThread thread_;
  ProducerConsumerQueue<size_t> queue_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MultiThreadProducerConsumerQueueTest);
};

TEST_F(MultiThreadProducerConsumerQueueTest, Pop) {
  // Perform a number of pops.
  size_t val;
  size_t i = 0;
  for (; i < kCapacity * 3; ++i) {
    ASSERT_OK(queue_.Pop(&val, kInfiniteTimeout));
    EXPECT_EQ(i, val);
  }

  // Wait until the queue is full. The size of the queue should be kCapacity
  // exactly.
  SleepUntilQueueIsFull();
  EXPECT_EQ(kCapacity, queue_.Size());

  queue_.Stop();

  // Should still have kCapacity elements before STOPPED being returned.
  for (size_t j = 0; j < kCapacity; ++j) {
    ASSERT_OK(queue_.Pop(&val, kInfiniteTimeout));
    EXPECT_EQ(i + j, val);
  }
  ASSERT_EQ(error::STOPPED, queue_.Pop(&val, kInfiniteTimeout).error_code());
}

TEST_F(MultiThreadProducerConsumerQueueTest, Peek) {
  const size_t kPositionOne = 25u;
  const size_t kPositionTwo = 88u;

  EXPECT_EQ(0u, queue_.HeadPos());

  size_t val;
  ASSERT_OK(queue_.Peek(kPositionOne, &val, kInfiniteTimeout));
  EXPECT_EQ(kPositionOne, val);
  EXPECT_EQ(kPositionOne - kCapacity / 2, queue_.HeadPos());

  ASSERT_OK(queue_.Peek(kPositionTwo, &val, kInfiniteTimeout));
  EXPECT_EQ(kPositionTwo, val);
  EXPECT_EQ(kPositionTwo - kCapacity / 2, queue_.HeadPos());

  // Wait until the queue is full. The size of the queue should be kCapacity
  // exactly.
  SleepUntilQueueIsFull();
  EXPECT_EQ(kCapacity, queue_.Size());

  queue_.Stop();
  EXPECT_EQ(kPositionTwo - kCapacity / 2, queue_.HeadPos());
  EXPECT_EQ(kPositionTwo + kCapacity / 2 - 1, queue_.TailPos());

  ASSERT_EQ(error::STOPPED,
            queue_.Peek(kPositionTwo + kCapacity, &val, kInfiniteTimeout)
                .error_code());
  // Head will be moved pass Tail and the queue is expected to be empty.
  EXPECT_EQ(kPositionTwo + kCapacity / 2, queue_.HeadPos());
  EXPECT_EQ(kPositionTwo + kCapacity / 2 - 1, queue_.TailPos());
  EXPECT_TRUE(queue_.Empty());
}

TEST_F(MultiThreadProducerConsumerQueueTest, PeekOnLargePosition) {
  base::ElapsedTimer timer;
  const size_t kVeryLargePosition = 88888888u;

  size_t val;
  ASSERT_EQ(error::TIME_OUT,
            queue_.Peek(kVeryLargePosition, &val, 0).error_code());
  EXPECT_TRUE(CheckTimeApproxEqual(0, timer.Elapsed()));

  ASSERT_EQ(error::TIME_OUT,
            queue_.Peek(kVeryLargePosition, &val, kTimeout).error_code());
  EXPECT_TRUE(CheckTimeApproxEqual(kTimeout, timer.Elapsed()));

  queue_.Stop();
}

namespace {
enum Operation {
  kPush,
  kPop,
  kPeek,
};
}  // namespace

class MultiThreadProducerConsumerQueueStopTest
    : public ::testing::TestWithParam<Operation> {
 public:
  MultiThreadProducerConsumerQueueStopTest() : queue_(1), event_(true, false) {}
  virtual ~MultiThreadProducerConsumerQueueStopTest() {}

 public:
  void ClosureTask(Operation op) {
    int val = 0;
    switch (op) {
      case kPush:
        // The queue was setup with size 1. The first push will return STOPPED
        // if Stop() has been called; otherwise it should return OK and the
        // second push will block until Stop() being called.
        status_ = queue_.Push(0, kInfiniteTimeout);
        if (status_.ok())
          status_ = queue_.Push(0, kInfiniteTimeout);
        break;
      case kPop:
        status_ = queue_.Pop(&val, kInfiniteTimeout);
        break;
      case kPeek:

        status_ = queue_.Peek(0, &val, kInfiniteTimeout);
        break;
      default:
        NOTREACHED();
    }
    event_.Signal();
  }

 protected:
  ProducerConsumerQueue<int> queue_;
  base::WaitableEvent event_;

 private:
  Status status_;

  DISALLOW_COPY_AND_ASSIGN(MultiThreadProducerConsumerQueueStopTest);
};

// Verify that Stop stops Push/Pop/Peek operations and return immediately.
TEST_P(MultiThreadProducerConsumerQueueStopTest, StopTests) {
  Operation op = GetParam();
  ClosureThread thread(
      "My Thread",
      base::Bind(&MultiThreadProducerConsumerQueueStopTest::ClosureTask,
                 base::Unretained(this),
                 op));
  thread.Start();

  base::ElapsedTimer timer;
  ASSERT_TRUE(!event_.IsSignaled());
  queue_.Stop();
  event_.Wait();
  // Expect Stop to stop the operations immediately.
  EXPECT_TRUE(CheckTimeApproxEqual(0, timer.Elapsed()));

  thread.Join();
}

INSTANTIATE_TEST_CASE_P(Operations,
                        MultiThreadProducerConsumerQueueStopTest,
                        ::testing::Values(kPush, kPop, kPeek));

}  // namespace media
