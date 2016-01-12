/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTP2PriorityQueue.h>
#include <proxygen/lib/utils/UnionBasedStatic.h>

using std::list;
using std::unique_ptr;

namespace proxygen {

folly::ThreadLocalPtr<HTTP2PriorityQueue::NextEgressResult>
HTTP2PriorityQueue::nextEgressResults_;


HTTP2PriorityQueue::Node::Node(HTTP2PriorityQueue::Node* inParent,
                               HTTPCodec::StreamID id,
                               uint8_t weight, HTTPTransaction *txn)
    : parent_(inParent),
      id_(id),
      weight_(weight + 1),
      txn_(txn) {
}

// Add a new node as a child of this node
HTTP2PriorityQueue::Handle
HTTP2PriorityQueue::Node::emplaceNode(
  unique_ptr<HTTP2PriorityQueue::Node> node, bool exclusive) {
  CHECK(!node->isEnqueued());
  list<unique_ptr<Node>> children;
  if (exclusive) {
    // this->children become new node's children
    std::swap(children, children_);
    totalChildWeight_ = 0;
    bool wasInEgressTree = inEgressTree();
    totalEnqueuedWeight_ = 0;
#ifndef NDEBUG
    totalEnqueuedWeightCheck_ = 0;
#endif
    if (wasInEgressTree && !inEgressTree()) {
      propagatePendingEgressClear(this);
    }
  }
  auto res = addChild(std::move(node));
  res->addChildren(std::move(children));
  return res;
}

void
HTTP2PriorityQueue::Node::addChildren(list<unique_ptr<Node>>&& children) {
  list<unique_ptr<Node>> emptyChilden;
  uint64_t totalEnqueuedWeight = 0;
  for (auto& child: children) {
    if (child->inEgressTree()) {
      totalEnqueuedWeight += child->weight_;
      child->parent_->removeEnqueuedChild(child.get());
      CHECK(!child->enqueuedHook_.is_linked());
      addEnqueuedChild(child.get());
    } else {
      CHECK(!child->enqueuedHook_.is_linked());
    }
    addChild(std::move(child));
  }
  std::swap(children, emptyChilden);
  if (totalEnqueuedWeight > 0) {
    if (!inEgressTree()) {
      propagatePendingEgressSignal(this);
    }
    totalEnqueuedWeight_ += totalEnqueuedWeight;
  }
}

HTTP2PriorityQueue::Handle
HTTP2PriorityQueue::Node::addChild(
  unique_ptr<HTTP2PriorityQueue::Node> child) {
  child->parent_ = this;
  totalChildWeight_ += child->weight_;
  Node* raw = child.get();
  raw->self_ = children_.insert(children_.end(), std::move(child));
  return raw;
}

unique_ptr<HTTP2PriorityQueue::Node>
HTTP2PriorityQueue::Node::detachChild(Node* node) {
  CHECK(!node->isEnqueued());
  totalChildWeight_ -= node->weight_;
  auto it = node->self_;
  auto res = std::move(*node->self_);
  children_.erase(it);
  node->parent_ = nullptr;
  return res;
}

HTTP2PriorityQueue::Handle
HTTP2PriorityQueue::Node::reparent(HTTP2PriorityQueue::Node* newParent,
                                   bool exclusive) {
  // Save enqueued_ and totalEnqueuedWeight_, clear them and restore
  // after reparenting
  bool wasInEgressTree = inEgressTree();
  bool enqueued = enqueued_;
  uint64_t totalEnqueuedWeight = totalEnqueuedWeight_;
  totalEnqueuedWeight_ = 0;
  enqueued_ = false;
  if (wasInEgressTree) {
    propagatePendingEgressClear(this);
  }

  auto self = parent_->detachChild(this);
  (void)newParent->emplaceNode(std::move(self), exclusive);

  // Restore state
  enqueued_ = enqueued;
  if (wasInEgressTree) {
    propagatePendingEgressSignal(this);
  }
  totalEnqueuedWeight_ += totalEnqueuedWeight;

  return this;
}

// Returns true if this is a descendant of node
bool
HTTP2PriorityQueue::Node::isDescendantOf(HTTP2PriorityQueue::Node *node) {
  Node* cur = parent_;
  while (cur) {
    if (cur->id_ == node->id_) {
      return true;
    }
    cur = cur->parent_;
  }
  return false;
}

// Here "enqueued" means enqueued or enqueued descendent - part of the
// nextEgress computation.

void
HTTP2PriorityQueue::Node::addEnqueuedChild(HTTP2PriorityQueue::Node* node) {
  CHECK(!node->enqueuedHook_.is_linked());
  enqueuedChildren_.push_back(*node);
}

void
HTTP2PriorityQueue::Node::removeEnqueuedChild(HTTP2PriorityQueue::Node* node) {
  CHECK(node->enqueuedHook_.is_linked());
  enqueuedChildren_.erase(enqueuedChildren_.iterator_to(*node));
}

void
HTTP2PriorityQueue::Node::signalPendingEgress() {
  enqueued_ = true;
  propagatePendingEgressSignal(this);
}

void
HTTP2PriorityQueue::Node::propagatePendingEgressSignal(
  HTTP2PriorityQueue::Node *node) {
  Node* parent = node->parent_;
  bool stop = node->totalEnqueuedWeight_ > 0;
  // Continue adding node->weight_ to parent_->totalEnqueuedWeight_ as
  // long as node state changed from no-egress-in-subtree to
  // egress-in-subtree
  while (parent && !stop) {
    stop = parent->inEgressTree();
    parent->totalEnqueuedWeight_ += node->weight_;
    parent->addEnqueuedChild(node);
    node = parent;
    parent = parent->parent_;
  }
}

void
HTTP2PriorityQueue::Node::clearPendingEgress() {
  CHECK(enqueued_);
  enqueued_ = false;
  propagatePendingEgressClear(this);
}

void
HTTP2PriorityQueue::Node::propagatePendingEgressClear(
  HTTP2PriorityQueue::Node* node) {
  Node* parent = node->parent_;
  bool stop = node->inEgressTree();
  // Continue subtracting node->weight_ from parent_->totalEnqueuedWeight_
  // as long as node state changes from egress-in-subtree to
  // no-egress-in-subtree
  while (parent && !stop) {
    CHECK_GE(parent->totalEnqueuedWeight_, node->weight_);
    parent->totalEnqueuedWeight_ -= node->weight_;
    parent->removeEnqueuedChild(node);
    stop = parent->inEgressTree();
    node = parent;
    parent = parent->parent_;
  }
}

// Set a new weight for this node
void
HTTP2PriorityQueue::Node::updateWeight(uint8_t weight) {
  int16_t delta = weight - weight_ + 1;
  weight_ = weight + 1;
  parent_->totalChildWeight_ += delta;
  if (isEnqueued()) {
    parent_->totalEnqueuedWeight_ += delta;
  }
}

// Removes the node from the tree
void
HTTP2PriorityQueue::Node::removeFromTree() {
  if (!children_.empty()) {
    // update child weights so they sum to (approximately) this node's weight.
    double r = double(weight_) / totalChildWeight_;
    for (auto& child: children_) {
      uint64_t newWeight = std::max(uint64_t(child->weight_ * r), uint64_t(1));
      CHECK_LE(newWeight, 256);
      child->updateWeight(uint8_t(newWeight) - 1);
    }
  }

  CHECK(!isEnqueued());
  if (inEgressTree()) {
    // Gah this is tricky.
    // The children of this node are moving to this node's parent.  We need the
    // tree in a consistent state before calling addChildren, so mark the
    // current node's totalEnqueuedWeight_ as 0 and propagate the clear upwards.
    // addChildren will handle re-signalling egress.
    totalEnqueuedWeight_ = 0;
    propagatePendingEgressClear(this);
  }

  // move my children to my parent
  parent_->addChildren(std::move(children_));
  (void)parent_->detachChild(this);
}

// Find the node for the given stream ID in the priority tree
HTTP2PriorityQueue::Node*
HTTP2PriorityQueue::Node::findInTree(HTTPCodec::StreamID id, uint64_t* depth) {
  if (id_ == id) {
    return this;
  }
  if (depth) {
    *depth += 1;
  }
  Node* res = nullptr;
  for (auto& child: children_) {
    res = child->findInTree(id, depth);
    if (res) {
      break;
    }
  }
  return res;
}

bool
HTTP2PriorityQueue::Node::iterate(
  const std::function<bool(HTTPCodec::StreamID,
                           HTTPTransaction *, double)>& fn,
  const std::function<bool()>& stopFn, bool all) {
  bool stop = false;
  if (stopFn()) {
    return true;
  }
#ifndef NDEBUG
  CHECK_EQ(totalEnqueuedWeight_, totalEnqueuedWeightCheck_);
#endif
  if (parent_ /* exclude root */  && (all || isEnqueued())) {
    stop = fn(id_, txn_, getRelativeWeight());
  }
  for (auto& child: children_) {
    if (stop || stopFn()) {
      return true;
    }
    stop = child->iterate(fn, stopFn, all);
  }
  return stop;
}

bool
HTTP2PriorityQueue::Node::visitBFS(
  double relativeParentWeight,
  const std::function<bool(HTTPCodec::StreamID, HTTPTransaction *, double)>& fn,
  bool all,
  PendingList& pendingNodes, bool enqueuedChildren) {
  bool invoke = (parent_ != nullptr && (all || isEnqueued()));
  auto relativeEnqueuedWeight = getRelativeEnqueuedWeight();

#ifndef NDEBUG
  CHECK_EQ(totalEnqueuedWeight_, totalEnqueuedWeightCheck_);
#endif
  // Add children when all==true, or for any not invoked node with
  // pending children
  if (all || (!invoke && totalEnqueuedWeight_ > 0)) {
    double newRelWeight = relativeParentWeight * relativeEnqueuedWeight;
    if (enqueuedChildren) {
      for (auto child = enqueuedChildren_.begin();
           child != enqueuedChildren_.end();
           child++) {
        pendingNodes.emplace_back(child->id_, &(*child), newRelWeight);
      }
    } else {
      for (auto& child: children_) {
        pendingNodes.emplace_back(child->id_, child.get(), newRelWeight);
      }
    }
  }

  // Invoke fn last in case it deletes this node
  if (invoke && fn(id_, txn_,
                   relativeParentWeight * relativeEnqueuedWeight)) {
    return true;
  }

  return false;
}

#ifndef NDEBUG
void
HTTP2PriorityQueue::Node::updateEnqueuedWeight(bool activeNodes) {
  totalEnqueuedWeightCheck_ = totalChildWeight_;
  for (auto& child: children_) {
    child->updateEnqueuedWeight(activeNodes);
  }
  if (activeNodes) {
    if (totalEnqueuedWeightCheck_ == 0 && !isEnqueued()) {
      // Must only be called with activeCount_ > 0, root cannot be dequeued
      CHECK_NOTNULL(parent_);
      parent_->totalEnqueuedWeightCheck_ -= weight_;
    } else {
      CHECK(parent_ == nullptr || enqueuedHook_.is_linked());
    }
  } else {
    totalEnqueuedWeightCheck_ = 0;
  }
}
#endif

/// class HTTP2PriorityQueue

HTTP2PriorityQueue::Handle
HTTP2PriorityQueue::addTransaction(HTTPCodec::StreamID id,
                                   http2::PriorityUpdate pri,
                                   HTTPTransaction *txn,
                                   uint64_t* depth) {
  CHECK_NE(id, 0);
  CHECK_NE(id, pri.streamDependency) << "Tried to create a loop in the tree";

  Node* parent = &root_;
  if (depth) {
    *depth = 0;
  }
  if (pri.streamDependency != 0) {
    Node* dep = find(pri.streamDependency, depth);
    if (dep == nullptr) {
      // specified a missing parent (timed out an idle node)?
      VLOG(4) << "assigning default priority to txn=" << id;
    } else {
      parent = dep;
    }
  }
  auto node = folly::make_unique<Node>(parent, id, pri.weight, txn);
  auto result = parent->emplaceNode(std::move(node), pri.exclusive);
  pendingWeightChange_ = true;
  return result;
}

HTTP2PriorityQueue::Handle
HTTP2PriorityQueue::updatePriority(HTTP2PriorityQueue::Handle handle,
                                   http2::PriorityUpdate pri) {
  Node* node = handle;
  pendingWeightChange_ = true;
  node->updateWeight(pri.weight);
  CHECK_NE(pri.streamDependency, node->getID()) <<
    "Tried to create a loop in the tree";;
  if (pri.streamDependency == node->parentID() && !pri.exclusive) {
    // no move
    return handle;
  }

  Node* newParent = find(pri.streamDependency);
  if (!newParent) {
    newParent = &root_;
  }

  if (newParent->isDescendantOf(node)) {
    newParent = newParent->reparent(node->getParent(), false);
  }
  return node->reparent(newParent, pri.exclusive);
}

void
HTTP2PriorityQueue::removeTransaction(HTTP2PriorityQueue::Handle handle) {
  Node* node = handle;
  pendingWeightChange_ = true;
  // TODO: or require the node to do it?
  if (node->isEnqueued()) {
    clearPendingEgress(handle);
  }
  node->removeFromTree();
}

void
HTTP2PriorityQueue::signalPendingEgress(Handle h) {
  if (!h->isEnqueued()) {
    h->signalPendingEgress();
    activeCount_++;
    pendingWeightChange_ = true;
  }
}

void
HTTP2PriorityQueue::clearPendingEgress(Handle h) {
  CHECK_GT(activeCount_, 0);
  // clear does a CHECK on h->isEnqueued()
  h->clearPendingEgress();
  activeCount_--;
  pendingWeightChange_ = true;
}

void
HTTP2PriorityQueue::iterateBFS(
  const std::function<bool(HTTPCodec::StreamID, HTTPTransaction *, double)>& fn,
  const std::function<bool()>& stopFn, bool all) {
  Node::PendingList pendingNodes{{0, &root_, 1.0}};
  Node::PendingList newPendingNodes;
  bool stop = false;

  updateEnqueuedWeight();
  while (!stop && !stopFn() && !pendingNodes.empty()) {
    CHECK(newPendingNodes.empty());
    while (!stop && !pendingNodes.empty()) {
      Node* node = findInternal(pendingNodes.front().id);
      if (node) {
        stop = node->visitBFS(pendingNodes.front().ratio, fn, all,
                              newPendingNodes, false /* all children */);
      }
      pendingNodes.pop_front();
    }
    std::swap(pendingNodes, newPendingNodes);
  }
}

bool
HTTP2PriorityQueue::nextEgressResult(HTTPCodec::StreamID id,
                                     HTTPTransaction* txn, double r) {
  nextEgressResults_.get()->emplace_back(txn, r);
  return false;
}

void
HTTP2PriorityQueue::nextEgress(HTTP2PriorityQueue::NextEgressResult& result) {
  struct WeightCmp {
    bool operator()(const std::pair<HTTPTransaction*, double>& t1,
                    const std::pair<HTTPTransaction*, double>& t2) {
      return t1.second > t2.second;
    }
  };

  result.reserve(activeCount_);
  nextEgressResults_.reset(&result);

  updateEnqueuedWeight();
  Node::PendingList pendingNodes;
  pendingNodes.emplace_back(0, &root_, 1.0);
  bool stop = false;
  while (!stop && !pendingNodes.empty()) {
    Node* node = pendingNodes.front().node;
    if (node) {
      stop = node->visitBFS(pendingNodes.front().ratio, nextEgressResult,
                            false, pendingNodes, true /* enqueued children */);
    }
    pendingNodes.pop_front();
  }
  std::sort(result.begin(), result.end(), WeightCmp());
  nextEgressResults_.release();
}

HTTP2PriorityQueue::Node*
HTTP2PriorityQueue::find(HTTPCodec::StreamID id, uint64_t* depth) {
  if (id == 0) {
    return nullptr;
  }
  return root_.findInTree(id, depth);
}

void
HTTP2PriorityQueue::updateEnqueuedWeight() {
#ifndef NDEBUG
  if (pendingWeightChange_) {
    root_.updateEnqueuedWeight(activeCount_ > 0);
    pendingWeightChange_ = false;
  }
#endif
}

}
