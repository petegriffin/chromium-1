// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_XR_XR_FRAME_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_XR_XR_FRAME_H_

#include <memory>

#include "device/vr/public/mojom/vr_service.mojom-blink.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/bindings/trace_wrapper_member.h"
#include "third_party/blink/renderer/platform/heap/handle.h"
#include "third_party/blink/renderer/platform/transforms/transformation_matrix.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"

namespace blink {

class ExceptionState;
class XRInputSource;
class XRPose;
class XRReferenceSpace;
class XRSession;
class XRSpace;
class XRViewerPose;

class XRFrame final : public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  explicit XRFrame(XRSession*);

  XRSession* session() const { return session_; }

  XRViewerPose* getViewerPose(XRReferenceSpace*, ExceptionState&) const;
  XRPose* getPose(XRSpace*, XRSpace*, ExceptionState&);

  void SetBasePoseMatrix(const TransformationMatrix&);

  void Trace(blink::Visitor*) override;

  void Deactivate();

 private:
  XRPose* GetTargetRayPose(XRInputSource*, XRSpace*) const;
  XRPose* GetGripPose(XRInputSource*, XRSpace*) const;

  const Member<XRSession> session_;

  // Maps from mojo space to headset space.
  std::unique_ptr<TransformationMatrix> base_pose_matrix_;

  bool active_ = true;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_XR_XR_FRAME_H_
