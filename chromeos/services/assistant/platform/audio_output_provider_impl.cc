// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/assistant/platform/audio_output_provider_impl.h"

#include <algorithm>
#include <utility>

#include "ash/public/interfaces/constants.mojom.h"
#include "base/bind.h"
#include "chromeos/services/assistant/media_session/assistant_media_session.h"
#include "chromeos/services/assistant/platform/audio_stream_handler.h"
#include "chromeos/services/assistant/public/mojom/assistant_audio_decoder.mojom.h"
#include "chromeos/services/assistant/public/mojom/constants.mojom.h"
#include "libassistant/shared/public/platform_audio_buffer.h"
#include "media/audio/audio_device_description.h"
#include "services/service_manager/public/cpp/connector.h"

namespace chromeos {
namespace assistant {

namespace {

bool IsEncodedFormat(const assistant_client::OutputStreamFormat& format) {
  return format.encoding ==
             assistant_client::OutputStreamEncoding::STREAM_MP3 ||
         format.encoding ==
             assistant_client::OutputStreamEncoding::STREAM_OPUS_IN_OGG;
}

class AudioOutputImpl : public assistant_client::AudioOutput {
 public:
  AudioOutputImpl(
      service_manager::Connector* connector,
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      scoped_refptr<base::SequencedTaskRunner> background_task_runner,
      mojom::AssistantAudioDecoderFactory* audio_decoder_factory,
      assistant_client::OutputStreamType type,
      assistant_client::OutputStreamFormat format,
      const std::string& device_id)
      : connector_(connector),
        main_task_runner_(task_runner),
        background_thread_task_runner_(background_task_runner),
        audio_decoder_factory_(audio_decoder_factory),
        stream_type_(type),
        format_(format),
        audio_stream_handler_(
            std::make_unique<AudioStreamHandler>(task_runner)),
        device_owner_(std::make_unique<AudioDeviceOwner>(task_runner,
                                                         background_task_runner,
                                                         device_id)) {}

  ~AudioOutputImpl() override {
    // This ensures that it will be executed after StartOnMainThread.
    main_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](std::unique_ptr<AudioDeviceOwner> device_owner,
               scoped_refptr<base::SequencedTaskRunner> background_runner) {
              // Ensures |device_owner| is destructed on the correct thread.
              background_runner->DeleteSoon(FROM_HERE, device_owner.release());
            },
            std::move(device_owner_), background_thread_task_runner_));
    main_task_runner_->DeleteSoon(FROM_HERE, audio_stream_handler_.release());
  }

  // assistant_client::AudioOutput overrides:
  assistant_client::OutputStreamType GetType() override { return stream_type_; }

  void Start(assistant_client::AudioOutput::Delegate* delegate) override {
    if (IsEncodedFormat(format_)) {
      main_task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(
              &AudioStreamHandler::StartAudioDecoder,
              base::Unretained(audio_stream_handler_.get()),
              audio_decoder_factory_, delegate,
              base::BindOnce(&AudioDeviceOwner::StartOnMainThread,
                             base::Unretained(device_owner_.get()),
                             audio_stream_handler_.get(), connector_)));
    } else {
      main_task_runner_->PostTask(
          FROM_HERE, base::BindOnce(&AudioDeviceOwner::StartOnMainThread,
                                    base::Unretained(device_owner_.get()),
                                    delegate, connector_, format_));
    }
  }

  void Stop() override {
    if (IsEncodedFormat(format_)) {
      device_owner_->SetDelegate(nullptr);
      main_task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(&AudioStreamHandler::OnStopped,
                         base::Unretained(audio_stream_handler_.get())));
    } else {
      background_thread_task_runner_->PostTask(
          FROM_HERE, base::BindOnce(&AudioDeviceOwner::StopOnBackgroundThread,
                                    base::Unretained(device_owner_.get())));
    }
  }

 private:
  service_manager::Connector* connector_;
  scoped_refptr<base::SequencedTaskRunner> main_task_runner_;
  scoped_refptr<base::SequencedTaskRunner> background_thread_task_runner_;
  mojom::AssistantAudioDecoderFactory* audio_decoder_factory_;

  const assistant_client::OutputStreamType stream_type_;
  assistant_client::OutputStreamFormat format_;

  std::unique_ptr<AudioStreamHandler> audio_stream_handler_;

  std::unique_ptr<AudioDeviceOwner> device_owner_;

  DISALLOW_COPY_AND_ASSIGN(AudioOutputImpl);
};

}  // namespace

AudioOutputProviderImpl::AudioOutputProviderImpl(
    service_manager::Connector* connector,
    AssistantMediaSession* media_session,
    scoped_refptr<base::SequencedTaskRunner> background_task_runner,
    const std::string& device_id)
    : loop_back_input_(connector,
                       media::AudioDeviceDescription::kLoopbackInputDeviceId,
                       /*hotword_device_id=*/std::string()),
      volume_control_impl_(connector, media_session),
      connector_(connector),
      main_task_runner_(base::SequencedTaskRunnerHandle::Get()),
      background_task_runner_(background_task_runner),
      device_id_(device_id) {
  connector_->BindInterface(mojom::kAudioDecoderServiceName,
                            mojo::MakeRequest(&audio_decoder_factory_ptr_));
  audio_decoder_factory_ = audio_decoder_factory_ptr_.get();
}

AudioOutputProviderImpl::~AudioOutputProviderImpl() = default;

assistant_client::AudioOutput* AudioOutputProviderImpl::CreateAudioOutput(
    assistant_client::OutputStreamType type,
    const assistant_client::OutputStreamFormat& stream_format) {
  // Owned by one arbitrary thread inside libassistant. It will be destroyed
  // once assistant_client::AudioOutput::Delegate::OnStopped() is called.
  return new AudioOutputImpl(connector_, main_task_runner_,
                             background_task_runner_, audio_decoder_factory_,
                             type, stream_format, device_id_);
}

std::vector<assistant_client::OutputStreamEncoding>
AudioOutputProviderImpl::GetSupportedStreamEncodings() {
  return std::vector<assistant_client::OutputStreamEncoding>{
      assistant_client::OutputStreamEncoding::STREAM_PCM_S16,
      assistant_client::OutputStreamEncoding::STREAM_PCM_S32,
      assistant_client::OutputStreamEncoding::STREAM_PCM_F32,
      assistant_client::OutputStreamEncoding::STREAM_MP3,
      assistant_client::OutputStreamEncoding::STREAM_OPUS_IN_OGG,
  };
}

assistant_client::AudioInput* AudioOutputProviderImpl::GetReferenceInput() {
  return &loop_back_input_;
}

bool AudioOutputProviderImpl::SupportsPlaybackTimestamp() const {
  // TODO(muyuanli): implement.
  return false;
}

assistant_client::VolumeControl& AudioOutputProviderImpl::GetVolumeControl() {
  return volume_control_impl_;
}

void AudioOutputProviderImpl::RegisterAudioEmittingStateCallback(
    AudioEmittingStateCallback callback) {
  // TODO(muyuanli): implement.
}

}  // namespace assistant
}  // namespace chromeos
