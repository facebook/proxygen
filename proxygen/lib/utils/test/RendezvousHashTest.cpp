/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Conv.h>
#include <folly/Foreach.h>
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <vector>

#include <proxygen/lib/utils/RendezvousHash.h>

using namespace proxygen;

TEST(RendezvousHash, Consistency) {
  RendezvousHash hashes;
  for (int i = 0; i < 10; ++i) {
    hashes.insert(folly::to<std::string>("key", i), 1);
  }

  std::map<std::string, size_t> mapping;
  for (int i = 0; i < 10000; ++i) {
    std::string s = folly::to<std::string>(i);
    mapping[s] = hashes.get(s).second;
  }

  FOR_EACH_KV (key, expected, mapping) {
    EXPECT_EQ(expected, hashes.get(key).second);
  }
}

TEST(RendezvousHash, ConsistencyWithNewNode) {
  RendezvousHash hashes;
  int nodes = 10;
  for (int i = 0; i < nodes; ++i) {
    hashes.insert(folly::to<std::string>("key", i), 1);
  }

  std::map<std::string, size_t> mapping;
  for (int i = 0; i < 10000; ++i) {
    std::string s = folly::to<std::string>(i);
    mapping[s] = hashes.get(s).second;
  }

  // Adding a new node
  hashes.insert(folly::to<std::string>("key", nodes), 1);

  // traffic should only flow to the new node
  FOR_EACH_KV (key, expected, mapping) {
    size_t id = hashes.get(key).second;
    EXPECT_TRUE(expected == id || nodes == int(id));
  }
}

TEST(RendezvousHash, ConsistencyWithIncreasedWeight) {
  RendezvousHash hashes_before;
  int nodes = 10;
  for (int i = 0; i < nodes; ++i) {
    hashes_before.insert(folly::to<std::string>("key", i), i);
  }

  std::map<std::string, size_t> mapping;
  for (int i = 0; i < 10000; ++i) {
    std::string s = folly::to<std::string>(i);
    mapping[s] = hashes_before.get(s).second;
  }

  // Increase the weight by 2
  RendezvousHash hashes_after;

  for (int i = 0; i < nodes; ++i) {
    hashes_after.insert(folly::to<std::string>("key", i), i*2);
  }

  // traffic shouldn't flow at all
  FOR_EACH_KV (key, expected, mapping) {
    EXPECT_EQ(expected, hashes_after.get(key).second);
  }
}

TEST(RendezvousHash, ConsistentFlowToIncreasedWeightNode) {
  RendezvousHash hashes_before;
  int nodes = 10;
  for (int i = 0; i < nodes; ++i) {
    hashes_before.insert(folly::to<std::string>("key", i), i);
  }

  std::map<std::string, size_t> mapping;
  for (int i = 0; i < 10000; ++i) {
    std::string s = folly::to<std::string>(i);
    mapping[s] = hashes_before.get(s).second;
  }

  // Increase the weight for a single node
  RendezvousHash hashes_after;

  hashes_after.insert(folly::to<std::string>("key", 0), 10);

  for (int i = 1; i < nodes; ++i) {
    hashes_after.insert(folly::to<std::string>("key", i), i);
  }

  // traffic should only flow to the first node
  FOR_EACH_KV (key, expected, mapping) {
    size_t id = hashes_after.get(key).second;
    EXPECT_TRUE(expected == id || 0 == int(id));
  }
}

