#include "queue.hpp"

#include <gtest/gtest.h>
#include <memory>

TEST(SafeQueue, DropNewestRejectsWhenFull) {
    SafeQueue<int> q(2);
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_FALSE(q.push(3)); // rejected: full
    EXPECT_EQ(q.dropCount(), 1u);
    EXPECT_EQ(q.pop(), 1); // oldest kept
    EXPECT_EQ(q.pop(), 2);
}

TEST(SafeQueue, DropOldestEvictsFront) {
    SafeQueue<int> q(2, SafeQueue<int>::Overflow::DropOldest);
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3)); // evicts 1: consumer keeps the freshest
    EXPECT_EQ(q.dropCount(), 1u);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_EQ(q.pop(), 3);
}

TEST(SafeQueue, ClosedPushRejectedButNotCounted) {
    SafeQueue<int> q(2);
    EXPECT_TRUE(q.push(1));
    q.close();
    EXPECT_FALSE(q.push(2)); // shutdown race: rejected, not an overload drop
    EXPECT_EQ(q.dropCount(), 0u);
    EXPECT_EQ(q.pop(), 1); // drains before signalling end
    EXPECT_EQ(q.pop(), 0); // default-constructed sentinel
}

TEST(SafeQueue, MoveOnlyItems) {
    using Queue = SafeQueue<std::unique_ptr<int>>;
    Queue q(2, Queue::Overflow::DropOldest);
    EXPECT_TRUE(q.push(std::make_unique<int>(7)));
    auto item = q.pop();
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(*item, 7);
    q.close();
    EXPECT_EQ(q.pop(), nullptr);
}
