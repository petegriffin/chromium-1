// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/windows_version.h"

#include <windows.h>

#include <memory>

#include "base/file_version_info_win.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/registry.h"

#if !defined(__clang__) && _MSC_FULL_VER < 191125507
#error VS 2017 Update 3.2 or higher is required
#endif

#if !defined(NTDDI_WIN10_RS2)
// Windows 10 April 2018 SDK is required to build Chrome.
#error April 2018 SDK (10.0.17134.0) or higher required.
#endif

namespace {
typedef BOOL (WINAPI *GetProductInfoPtr)(DWORD, DWORD, DWORD, DWORD, PDWORD);
}  // namespace

namespace base {
namespace win {

namespace {

// Returns the the "UBR" value from the registry. Introduced in Windows 10,
// this undocumented value appears to be similar to a patch number.
// Returns 0 if the value does not exist or it could not be read.
int GetUBR() {
  // The values under the CurrentVersion registry hive are mirrored under
  // the corresponding Wow6432 hive.
  static constexpr char16 kRegKeyWindowsNTCurrentVersion[] =
      STRING16_LITERAL("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");

  RegKey key;
  if (key.Open(HKEY_LOCAL_MACHINE, kRegKeyWindowsNTCurrentVersion,
               KEY_QUERY_VALUE) != ERROR_SUCCESS) {
    return 0;
  }

  DWORD ubr = 0;
  key.ReadValueDW(STRING16_LITERAL("UBR"), &ubr);

  return static_cast<int>(ubr);
}

const _SYSTEM_INFO& GetSystemInfoStorage() {
  static const NoDestructor<_SYSTEM_INFO> system_info([] {
    _SYSTEM_INFO info = {};
    ::GetNativeSystemInfo(&info);
    return info;
  }());
  return *system_info;
}

}  // namespace

// static
OSInfo** OSInfo::GetInstanceStorage() {
  // Note: we don't use the Singleton class because it depends on AtExitManager,
  // and it's convenient for other modules to use this class without it.
  static OSInfo* info = []() {
    _OSVERSIONINFOEXW version_info = {sizeof(version_info)};
    ::GetVersionEx(reinterpret_cast<_OSVERSIONINFOW*>(&version_info));

    DWORD os_type = 0;
    if (version_info.dwMajorVersion == 6 || version_info.dwMajorVersion == 10) {
      // Only present on Vista+.
      GetProductInfoPtr get_product_info =
          reinterpret_cast<GetProductInfoPtr>(::GetProcAddress(
              ::GetModuleHandle(L"kernel32.dll"), "GetProductInfo"));
      get_product_info(version_info.dwMajorVersion, version_info.dwMinorVersion,
                       0, 0, &os_type);
    }

    return new OSInfo(version_info, GetSystemInfoStorage(), os_type);
  }();

  return &info;
}

// static
OSInfo* OSInfo::GetInstance() {
  return *GetInstanceStorage();
}

// static
OSInfo::WindowsArchitecture OSInfo::GetArchitecture() {
  switch (GetSystemInfoStorage().wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_INTEL:
      return X86_ARCHITECTURE;
    case PROCESSOR_ARCHITECTURE_AMD64:
      return X64_ARCHITECTURE;
    case PROCESSOR_ARCHITECTURE_IA64:
      return IA64_ARCHITECTURE;
    case PROCESSOR_ARCHITECTURE_ARM64:
      return ARM64_ARCHITECTURE;
    default:
      return OTHER_ARCHITECTURE;
  }
}

OSInfo::OSInfo(const _OSVERSIONINFOEXW& version_info,
               const _SYSTEM_INFO& system_info,
               int os_type)
    : version_(VERSION_PRE_XP),
      wow64_status_(GetWOW64StatusForProcess(GetCurrentProcess())) {
  version_number_.major = version_info.dwMajorVersion;
  version_number_.minor = version_info.dwMinorVersion;
  version_number_.build = version_info.dwBuildNumber;
  version_number_.patch = GetUBR();
  version_ = MajorMinorBuildToVersion(
      version_number_.major, version_number_.minor, version_number_.build);
  service_pack_.major = version_info.wServicePackMajor;
  service_pack_.minor = version_info.wServicePackMinor;
  service_pack_str_ = WideToUTF8(version_info.szCSDVersion);

  processors_ = system_info.dwNumberOfProcessors;
  allocation_granularity_ = system_info.dwAllocationGranularity;

  if (version_info.dwMajorVersion == 6 || version_info.dwMajorVersion == 10) {
    // Only present on Vista+.
    switch (os_type) {
      case PRODUCT_CLUSTER_SERVER:
      case PRODUCT_DATACENTER_SERVER:
      case PRODUCT_DATACENTER_SERVER_CORE:
      case PRODUCT_ENTERPRISE_SERVER:
      case PRODUCT_ENTERPRISE_SERVER_CORE:
      case PRODUCT_ENTERPRISE_SERVER_IA64:
      case PRODUCT_SMALLBUSINESS_SERVER:
      case PRODUCT_SMALLBUSINESS_SERVER_PREMIUM:
      case PRODUCT_STANDARD_SERVER:
      case PRODUCT_STANDARD_SERVER_CORE:
      case PRODUCT_WEB_SERVER:
        version_type_ = SUITE_SERVER;
        break;
      case PRODUCT_PROFESSIONAL:
      case PRODUCT_ULTIMATE:
        version_type_ = SUITE_PROFESSIONAL;
        break;
      case PRODUCT_ENTERPRISE:
      case PRODUCT_ENTERPRISE_E:
      case PRODUCT_ENTERPRISE_EVALUATION:
      case PRODUCT_ENTERPRISE_N:
      case PRODUCT_ENTERPRISE_N_EVALUATION:
      case PRODUCT_ENTERPRISE_S:
      case PRODUCT_ENTERPRISE_S_EVALUATION:
      case PRODUCT_ENTERPRISE_S_N:
      case PRODUCT_ENTERPRISE_S_N_EVALUATION:
      case PRODUCT_BUSINESS:
      case PRODUCT_BUSINESS_N:
        version_type_ = SUITE_ENTERPRISE;
        break;
      case PRODUCT_EDUCATION:
      case PRODUCT_EDUCATION_N:
        version_type_ = SUITE_EDUCATION;
        break;
      case PRODUCT_HOME_BASIC:
      case PRODUCT_HOME_PREMIUM:
      case PRODUCT_STARTER:
      default:
        version_type_ = SUITE_HOME;
        break;
    }
  } else if (version_info.dwMajorVersion == 5 &&
             version_info.dwMinorVersion == 2) {
    if (version_info.wProductType == VER_NT_WORKSTATION &&
        system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
      version_type_ = SUITE_PROFESSIONAL;
    } else if (version_info.wSuiteMask & VER_SUITE_WH_SERVER) {
      version_type_ = SUITE_HOME;
    } else {
      version_type_ = SUITE_SERVER;
    }
  } else if (version_info.dwMajorVersion == 5 &&
             version_info.dwMinorVersion == 1) {
    if (version_info.wSuiteMask & VER_SUITE_PERSONAL)
      version_type_ = SUITE_HOME;
    else
      version_type_ = SUITE_PROFESSIONAL;
  } else {
    // Windows is pre XP so we don't care but pick a safe default.
    version_type_ = SUITE_HOME;
  }
}

OSInfo::~OSInfo() {
}

Version OSInfo::Kernel32Version() const {
  static const Version kernel32_version =
      MajorMinorBuildToVersion(Kernel32BaseVersion().components()[0],
                               Kernel32BaseVersion().components()[1],
                               Kernel32BaseVersion().components()[2]);
  return kernel32_version;
}

// Retrieve a version from kernel32. This is useful because when running in
// compatibility mode for a down-level version of the OS, the file version of
// kernel32 will still be the "real" version.
base::Version OSInfo::Kernel32BaseVersion() const {
  static const NoDestructor<base::Version> version([] {
    std::unique_ptr<FileVersionInfoWin> file_version_info =
        FileVersionInfoWin::CreateFileVersionInfoWin(
            FilePath(FILE_PATH_LITERAL("kernel32.dll")));
    if (!file_version_info) {
      // crbug.com/912061: on some systems it seems kernel32.dll might be
      // corrupted or not in a state to get version info. In this case try
      // kernelbase.dll as a fallback.
      file_version_info = FileVersionInfoWin::CreateFileVersionInfoWin(
          FilePath(FILE_PATH_LITERAL("kernelbase.dll")));
    }
    CHECK(file_version_info);
    const int major =
        HIWORD(file_version_info->fixed_file_info()->dwFileVersionMS);
    const int minor =
        LOWORD(file_version_info->fixed_file_info()->dwFileVersionMS);
    const int build =
        HIWORD(file_version_info->fixed_file_info()->dwFileVersionLS);
    const int patch =
        LOWORD(file_version_info->fixed_file_info()->dwFileVersionLS);
    return base::Version(std::vector<uint32_t>{major, minor, build, patch});
  }());
  return *version;
}

std::string OSInfo::processor_model_name() {
  if (processor_model_name_.empty()) {
    const char16 kProcessorNameString[] =
        STRING16_LITERAL("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
    RegKey key(HKEY_LOCAL_MACHINE, kProcessorNameString, KEY_READ);
    string16 value;
    key.ReadValue(STRING16_LITERAL("ProcessorNameString"), &value);
    processor_model_name_ = UTF16ToUTF8(value);
  }
  return processor_model_name_;
}

// static
OSInfo::WOW64Status OSInfo::GetWOW64StatusForProcess(HANDLE process_handle) {
  typedef BOOL (WINAPI* IsWow64ProcessFunc)(HANDLE, PBOOL);
  IsWow64ProcessFunc is_wow64_process = reinterpret_cast<IsWow64ProcessFunc>(
      GetProcAddress(GetModuleHandle(L"kernel32.dll"), "IsWow64Process"));
  if (!is_wow64_process)
    return WOW64_DISABLED;
  BOOL is_wow64 = FALSE;
  if (!(*is_wow64_process)(process_handle, &is_wow64))
    return WOW64_UNKNOWN;
  return is_wow64 ? WOW64_ENABLED : WOW64_DISABLED;
}

// With the exception of Server 2003, server variants are treated the same as
// the corresponding workstation release.
// static
Version OSInfo::MajorMinorBuildToVersion(int major, int minor, int build) {
  if (major == 10) {
    if (build >= 17763)
      return VERSION_WIN10_RS5;
    if (build >= 17134)
      return VERSION_WIN10_RS4;
    if (build >= 16299)
      return VERSION_WIN10_RS3;
    if (build >= 15063)
      return VERSION_WIN10_RS2;
    if (build >= 14393)
      return VERSION_WIN10_RS1;
    if (build >= 10586)
      return VERSION_WIN10_TH2;
    return VERSION_WIN10;
  }

  if (major > 6) {
    // Hitting this likely means that it's time for a >10 block above.
    NOTREACHED() << major << "." << minor << "." << build;
    return VERSION_WIN_LAST;
  }

  if (major == 6) {
    switch (minor) {
      case 0:
        return VERSION_VISTA;
      case 1:
        return VERSION_WIN7;
      case 2:
        return VERSION_WIN8;
      default:
        DCHECK_EQ(minor, 3);
        return VERSION_WIN8_1;
    }
  }

  if (major == 5 && minor != 0) {
    // Treat XP Pro x64, Home Server, and Server 2003 R2 as Server 2003.
    return minor == 1 ? VERSION_XP : VERSION_SERVER_2003;
  }

  // Win 2000 or older.
  return VERSION_PRE_XP;
}

Version GetVersion() {
  return OSInfo::GetInstance()->version();
}

}  // namespace win
}  // namespace base
