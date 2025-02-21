// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGPU_GPU_ADAPTER_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGPU_GPU_ADAPTER_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/modules/webgpu/dawn_object.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class GPUDevice;
class GPUDeviceDescriptor;

class GPUAdapter final : public DawnObject {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static GPUAdapter* Create(
      const String& name,
      scoped_refptr<DawnControlClientHolder> dawn_control_client);
  GPUAdapter(const String& name,
             scoped_refptr<DawnControlClientHolder> dawn_control_client);

  const String& name() const;

  GPUDevice* createDevice(const GPUDeviceDescriptor* descriptor);

 private:
  String name_;

  DISALLOW_COPY_AND_ASSIGN(GPUAdapter);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGPU_GPU_ADAPTER_H_
