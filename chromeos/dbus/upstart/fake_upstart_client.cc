// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/upstart/fake_upstart_client.h"

#include "base/bind.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/fake_auth_policy_client.h"
#include "chromeos/dbus/fake_media_analytics_client.h"

namespace chromeos {

namespace {
// Used to track the fake instance, mirrors the instance in the base class.
FakeUpstartClient* g_instance = nullptr;
}  // namespace

FakeUpstartClient::FakeUpstartClient() {
  DCHECK(!g_instance);
  g_instance = this;
}

FakeUpstartClient::~FakeUpstartClient() {
  DCHECK_EQ(this, g_instance);
  g_instance = nullptr;
}

// static
FakeUpstartClient* FakeUpstartClient::Get() {
  return g_instance;
}

void FakeUpstartClient::StartJob(const std::string& job,
                                 const std::vector<std::string>& upstart_env,
                                 VoidDBusMethodCallback callback) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), true));
}

void FakeUpstartClient::StopJob(const std::string& job,
                                VoidDBusMethodCallback callback) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), true));
}

void FakeUpstartClient::StartAuthPolicyService() {
  static_cast<FakeAuthPolicyClient*>(
      DBusThreadManager::Get()->GetAuthPolicyClient())
      ->SetStarted(true);
}

void FakeUpstartClient::RestartAuthPolicyService() {
  FakeAuthPolicyClient* authpolicy_client = static_cast<FakeAuthPolicyClient*>(
      DBusThreadManager::Get()->GetAuthPolicyClient());
  DLOG_IF(WARNING, !authpolicy_client->started())
      << "Trying to restart authpolicyd which is not started";
  authpolicy_client->SetStarted(true);
}

void FakeUpstartClient::StartMediaAnalytics(
    const std::vector<std::string>& /* upstart_env */,
    VoidDBusMethodCallback callback) {
  FakeMediaAnalyticsClient* media_analytics_client =
      static_cast<FakeMediaAnalyticsClient*>(
          DBusThreadManager::Get()->GetMediaAnalyticsClient());
  DLOG_IF(WARNING, media_analytics_client->process_running())
      << "Trying to start media analytics which is already started.";
  media_analytics_client->set_process_running(true);
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), true));
}

void FakeUpstartClient::RestartMediaAnalytics(VoidDBusMethodCallback callback) {
  FakeMediaAnalyticsClient* media_analytics_client =
      static_cast<FakeMediaAnalyticsClient*>(
          DBusThreadManager::Get()->GetMediaAnalyticsClient());
  media_analytics_client->set_process_running(false);
  media_analytics_client->set_process_running(true);
  media_analytics_client->SetStateSuspended();
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), true));
}

void FakeUpstartClient::StopMediaAnalytics() {
  FakeMediaAnalyticsClient* media_analytics_client =
      static_cast<FakeMediaAnalyticsClient*>(
          DBusThreadManager::Get()->GetMediaAnalyticsClient());
  DLOG_IF(WARNING, !media_analytics_client->process_running())
      << "Trying to stop media analytics which is not started.";
  media_analytics_client->set_process_running(false);
}

void FakeUpstartClient::StopMediaAnalytics(VoidDBusMethodCallback callback) {
  FakeMediaAnalyticsClient* media_analytics_client =
      static_cast<FakeMediaAnalyticsClient*>(
          DBusThreadManager::Get()->GetMediaAnalyticsClient());
  media_analytics_client->set_process_running(false);
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), true));
}

}  // namespace chromeos
