// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include "ash/login_status.h"
#include "ash/public/cpp/ash_switches.h"
#include "ash/public/cpp/ash_view_ids.h"
#include "ash/public/interfaces/constants.mojom.h"
#include "ash/public/interfaces/system_tray_test_api.test-mojom-test-utils.h"
#include "ash/public/interfaces/system_tray_test_api.test-mojom.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/login/lock/screen_locker.h"
#include "chrome/browser/chromeos/login/lock/screen_locker_tester.h"
#include "chrome/browser/chromeos/login/test/login_screen_tester.h"
#include "chrome/browser/chromeos/login/ui/login_display_host.h"
#include "chrome/browser/chromeos/login/ui/webui_login_view.h"
#include "chrome/browser/chromeos/policy/device_policy_builder.h"
#include "chrome/browser/chromeos/policy/device_policy_cros_browser_test.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/ui/webui/chromeos/login/oobe_ui.h"
#include "chromeos/constants/chromeos_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/fake_session_manager_client.h"
#include "chromeos/dbus/session_manager_client.h"
#include "components/policy/proto/chrome_device_policy.pb.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/service_manager_connection.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "services/service_manager/public/cpp/connector.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/views/view.h"

namespace em = enterprise_management;

namespace chromeos {

class ShutdownPolicyBaseTest
    : public policy::DevicePolicyCrosBrowserTest,
      public DeviceSettingsService::Observer {
 protected:
  ShutdownPolicyBaseTest() {}
  ~ShutdownPolicyBaseTest() override {}

  // DeviceSettingsService::Observer:
  void DeviceSettingsUpdated() override {
    if (run_loop_)
      run_loop_->Quit();
  }

  // policy::DevicePolicyCrosBrowserTest:
  void SetUpOnMainThread() override {
    policy::DevicePolicyCrosBrowserTest::SetUpOnMainThread();
    // Connect to the ash test interface.
    content::ServiceManagerConnection::GetForProcess()
        ->GetConnector()
        ->BindInterface(ash::mojom::kServiceName, &tray_test_api_);
  }

  void SetUpInProcessBrowserTestFixture() override {
    policy::DevicePolicyCrosBrowserTest::SetUpInProcessBrowserTestFixture();
    InstallOwnerKey();
    MarkAsEnterpriseOwned();
  }

  // Updates the device shutdown policy and sets it to |reboot_on_shutdown|.
  void UpdateRebootOnShutdownPolicy(bool reboot_on_shutdown) {
    policy::DevicePolicyBuilder* builder = device_policy();
    ASSERT_TRUE(builder);
    em::ChromeDeviceSettingsProto& proto(builder->payload());
    proto.mutable_reboot_on_shutdown()->set_reboot_on_shutdown(
        reboot_on_shutdown);
  }

  // Refreshes device policy and waits for it to be applied.
  void SyncRefreshDevicePolicy() {
    run_loop_.reset(new base::RunLoop());
    DeviceSettingsService::Get()->AddObserver(this);
    RefreshDevicePolicy();
    run_loop_->Run();
    DeviceSettingsService::Get()->RemoveObserver(this);
    run_loop_.reset();
  }

  // Blocks until the OobeUI indicates that the javascript side has been
  // initialized.
  void WaitUntilOobeUIIsReady(OobeUI* oobe_ui) {
    ASSERT_TRUE(oobe_ui);
    base::RunLoop run_loop;
    const bool oobe_ui_ready = oobe_ui->IsJSReady(run_loop.QuitClosure());
    if (!oobe_ui_ready)
      run_loop.Run();
  }

  bool result_;
  std::unique_ptr<base::RunLoop> run_loop_;
  ash::mojom::SystemTrayTestApiPtr tray_test_api_;
};

class ShutdownPolicyInSessionTest
    : public ShutdownPolicyBaseTest {
 protected:
  ShutdownPolicyInSessionTest() {}
  ~ShutdownPolicyInSessionTest() override {}

  // Opens the system tray menu. This creates the tray views.
  void OpenSystemTrayMenu() {
    ash::mojom::SystemTrayTestApiAsyncWaiter wait_for(tray_test_api_.get());
    wait_for.ShowBubble();
  }

  // Closes the system tray menu. This deletes the tray views.
  void CloseSystemTrayMenu() {
    ash::mojom::SystemTrayTestApiAsyncWaiter wait_for(tray_test_api_.get());
    wait_for.CloseBubble();
  }

  // Returns true if the shutdown button's tooltip matches the text of the
  // resource |message_id|.
  bool HasShutdownButtonTooltip(const std::string& tooltip) {
    ash::mojom::SystemTrayTestApiAsyncWaiter wait_for(tray_test_api_.get());
    base::string16 actual_tooltip;
    wait_for.GetBubbleViewTooltip(ash::VIEW_ID_POWER_BUTTON, &actual_tooltip);
    return base::UTF8ToUTF16(tooltip) == actual_tooltip;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ShutdownPolicyInSessionTest);
};

// Tests that by default the shutdown button tooltip is "Shut down".
IN_PROC_BROWSER_TEST_F(ShutdownPolicyInSessionTest, TestBasic) {
  OpenSystemTrayMenu();
  EXPECT_TRUE(HasShutdownButtonTooltip("Shut down"));
  CloseSystemTrayMenu();
}

// Tests that enabling the reboot-on-shutdown policy changes the shutdown button
// tooltip to "restart". Note that the tooltip doesn't change dynamically if the
// menu is open during the policy change -- that's a rare condition and
// supporting it would add complexity.
//
// TODO(crbug.com/851208): Disabled test due to flakiness.
IN_PROC_BROWSER_TEST_F(ShutdownPolicyInSessionTest, DISABLED_PolicyChange) {
  // Change the policy to reboot and let it propagate over mojo to ash.
  UpdateRebootOnShutdownPolicy(true);
  SyncRefreshDevicePolicy();
  content::RunAllPendingInMessageLoop();

  OpenSystemTrayMenu();
  EXPECT_TRUE(HasShutdownButtonTooltip("Restart"));
  CloseSystemTrayMenu();

  // Change the policy to shutdown and let it propagate over mojo to ash.
  UpdateRebootOnShutdownPolicy(false);
  SyncRefreshDevicePolicy();
  content::RunAllPendingInMessageLoop();

  OpenSystemTrayMenu();
  EXPECT_TRUE(HasShutdownButtonTooltip("Shut down"));
  CloseSystemTrayMenu();
}

class ShutdownPolicyLockerTest : public ShutdownPolicyBaseTest {
 protected:
  ShutdownPolicyLockerTest() = default;
  ~ShutdownPolicyLockerTest() override = default;

