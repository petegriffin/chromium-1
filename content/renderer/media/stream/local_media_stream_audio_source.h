// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_STREAM_LOCAL_MEDIA_STREAM_AUDIO_SOURCE_H_
#define CONTENT_RENDERER_MEDIA_STREAM_LOCAL_MEDIA_STREAM_AUDIO_SOURCE_H_

#include <string>

#include "content/common/content_export.h"
#include "media/base/audio_capturer_source.h"
#include "third_party/blink/public/platform/modules/mediastream/media_stream_audio_source.h"

namespace content {

// Represents a local source of audio data generated by an AudioInputDevice.
// Uses content::AudioDeviceFactory to auto-create the AudioInputDevice, using
// the parameters and session ID found in MediaStreamDevice, just before the
// first track is connected. Audio data is transported directly to the tracks
// (i.e., there is no audio processing).
class CONTENT_EXPORT LocalMediaStreamAudioSource
    : public blink::MediaStreamAudioSource,
      public media::AudioCapturerSource::CaptureCallback {
 public:
  // |consumer_render_frame_id| references the RenderFrame that will consume the
  // audio data. Audio parameters and (optionally) a pre-existing audio session
  // ID are read from |device_info|.
  LocalMediaStreamAudioSource(
      int consumer_render_frame_id,
      const blink::MediaStreamDevice& device,
      bool disable_local_echo,
      const ConstraintsCallback& started_callback,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  ~LocalMediaStreamAudioSource() final;

  // MediaStreamAudioSource implementation.
  void ChangeSourceImpl(const blink::MediaStreamDevice& new_device) final;

 private:
  // MediaStreamAudioSource implementation.
  bool EnsureSourceIsStarted() final;
  void EnsureSourceIsStopped() final;

  // media::AudioCapturerSource::CaptureCallback implementation.
  void OnCaptureStarted() final;
  void Capture(const media::AudioBus* audio_bus,
               int audio_delay_milliseconds,
               double volume,
               bool key_pressed) final;
  void OnCaptureError(const std::string& message) final;
  void OnCaptureMuted(bool is_muted) final;

  // The RenderFrame that will consume the audio data. Used when creating
  // AudioInputDevices via the AudioDeviceFactory.
  const int consumer_render_frame_id_;

  // The device created by the AudioDeviceFactory in EnsureSourceIsStarted().
  scoped_refptr<media::AudioCapturerSource> source_;

  // Callback that's called when the audio source has been initialized.
  ConstraintsCallback started_callback_;

  // In debug builds, check that all methods that could cause object graph
  // or data flow changes are being called on the main thread.
  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(LocalMediaStreamAudioSource);
};

}  // namespace content

#endif  // CONTENT_RENDERER_MEDIA_STREAM_LOCAL_MEDIA_STREAM_AUDIO_SOURCE_H_
