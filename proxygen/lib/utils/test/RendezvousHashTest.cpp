/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Conv.h>
#include <folly/Foreach.h>
#include <folly/portability/GTest.h>
#include <map>
#include <vector>

#include <proxygen/lib/utils/RendezvousHash.h>

using namespace proxygen;

TEST(RendezvousHash, Consistency) {
  RendezvousHash hashes;
  std::vector<std::pair<std::string, uint64_t> > nodes;
  for (int i = 0; i < 10; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), 1);
  }
  hashes.build(nodes);

  std::map<uint64_t, size_t> mapping;
  for (int i = 0; i < 10000; ++i) {
    mapping[i] = hashes.get(i);
  }

  FOR_EACH_KV (key, expected, mapping) {
    EXPECT_EQ(expected, hashes.get(key));
  }
}

TEST(RendezvousHash, ConsistencyWithNewNode) {
  RendezvousHash hashes;
  int numNodes = 10;
  std::vector<std::pair<std::string, uint64_t> > nodes;
  for (int i = 0; i < numNodes; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), 1);
  }
  hashes.build(nodes);
  std::map<uint64_t, size_t> mapping;
  for (uint64_t i = 0; i < 10000; ++i) {
    mapping[i] = hashes.get(i);
  }
  hashes = RendezvousHash();
  // Adding a new node and rebuild the hash
  nodes.emplace_back(folly::to<std::string>("key", numNodes), 1);
  hashes.build(nodes);
  // traffic should only flow to the new node
  FOR_EACH_KV (key, expected, mapping) {
    size_t id = hashes.get(key);
    EXPECT_TRUE(expected == id || numNodes == int(id));
  }
}

TEST(RendezvousHash, ConsistencyWithIncreasedWeight) {
  RendezvousHash hashes;
  int numNodes = 10;
  std::vector<std::pair<std::string, uint64_t> > nodes;
  for (int i = 0; i < numNodes; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), i);
  }
  hashes.build(nodes);

  std::map<uint64_t, size_t> mapping;
  for (uint64_t i = 0; i < 10000; ++i) {
    mapping[i] = hashes.get(i);
  }

  // Increase the weight by 2
  nodes.clear();
  hashes = RendezvousHash();
  for (int i = 0; i < numNodes; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), i*2);
  }
  hashes.build(nodes);

  // traffic shouldn't flow at all
  FOR_EACH_KV (key, expected, mapping) {
    EXPECT_EQ(expected, hashes.get(key));
  }
}

TEST(RendezvousHash, ConsistentFlowToIncreasedWeightNode) {
  RendezvousHash hashes;
  int numNodes = 10;
  std::vector<std::pair<std::string, uint64_t> > nodes;
  for (int i = 0; i < numNodes; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), i);
  }
  hashes.build(nodes);

  std::map<uint64_t, size_t> mapping;
  for (uint64_t i = 0; i < 10000; ++i) {
    mapping[i] = hashes.get(i);
  }

  nodes.clear();
  // Increase the weight for a single node
  hashes = RendezvousHash();

  nodes.emplace_back(folly::to<std::string>("key", 0), 10);

  for (int i = 1; i < numNodes; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), i);
  }
  hashes.build(nodes);
  // traffic should only flow to the first node
  FOR_EACH_KV (key, expected, mapping) {
    size_t id = hashes.get(key);
    EXPECT_TRUE(expected == id || 0 == int(id));
  }
}

TEST(RendezvousHash, ConsistentFlowToDecreasedWeightNodes) {
  RendezvousHash hashes;
  int numNodes = 18;
  std::vector<std::pair<std::string, uint64_t> > nodes;
  for (int i = 0; i < numNodes; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), 100);
  }
  hashes.build(nodes);
  std::map<uint64_t, size_t> mapping;
  for (uint64_t i = 0; i < 10000; ++i) {
    mapping[i] = hashes.get(i);
  }

  nodes.clear();
  hashes = RendezvousHash();

  // decrease the weights for 5 nodes
  for (int i = 0; i < 5; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), 50);
  }

  // keep the weights for the rest unchanged
  for (int i = 5; i < numNodes; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), 100);
  }

  hashes.build(nodes);
  FOR_EACH_KV (key, expected, mapping) {
    // traffic should only flow to nodes with decreased nodes
    size_t id = hashes.get(key);
    EXPECT_TRUE(expected == id || id >= 5);
  }
}

TEST(RendezvousHash, ConsistentFlowToDecreasedWeightNode) {
  RendezvousHash hashes;
  int numNodes = 10;
  std::vector<std::pair<std::string, uint64_t> > nodes;
  for (int i = 0; i < numNodes; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), i);
  }
  hashes.build(nodes);
  std::map<uint64_t, size_t> mapping;
  for (uint64_t i = 0; i < 10000; ++i) {
    mapping[i] = hashes.get(i);
  }

  // Increase the weight for a single node
  nodes.clear();
  hashes = RendezvousHash();

  for (int i = 0; i < numNodes - 1; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), i);
  }

  // zero the weight of the last node
  nodes.emplace_back(folly::to<std::string>("key", numNodes-1), 0);
  hashes.build(nodes);
  FOR_EACH_KV (key, expected, mapping) {
    // traffic should only flow from the zero weight cluster to others
    size_t id = hashes.get(key);
    if (expected == (uint64_t)numNodes-1) {
       EXPECT_TRUE(expected != id);
    } else {
       EXPECT_TRUE(expected == id);
    }
  }
}

TEST(RendezvousHash, ConsistencyWithDecreasedWeight) {
  RendezvousHash hashes;
  int numNodes = 10;
  std::vector<std::pair<std::string, uint64_t> > nodes;
  for (int i = 0; i < numNodes; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), i*2);
  }
  hashes.build(nodes);
  std::map<uint64_t, size_t> mapping;
  for (uint64_t i = 0; i < 10000; ++i) {
    mapping[i] = hashes.get(i);
  }

  // Decrease the weight by 2
  nodes.clear();
  hashes = RendezvousHash();

  for (int i = 0; i < numNodes; ++i) {
    nodes.emplace_back(folly::to<std::string>("key", i), i);
  }
  hashes.build(nodes);

  // traffic shouldn't flow at all
  FOR_EACH_KV (key, expected, mapping) {
    EXPECT_EQ(expected, hashes.get(key));
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
    RendezvousHash hashes;
    std::vector<std::pair<std::string, uint64_t> > nodes;
    FOR_EACH_RANGE (i, 0, keys.size()) {
      nodes.emplace_back(keys[i], weight[i]);
    }
    hashes.build(nodes);

    std::vector<uint64_t> distribution(keys.size());

    for (uint64_t i = 0; i < 21000; ++i) {
      distribution[hashes.get(i)]++;
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