  void SetUpInProcessBrowserTestFixture() override {
    fake_session_manager_client_ = new FakeSessionManagerClient;
    DBusThreadManager::GetSetterForTesting()->SetSessionManagerClient(
        std::unique_ptr<SessionManagerClient>(fake_session_manager_client_));

    ShutdownPolicyBaseTest::SetUpInProcessBrowserTestFixture();
    zero_duration_mode_.reset(new ui::ScopedAnimationDurationScaleMode(
        ui::ScopedAnimationDurationScaleMode::ZERO_DURATION));
    InstallOwnerKey();
    MarkAsEnterpriseOwned();
  }

  void SetUpOnMainThread() override {
    ShutdownPolicyBaseTest::SetUpOnMainThread();

    // Bring up the locker screen.
    chromeos::ScreenLockerTester().Lock();
  }

  void TearDownOnMainThread() override {
    ScreenLocker::Hide();
    ShutdownPolicyBaseTest::TearDownOnMainThread();
  }

 private:
  std::unique_ptr<ui::ScopedAnimationDurationScaleMode> zero_duration_mode_;
  FakeSessionManagerClient* fake_session_manager_client_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(ShutdownPolicyLockerTest);
};

IN_PROC_BROWSER_TEST_F(ShutdownPolicyLockerTest, TestBasic) {
  ScreenLockerTester tester;
  EXPECT_FALSE(tester.IsLockRestartButtonShown());
  EXPECT_TRUE(tester.IsLockShutdownButtonShown());
}

IN_PROC_BROWSER_TEST_F(ShutdownPolicyLockerTest, PolicyChange) {
  ScreenLockerTester tester;
  int ui_update_count = tester.GetUiUpdateCount();
  UpdateRebootOnShutdownPolicy(true);
  RefreshDevicePolicy();
  tester.WaitForUiUpdate(ui_update_count);
  EXPECT_TRUE(tester.IsLockRestartButtonShown());
  EXPECT_FALSE(tester.IsLockShutdownButtonShown());

  ui_update_count = tester.GetUiUpdateCount();
  UpdateRebootOnShutdownPolicy(false);
  RefreshDevicePolicy();
  tester.WaitForUiUpdate(ui_update_count);
  EXPECT_FALSE(tester.IsLockRestartButtonShown());
  EXPECT_TRUE(tester.IsLockShutdownButtonShown());
}

class ShutdownPolicyLoginTest : public ShutdownPolicyBaseTest {
 protected:
  ShutdownPolicyLoginTest() = default;
  ~ShutdownPolicyLoginTest() override = default;

