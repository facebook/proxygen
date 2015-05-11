/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include <list>
#include <map>
#include <proxygen/lib/http/session/HTTP2PriorityQueue.h>

using namespace std::placeholders;

namespace proxygen {

typedef std::list<std::pair<HTTPCodec::StreamID, uint8_t>> IDList;

class QueueTest : public testing::Test {
 public:
  QueueTest() {}

 protected:
  void addTransaction(HTTPCodec::StreamID id, http2::PriorityUpdate pri) {
    HTTP2PriorityQueue::Handle h = q_.addTransaction(id, pri, nullptr);
    handles_.insert(std::make_pair(id, h));
  }

  void removeTransaction(HTTPCodec::StreamID id) {
    q_.removeTransaction(handles_[id]);
  }

  void updatePriority(HTTPCodec::StreamID id, http2::PriorityUpdate pri) {
    handles_[id] = q_.updatePriority(handles_[id], pri);
  }

  void signalEgress(HTTPCodec::StreamID id, bool mark) {
    if (mark) {
      q_.signalPendingEgress(handles_[id]);
    } else {
      q_.clearPendingEgress(handles_[id]);
    }
  }

  void buildSimpleTree() {
    addTransaction(1, {0, false, 16});
    addTransaction(3, {1, false, 4});
    addTransaction(5, {1, false, 4});
    addTransaction(7, {1, false, 8});
    addTransaction(9, {5, false, 8});
  }

  bool visitNode(HTTPCodec::StreamID id, HTTPTransaction* txn, double r) {
    nodes_.push_back(std::make_pair(id, r * 100));
    return false;
  }

  void dump() {
    nodes_.clear();
    q_.iterate(std::bind(&QueueTest::visitNode, this, _1, _2, _3),
               [] { return false; }, true);
  }

  void dumpBFS() {
    nodes_.clear();
    q_.iterateBFS(std::bind(&QueueTest::visitNode, this, _1, _2, _3),
                  [] { return false; }, true);
  }

  HTTP2PriorityQueue q_;
  std::map<HTTPCodec::StreamID, HTTP2PriorityQueue::Handle> handles_;
  IDList nodes_;
};


TEST_F(QueueTest, Basic) {
  buildSimpleTree();
  dump();
  EXPECT_EQ(nodes_, IDList({{1, 100}, {3, 25}, {5, 25}, {9, 100}, {7, 50}}));
}

TEST_F(QueueTest, RemoveLeaf) {
  buildSimpleTree();

  removeTransaction(3);
  dump();

  EXPECT_EQ(nodes_, IDList({{1, 100}, {5, 33}, {9, 100}, {7, 66}}));
}

TEST_F(QueueTest, RemoveParent) {
  buildSimpleTree();

  removeTransaction(5);
  dump();

  EXPECT_EQ(nodes_, IDList({{1, 100}, {3, 20}, {7, 40}, {9, 40}}));
}

TEST_F(QueueTest, UpdateWeight) {
  buildSimpleTree();

  updatePriority(5, {1, false, 8});
  dump();

  EXPECT_EQ(nodes_, IDList({{1, 100}, {3, 20}, {5, 40}, {9, 100}, {7, 40}}));
}

TEST_F(QueueTest, UpdateWeightExcl) {
  buildSimpleTree();

  updatePriority(5, {1, true, 8});
  dump();

  EXPECT_EQ(nodes_, IDList({{1, 100}, {5, 100}, {9, 40}, {3, 20}, {7, 40}}));
}

TEST_F(QueueTest, UpdateParentSibling) {
  buildSimpleTree();

  updatePriority(5, {3, false, 4});
  dump();

  EXPECT_EQ(nodes_, IDList({{1, 100}, {3, 33}, {5, 100},
                               {9, 100}, {7, 66}}));
}

TEST_F(QueueTest, UpdateParentSiblingExcl) {
  buildSimpleTree();

  updatePriority(7, {5, true, 4});
  dump();

  EXPECT_EQ(nodes_, IDList({{1, 100}, {3, 50}, {5, 50},
                              {7, 100}, {9, 100}}));
}

TEST_F(QueueTest, UpdateParentAncestor) {
  buildSimpleTree();

  updatePriority(9, {0, false, 16});
  dump();

  EXPECT_EQ(nodes_, IDList({{1, 50}, {3, 25}, {5, 25}, {7, 50}, {9, 50}}));
}

TEST_F(QueueTest, UpdateParentAncestorExcl) {
  buildSimpleTree();

  updatePriority(9, {0, true, 16});
  dump();

  EXPECT_EQ(nodes_, IDList({{9, 100}, {1, 100}, {3, 25}, {5, 25}, {7, 50}}));
}

TEST_F(QueueTest, UpdateParentDescendant) {
  buildSimpleTree();

  updatePriority(1, {5, false, 8});
  dump();

  EXPECT_EQ(nodes_, IDList({{5, 100}, {9, 50}, {1, 50}, {3, 33}, {7, 66}}));
}

TEST_F(QueueTest, UpdateParentDescendantExcl) {
  buildSimpleTree();

  updatePriority(1, {5, true, 8});
  dump();

  EXPECT_EQ(nodes_, IDList({{5, 100}, {1, 100}, {3, 20}, {7, 40}, {9, 40}}));
}

TEST_F(QueueTest, ExclusiveAdd) {
  buildSimpleTree();

  addTransaction(11, {1, true, 100});

  dump();
  EXPECT_EQ(nodes_, IDList({
        {1, 100}, {11, 100}, {3, 25}, {5, 25}, {9, 100}, {7, 50}
      }));
}

TEST_F(QueueTest, AddUnknown) {
  buildSimpleTree();

  addTransaction(11, {75, false, 16});

  dump();
  EXPECT_EQ(nodes_, IDList({
        {1, 50}, {3, 25}, {5, 25}, {9, 100}, {7, 50}, {11, 50}
      }));
}

TEST_F(QueueTest, Misc) {
  buildSimpleTree();

  signalEgress(1, true);
  EXPECT_FALSE(q_.empty());
  EXPECT_EQ(q_.numPendingEgress(), 1);
  signalEgress(1, false);
  EXPECT_EQ(q_.numPendingEgress(), 0);
  EXPECT_TRUE(q_.empty());
  removeTransaction(9);
  removeTransaction(1);
  dump();
  EXPECT_EQ(nodes_, IDList({{3, 25}, {5, 25}, {7, 50}}));
}

TEST_F(QueueTest, BreadthFirst) {
  buildSimpleTree();

  dumpBFS();
  EXPECT_EQ(nodes_, IDList({{1, 100}, {3, 25}, {5, 25}, {7, 50}, {9, 100}}));
}


}
