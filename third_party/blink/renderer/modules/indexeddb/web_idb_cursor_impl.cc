// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/indexeddb/web_idb_cursor_impl.h"

#include <stddef.h>

#include <string>
#include <vector>

#include "base/single_thread_task_runner.h"
#include "mojo/public/cpp/bindings/strong_associated_binding.h"
#include "third_party/blink/public/platform/modules/indexeddb/web_idb_database_exception.h"
#include "third_party/blink/renderer/modules/indexeddb/idb_key_range.h"
#include "third_party/blink/renderer/modules/indexeddb/indexed_db_dispatcher.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

using blink::mojom::blink::IDBCallbacksAssociatedPtrInfo;
using blink::mojom::blink::IDBCursorAssociatedPtrInfo;

namespace blink {

WebIDBCursorImpl::WebIDBCursorImpl(
    mojom::blink::IDBCursorAssociatedPtrInfo cursor_info,
    int64_t transaction_id,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : transaction_id_(transaction_id),
      continue_count_(0),
      used_prefetches_(0),
      pending_onsuccess_callbacks_(0),
      prefetch_amount_(kMinPrefetchAmount),
      task_runner_(task_runner),
      weak_factory_(this) {
  cursor_.Bind(std::move(cursor_info), std::move(task_runner));
  IndexedDBDispatcher::RegisterCursor(this);
}

WebIDBCursorImpl::~WebIDBCursorImpl() {
  // It's not possible for there to be pending callbacks that address this
  // object since inside WebKit, they hold a reference to the object which owns
  // this object. But, if that ever changed, then we'd need to invalidate
  // any such pointers.
  IndexedDBDispatcher::UnregisterCursor(this);
}

void WebIDBCursorImpl::Advance(uint32_t count, WebIDBCallbacks* callbacks_ptr) {
  std::unique_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);
  if (count <= prefetch_keys_.size()) {
    CachedAdvance(count, callbacks.get());
    return;
  }
  ResetPrefetchCache();

  // Reset all cursor prefetch caches except for this cursor.
  IndexedDBDispatcher::ResetCursorPrefetchCaches(transaction_id_, this);

  callbacks->SetState(weak_factory_.GetWeakPtr(), transaction_id_);
  cursor_->Advance(count,
                   WTF::Bind(&WebIDBCursorImpl::AdvanceCallback,
                             WTF::Unretained(this), std::move(callbacks)));
}

void WebIDBCursorImpl::AdvanceCallback(
    std::unique_ptr<WebIDBCallbacks> callbacks,
    mojom::blink::IDBErrorPtr error,
    mojom::blink::IDBCursorValuePtr cursor_value) {
  if (error) {
    callbacks->Error(error->error_code, std::move(error->error_message));
    callbacks.reset();
    return;
  }

  if (!cursor_value) {
    callbacks->SuccessValue(nullptr);
    callbacks.reset();
    return;
  }

  if (cursor_value->keys.size() != 1u ||
      cursor_value->primary_keys.size() != 1u ||
      cursor_value->values.size() != 1u) {
    callbacks->Error(blink::kWebIDBDatabaseExceptionUnknownError,
                     "Invalid response");
    callbacks.reset();
    return;
  }

  callbacks->SuccessCursorContinue(std::move(cursor_value->keys[0]),
                                   std::move(cursor_value->primary_keys[0]),
                                   std::move(cursor_value->values[0]));
  callbacks.reset();
}

void WebIDBCursorImpl::CursorContinue(const IDBKey* key,
                                      const IDBKey* primary_key,
                                      WebIDBCallbacks* callbacks_ptr) {
  DCHECK(key && primary_key);
  std::unique_ptr<WebIDBCallbacks> callbacks(callbacks_ptr);

  if (key->GetType() == mojom::IDBKeyType::Null &&
      primary_key->GetType() == mojom::IDBKeyType::Null) {
    // No key(s), so this would qualify for a prefetch.
    ++continue_count_;

    if (!prefetch_keys_.IsEmpty()) {
      // We have a prefetch cache, so serve the result from that.
      CachedContinue(callbacks.get());
      return;
    }

    if (continue_count_ > kPrefetchContinueThreshold) {
      // Request pre-fetch.
      ++pending_onsuccess_callbacks_;

      callbacks->SetState(weak_factory_.GetWeakPtr(), transaction_id_);
      cursor_->Prefetch(prefetch_amount_,
                        WTF::Bind(&WebIDBCursorImpl::PrefetchCallback,
                                  WTF::Unretained(this), std::move(callbacks)));

      // Increase prefetch_amount_ exponentially.
      prefetch_amount_ *= 2;
      if (prefetch_amount_ > kMaxPrefetchAmount)
        prefetch_amount_ = kMaxPrefetchAmount;

      return;
    }
  } else {
    // Key argument supplied. We couldn't prefetch this.
    ResetPrefetchCache();
  }

  // Reset all cursor prefetch caches except for this cursor.
  IndexedDBDispatcher::ResetCursorPrefetchCaches(transaction_id_, this);
  callbacks->SetState(weak_factory_.GetWeakPtr(), transaction_id_);
  cursor_->CursorContinue(
      IDBKey::Clone(key), IDBKey::Clone(primary_key),
      WTF::Bind(&WebIDBCursorImpl::CursorContinueCallback,
                WTF::Unretained(this), std::move(callbacks)));
}

void WebIDBCursorImpl::CursorContinueCallback(
    std::unique_ptr<WebIDBCallbacks> callbacks,
    mojom::blink::IDBErrorPtr error,
    mojom::blink::IDBCursorValuePtr value) {
  if (error) {
    callbacks->Error(error->error_code, std::move(error->error_message));
    callbacks.reset();
    return;
  }

  if (!value) {
    callbacks->SuccessValue(nullptr);
    callbacks.reset();
    return;
  }

  if (value->keys.size() != 1u || value->primary_keys.size() != 1u ||
      value->values.size() != 1u) {
    callbacks->Error(blink::kWebIDBDatabaseExceptionUnknownError,
                     "Invalid response");
    callbacks.reset();
    return;
  }

  callbacks->SuccessCursorContinue(std::move(value->keys[0]),
                                   std::move(value->primary_keys[0]),
                                   std::move(value->values[0]));
  callbacks.reset();
}

void WebIDBCursorImpl::PrefetchCallback(
    std::unique_ptr<WebIDBCallbacks> callbacks,
    mojom::blink::IDBErrorPtr error,
    mojom::blink::IDBCursorValuePtr value) {
  if (error) {
    callbacks->Error(error->error_code, std::move(error->error_message));
    callbacks.reset();
    return;
  }

  if (!value) {
    callbacks->SuccessValue(nullptr);
    callbacks.reset();
    return;
  }

  if (value->keys.size() != value->primary_keys.size() ||
      value->keys.size() != value->values.size()) {
    callbacks->Error(blink::kWebIDBDatabaseExceptionUnknownError,
                     "Invalid response");
    callbacks.reset();
    return;
  }

  callbacks->SuccessCursorPrefetch(std::move(value->keys),
                                   std::move(value->primary_keys),
                                   std::move(value->values));
  callbacks.reset();
}

void WebIDBCursorImpl::PostSuccessHandlerCallback() {
  pending_onsuccess_callbacks_--;

  // If the onsuccess callback called continue()/advance() on the cursor
  // again, and that request was served by the prefetch cache, then
  // pending_onsuccess_callbacks_ would be incremented. If not, it means the
  // callback did something else, or nothing at all, in which case we need to
  // reset the cache.

  if (pending_onsuccess_callbacks_ == 0)
    ResetPrefetchCache();
}

void WebIDBCursorImpl::SetPrefetchData(
    Vector<std::unique_ptr<IDBKey>> keys,
    Vector<std::unique_ptr<IDBKey>> primary_keys,
    Vector<std::unique_ptr<IDBValue>> values) {
  // Keys and values are stored in reverse order so that a cache'd continue can
  // pop a value off of the back and prevent new memory allocations.
  prefetch_keys_.AppendRange(std::make_move_iterator(keys.rbegin()),
                             std::make_move_iterator(keys.rend()));
  prefetch_primary_keys_.AppendRange(
      std::make_move_iterator(primary_keys.rbegin()),
      std::make_move_iterator(primary_keys.rend()));
  prefetch_values_.AppendRange(std::make_move_iterator(values.rbegin()),
                               std::make_move_iterator(values.rend()));

  used_prefetches_ = 0;
  pending_onsuccess_callbacks_ = 0;
}

void WebIDBCursorImpl::CachedAdvance(unsigned long count,
                                     WebIDBCallbacks* callbacks) {
  DCHECK_GE(prefetch_keys_.size(), count);
  DCHECK_EQ(prefetch_primary_keys_.size(), prefetch_keys_.size());
  DCHECK_EQ(prefetch_values_.size(), prefetch_keys_.size());

  while (count > 1) {
    prefetch_keys_.pop_back();
    prefetch_primary_keys_.pop_back();
    prefetch_values_.pop_back();
    ++used_prefetches_;
    --count;
  }

  CachedContinue(callbacks);
}

void WebIDBCursorImpl::CachedContinue(WebIDBCallbacks* callbacks) {
  DCHECK_GT(prefetch_keys_.size(), 0ul);
  DCHECK_EQ(prefetch_primary_keys_.size(), prefetch_keys_.size());
  DCHECK_EQ(prefetch_values_.size(), prefetch_keys_.size());

  // Keys and values are stored in reverse order so that a cache'd continue can
  // pop a value off of the back and prevent new memory allocations.
  std::unique_ptr<IDBKey> key = std::move(prefetch_keys_.back());
  std::unique_ptr<IDBKey> primary_key =
      std::move(prefetch_primary_keys_.back());
  std::unique_ptr<IDBValue> value = std::move(prefetch_values_.back());

  prefetch_keys_.pop_back();
  prefetch_primary_keys_.pop_back();
  prefetch_values_.pop_back();
  ++used_prefetches_;

  ++pending_onsuccess_callbacks_;

  if (!continue_count_) {
    // The cache was invalidated by a call to ResetPrefetchCache()
    // after the RequestIDBCursorPrefetch() was made. Now that the
    // initiating continue() call has been satisfied, discard
    // the rest of the cache.
    ResetPrefetchCache();
  }

  callbacks->SuccessCursorContinue(std::move(key), std::move(primary_key),
                                   std::move(value));
}

void WebIDBCursorImpl::ResetPrefetchCache() {
  continue_count_ = 0;
  prefetch_amount_ = kMinPrefetchAmount;

  if (prefetch_keys_.IsEmpty()) {
    // No prefetch cache, so no need to reset the cursor in the back-end.
    return;
  }

  // Reset the back-end cursor.
  cursor_->PrefetchReset(used_prefetches_, prefetch_keys_.size());

  // Reset the prefetch cache.
  prefetch_keys_.clear();
  prefetch_primary_keys_.clear();
  prefetch_values_.clear();

  pending_onsuccess_callbacks_ = 0;
}

IDBCallbacksAssociatedPtrInfo WebIDBCursorImpl::GetCallbacksProxy(
    std::unique_ptr<WebIDBCallbacks> callbacks) {
  IDBCallbacksAssociatedPtrInfo ptr_info;
  auto request = mojo::MakeRequest(&ptr_info);
  mojo::MakeStrongAssociatedBinding(std::move(callbacks), std::move(request),
                                    task_runner_);
  return ptr_info;
}

}  // namespace blink
