/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/HTTPCodec.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/http/codec/experimental/HTTP2Framer.h>

#include <list>

namespace proxygen {

class HTTP2PriorityQueue {

 private:
  class Node;

 public:

  typedef std::list<Node>::iterator Handle;

 private:

  class Node {
   public:
    Node(Node* parent, HTTPCodec::StreamID id,
         uint32_t weight, HTTPTransaction *txn)
      : parent_(parent),
        id_(id),
        weight_(weight),
        txn_(txn) {
    }

    Node(Node* parent, HTTPCodec::StreamID id,
         uint32_t weight, HTTPTransaction *txn,
         std::list<Node>&& children)
      : parent_(parent),
        id_(id),
        weight_(weight),
        txn_(txn),
        children_(std::move(children)) {
      // update children to point to new parent
      for (auto& child: children_) {
        child.parent_ = this;
        totalChildWeight_ += child.weight_;
      }
    }

    void addChildren(std::list<Node>&& children) {
      for (auto& child: children) {
        children_.emplace_back(this, child.id_, child.weight_, child.txn_,
                               std::move(child.children_));
        totalChildWeight_ += child.weight_;
      }
    }

    Node* parent() const {
      return parent_;
    }

    HTTPCodec::StreamID parentID() const {
      if (parent_) {
        return parent_->id_;
      }
      return 0;
    }

    Handle lastChild() {
      auto it = children_.rbegin();
      it++;
      Handle result = it.base();
      return result;
    }

    // Add a new node as a child of this node
    Handle emplaceNode(HTTPCodec::StreamID id, uint32_t weight,
                       HTTPTransaction* txn, bool exclusive,
                       std::list<Node>&& nodeChildren) {
      std::list<Node> children;
      if (exclusive) {
        // this->children become new node's children
        std::swap(children, children_);
        totalChildWeight_ = 0;
      }
      children_.emplace_back(this, id, weight, txn, std::move(nodeChildren));
      Handle result = lastChild();
      result->addChildren(std::move(children));
      totalChildWeight_ += weight;
      return result;
    }

    void detachChild(HTTPCodec::StreamID id) {
      auto it = std::find_if(children_.begin(), children_.end(),
                             [id] (Node& n) { return n.id_ == id; });
      if (it == children_.end()) {
        CHECK(false) << "Detach non-child id=" << id;
      }
      detachChild(it);
    }

    void detachChild(Handle it) {
      totalChildWeight_ -= it->weight_;
      children_.erase(it);
    }

    Handle reparent(Node* newParent, bool exclusive) {
      Node* oldParent = parent_;
      std::list<Node> children;
      std::swap(children_, children);
      Node selfCopy = *this;
      parent_->detachChild(id_);
      auto result = newParent->emplaceNode(selfCopy.id_, selfCopy.weight_,
                                           selfCopy.txn_, exclusive,
                                           std::move(children));
      result->enqueued_ = selfCopy.enqueued_;
      return result;
    }

    // Returns true if this is a descendant of node
    bool isDescendantOf(Node *node) {
      Node* cur = parent_;
      while (cur) {
        if (cur->id_ == node->id_) {
          return true;
        }
        cur = cur->parent_;
      }
      return false;
    }

    // True if this Node is in the egress queue
    bool isEnqueued() const {
      return (txn_ != nullptr && enqueued_);
    }

    void signalPendingEgress() {
      enqueued_ = true;
    }

    void clearPendingEgress() {
      CHECK(enqueued_);
      enqueued_ = false;
    }

    // Set a new weight for this node
    void updateWeight(uint8_t weight) {
      int16_t delta = weight - weight_;
      weight_ = weight;
      parent_->totalChildWeight_ += delta;
    }

    // Removes the node from the tree
    void removeFromTree(Handle self) {
      // move my children to my parent
      parent_->addChildren(std::move(children_));
      parent_->detachChild(self);
    }

    // Find the node for the given stream ID in the priority tree
    Node* findInTree(HTTPCodec::StreamID id) {
      if (id_ == id) {
        return this;
      }
      Node* res = nullptr;
      for (auto& child: children_) {
        res = child.findInTree(id);
        if (res) {
          break;
        }
      }
      return res;
    }