TEST(RendezvousHash, ConsistentFlowToDecreasedWeightNodes) {
  RendezvousHash hashes_before;
  int nodes = 18;
  for (int i = 0; i < nodes; ++i) {
    hashes_before.insert(folly::to<std::string>("key", i), 100);
  }

  std::map<std::string, size_t> mapping;
  for (int i = 0; i < 10000; ++i) {
    std::string s = folly::to<std::string>(i);
    mapping[s] = hashes_before.get(s).second;
  }


  RendezvousHash hashes_after;

  // decrease the weights for 5 nodes
  for (int i = 0; i < 5; ++i) {
    hashes_after.insert(folly::to<std::string>("key", i), 50);
  }

  // keep the weights for the rest unchanged
  for (int i = 5; i < nodes; ++i) {
    hashes_after.insert(folly::to<std::string>("key", i), 100);
  }


  FOR_EACH_KV (key, expected, mapping) {
    // traffic should only flow to nodes with decreased nodes
    size_t id = hashes_after.get(key).second;
    EXPECT_TRUE(expected == id || id >= 5);
  }
}

TEST(RendezvousHash, ConsistentFlowToDecreasedWeightNode) {
  RendezvousHash hashes_before;
  int nodes = 10;
  for (int i = 0; i < nodes; ++i) {
    hashes_before.insert(folly::to<std::string>("key", i), i);
  }

  std::map<std::string, size_t> mapping;
  for (int i = 0; i < 10000; ++i) {
    std::string s = folly::to<std::string>(i);
    mapping[s] = hashes_before.get(s).second;
  }

  // Increase the weight for a single node
  RendezvousHash hashes_after;

  for (int i = 0; i < nodes - 1; ++i) {
    hashes_after.insert(folly::to<std::string>("key", i), i);
  }

  // zero the weight of the last node
  hashes_after.insert(folly::to<std::string>("key", nodes-1), 0);

  FOR_EACH_KV (key, expected, mapping) {
    // traffic should only flow from the zero weight cluster to others
    size_t id = hashes_after.get(key).second;
    if (expected == (uint64_t)nodes-1) {
       EXPECT_TRUE(expected != id);
    } else {
       EXPECT_TRUE(expected == id);
    }
  }
}

TEST(RendezvousHash, ConsistencyWithDecreasedWeight) {
  RendezvousHash hashes_before;
  int nodes = 10;
  for (int i = 0; i < nodes; ++i) {
    hashes_before.insert(folly::to<std::string>("key", i), i*2);
  }

  std::map<std::string, size_t> mapping;
  for (int i = 0; i < 10000; ++i) {
    std::string s = folly::to<std::string>(i);
    mapping[s] = hashes_before.get(s).second;
  }

  // Decrease the weight by 2
  RendezvousHash hashes_after;

  for (int i = 0; i < nodes; ++i) {
    hashes_after.insert(folly::to<std::string>("key", i), i);
  }

  // traffic shouldn't flow at all
  FOR_EACH_KV (key, expected, mapping) {
    EXPECT_EQ(expected, hashes_after.get(key).second);
  }
}

TEST(ConsistentHashRing, DistributionAccuracy) {
  std::vector<std::string> keys =
    {"ash_proxy", "prn_proxy", "snc_proxy", "frc_proxy"};

  std::vector<std::vector<uint64_t>> weights = {
    {248, 342, 2, 384},
    {10, 10, 10, 10},
    {25, 25, 10, 10},
    {100, 10, 10, 1},
    {100, 5, 5, 5},
    {922337203685, 12395828300, 50192385101, 59293845010}
  };

  for (auto& weight: weights) {
    RendezvousHash hash;

    FOR_EACH_RANGE (i, 0, keys.size()) {
      hash.insert(keys[i], weight[i]);
    }

    std::vector<uint64_t> distribution(keys.size());

    for (uint64_t i = 0; i < 21000; ++i) {
      distribution[hash.get(i).second]++;
    }

    uint64_t totalWeight = 0;

    for (auto& w: weight) {
      totalWeight += w;
    }

    double maxError = 0.0;
    for (size_t i = 0; i < keys.size(); ++i) {
      double expected = 100.0 * weight[i] / totalWeight;
      double actual = 100.0 * distribution[i] / 21000;

      maxError = std::max(maxError, fabs(expected - actual));
    }
    // make sure the error rate is less than 1.0%
    EXPECT_LE(maxError, 1.0);
  }
}