  // ShutdownPolicyBaseTest:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kLoginManager);
    command_line->AppendSwitch(switches::kForceLoginManagerInTests);
  }

  void SetUpInProcessBrowserTestFixture() override {
    ShutdownPolicyBaseTest::SetUpInProcessBrowserTestFixture();
    InstallOwnerKey();
    MarkAsEnterpriseOwned();
  }

  void SetUpOnMainThread() override {
    ShutdownPolicyBaseTest::SetUpOnMainThread();

    content::WindowedNotificationObserver(
        chrome::NOTIFICATION_LOGIN_OR_LOCK_WEBUI_VISIBLE,
        content::NotificationService::AllSources()).Wait();
    LoginDisplayHost* host = LoginDisplayHost::default_host();
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->GetOobeWebContents());

    // Wait for the login UI to be ready.
    WaitUntilOobeUIIsReady(host->GetOobeUI());
  }

  void TearDownOnMainThread() override {
    // If the login display is still showing, exit gracefully.
    if (LoginDisplayHost::default_host()) {
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&chrome::AttemptExit));
      RunUntilBrowserProcessQuits();
    }
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ShutdownPolicyLoginTest);
};

IN_PROC_BROWSER_TEST_F(ShutdownPolicyLoginTest, PolicyNotSet) {
  test::LoginScreenTester tester;
  EXPECT_FALSE(tester.IsRestartButtonShown());
  EXPECT_TRUE(tester.IsShutdownButtonShown());
}

IN_PROC_BROWSER_TEST_F(ShutdownPolicyLoginTest, PolicyChange) {
  test::LoginScreenTester tester;
  int ui_update_count = tester.GetUiUpdateCount();
  UpdateRebootOnShutdownPolicy(true);
  RefreshDevicePolicy();
  tester.WaitForUiUpdate(ui_update_count);
  EXPECT_TRUE(tester.IsRestartButtonShown());
  EXPECT_FALSE(tester.IsShutdownButtonShown());

  ui_update_count = tester.GetUiUpdateCount();
  UpdateRebootOnShutdownPolicy(false);
  RefreshDevicePolicy();
  tester.WaitForUiUpdate(ui_update_count);
  EXPECT_FALSE(tester.IsRestartButtonShown());
  EXPECT_TRUE(tester.IsShutdownButtonShown());
}

}  // namespace chromeos