    /* Execute the given function on this node and all child nodes presently
     * enqueued, until one of them asks to stop, or the stop function returns
     * true.
     *
     * The all parameter visits every node, even the ones not currently
     * enqueued.
     *
     * The arguments to the function are
     *   txn - HTTPTransaction for the node
     *   ratio - weight of this txn relative to all peers (not just enequeued)
     */
    bool iterate(const std::function<bool(HTTPCodec::StreamID,
                                          HTTPTransaction *, double)>& fn,
                 const std::function<bool()>& stopFn, bool all) {
      bool stop = false;
      if (stopFn()) {
        return true;
      }
      if (parent_ /* exclude root */  && (all || isEnqueued())) {
        stop = fn(id_, txn_, (double)weight_ / parent_->totalChildWeight_);
      }
      for (auto& child: children_) {
        if (stop || stopFn()) {
          return true;
        }
        stop = child.iterate(fn, stopFn, all);
      }
      return stop;
    }

    bool visitBFS(const std::function<bool(HTTPCodec::StreamID,
                                           HTTPTransaction *, double)>& fn,
                  bool all, std::list<Node*>& pendingNodes) {
      bool stop = false;
      if (parent_ != nullptr && (all || isEnqueued())) {
        stop = fn(id_, txn_, (double)weight_ / parent_->totalChildWeight_);
      }
      if (stop) {
        return true;
      }
      for (auto& child: children_) {
        pendingNodes.push_back(&child);
      }
      return false;
    }


   private:
    Node *parent_{nullptr};
    bool enqueued_{false};
    HTTPCodec::StreamID id_{0};
    uint8_t weight_{16};
    HTTPTransaction *txn_{nullptr};
    uint64_t totalChildWeight_{0};
    std::list<Node> children_;


  };

 public:
  HTTP2PriorityQueue() {}

  // Find the node in priority tree
  Node* find(HTTPCodec::StreamID id) {
    if (id == 0) {
      return nullptr;
    }
    return root_.findInTree(id);
  }

  // Notify the queue when a transaction has egress
  void signalPendingEgress(Handle h) {
    h->signalPendingEgress();
    activeCount_++;
  }

  // Notify the queue when a transaction no longer has egress
  void clearPendingEgress(Handle h) {
    CHECK(activeCount_ > 0);
    h->clearPendingEgress();
    activeCount_--;
  }

  // adds new transaction (possibly nullptr) to the priority tree
  Handle addTransaction(HTTPCodec::StreamID id, http2::PriorityUpdate pri,
                        HTTPTransaction *txn) {
    CHECK(id != 0);

    Node* parent = &root_;
    if (pri.streamDependency != 0) {
      Node* dep = find(pri.streamDependency);
      if (dep == nullptr) {
        // specified a missing parent (timed out an idle node)?
        LOG(INFO) << "assigning default priority to txn=" << id;
      } else {
        parent = dep;
      }
    }
    std::list<Node> emptyChildren;
    auto result = parent->emplaceNode(id, pri.weight, txn, pri.exclusive,
                                      std::move(emptyChildren));
    return result;
  }

  // update the priority of an existing node
  Handle updatePriority(Handle handle, http2::PriorityUpdate pri) {
    Node& node = *handle;
    node.updateWeight(pri.weight);
    if (pri.streamDependency == node.parentID() && !pri.exclusive) {
      // no move
      return handle;
    }

    Node* newParent = find(pri.streamDependency);
    if (!newParent) {
      newParent = &root_;
    }

    if (newParent->isDescendantOf(&node)) {
      newParent = &(*(newParent->reparent(node.parent(), false)));
    }
    return node.reparent(newParent, pri.exclusive);
  }

  // Remove the transaction from the priority tree
  void removeTransaction(Handle handle) {
    Node& node = *handle;
    // TODO: or require the node to do it?
    if (node.isEnqueued()) {
      clearPendingEgress(handle);
    }
    node.removeFromTree(handle);
  }

  // Returns true if there are no transaction with pending egress
  bool empty() const {
    return activeCount_ == 0;
  }

  // The number with pending egress
  uint64_t numPendingEgress() const {
    return activeCount_;
  }

  void iterate(const std::function<bool(HTTPCodec::StreamID,
                                        HTTPTransaction *, double)>& fn,
               const std::function<bool()>& stopFn, bool all) {
    root_.iterate(fn, stopFn, all);
  }

  void iterateBFS(const std::function<bool(HTTPCodec::StreamID,
                                           HTTPTransaction *, double)>& fn,
                  const std::function<bool()>& stopFn, bool all) {
    std::list<Node*> pendingNodes;
    pendingNodes.push_back(&root_);
    bool stop = false;
    while (!stop && !stopFn() && !pendingNodes.empty()) {
      Node* node = pendingNodes.front();
      stop = node->visitBFS(fn, all, pendingNodes);
      pendingNodes.pop_front();
    }
  }


  Node root_{nullptr, 0, 1, nullptr};
  uint64_t activeCount_{0};
};


}
