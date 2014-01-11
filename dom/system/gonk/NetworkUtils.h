/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NetworkUtils_h
#define NetworkUtils_h

#include "nsString.h"
#include "mozilla/dom/NetworkOptionsBinding.h"
#include "mozilla/dom/network/NetUtils.h"
#include "mozilla/ipc/Netd.h"
#include "nsTArray.h"

class NetworkParams;
class CommandChain;

using namespace mozilla::dom;

typedef void (*POSTMESSAGE)(NetworkResultOptions& aResult);
typedef void (*CALLBACK)(CommandChain*, bool, NetworkResultOptions& aResult);
typedef void (*ERROR_CALLBACK)(NetworkParams& aOptions, NetworkResultOptions& aResult);
typedef void (*COMMAND)(CommandChain*, CALLBACK, NetworkResultOptions& aResult);

class NetworkParams
{
public:
  NetworkParams() {
  }

  NetworkParams(const NetworkParams& aOther) {
    mIp = aOther.mIp;
    mCmd = aOther.mCmd;
    mDns1_str = aOther.mDns1_str;
    mDns2_str = aOther.mDns2_str;
    mGateway = aOther.mGateway;
    mGateway_str = aOther.mGateway_str;
    mHostnames = aOther.mHostnames;
    mId = aOther.mId;
    mIfname = aOther.mIfname;
    mNetmask = aOther.mNetmask;
    mOldIfname = aOther.mOldIfname;
    mMode = aOther.mMode;
    mReport = aOther.mReport;
    mIsAsync = aOther.mIsAsync;
    mEnabled = aOther.mEnabled;
    mWifictrlinterfacename = aOther.mWifictrlinterfacename;
    mInternalIfname = aOther.mInternalIfname;
    mExternalIfname = aOther.mExternalIfname;
    mEnable = aOther.mEnable;
    mSsid = aOther.mSsid;
    mSecurity = aOther.mSecurity;
    mKey = aOther.mKey;
    mPrefix = aOther.mPrefix;
    mLink = aOther.mLink;
    mInterfaceList = aOther.mInterfaceList;
    mWifiStartIp = aOther.mWifiStartIp;
    mWifiEndIp = aOther.mWifiEndIp;
    mUsbStartIp = aOther.mUsbStartIp;
    mUsbEndIp = aOther.mUsbEndIp;
    mDns1 = aOther.mDns1;
    mDns2 = aOther.mDns2;
    mRxBytes = aOther.mRxBytes;
    mTxBytes = aOther.mTxBytes;
    mDate = aOther.mDate;
    mStartIp = aOther.mStartIp;
    mEndIp = aOther.mEndIp;
    mServerIp = aOther.mServerIp;
    mMaskLength = aOther.mMaskLength;
    mPreInternalIfname = aOther.mPreInternalIfname;
    mPreExternalIfname = aOther.mPreExternalIfname;
    mCurInternalIfname = aOther.mCurInternalIfname;
    mCurExternalIfname = aOther.mCurExternalIfname;
    mThreshold = aOther.mThreshold;
  }
  
  NetworkParams(const NetworkCommandOptions& aOther) {

#define COPY_SEQUENCE_FIELD(prop)                                                            \
    if (aOther.prop.WasPassed()) {                                                           \
      mozilla::dom::Sequence<nsString > const & currentValue = aOther.prop.InternalValue();  \
      uint32_t length = currentValue.Length();                                               \
      for (uint32_t idx = 0; idx < length; idx++) {                                          \
        mHostnames.AppendElement(currentValue[idx]);                                         \
      }                                                                                      \
    }

#define COPY_OPT_STRING_FIELD(prop, defaultValue)       \
    if (aOther.prop.WasPassed()) {                      \
      if (aOther.prop.Value().EqualsLiteral("null")) {  \
        prop = defaultValue;                            \
      } else {                                          \
        prop = aOther.prop.Value();                     \
      }                                                 \
    } else {                                            \
      prop = defaultValue;                              \
    }

#define COPY_OPT_FIELD(prop, defaultValue)            \
    if (aOther.prop.WasPassed()) {                    \
      prop = aOther.prop.Value();                     \
    } else {                                          \
      prop = defaultValue;                            \
    }

#define COPY_FIELD(prop) prop = aOther.prop;

    COPY_FIELD(mId)
    COPY_FIELD(mCmd)
    COPY_OPT_STRING_FIELD(mDns1_str, EmptyString())
    COPY_OPT_STRING_FIELD(mDns2_str, EmptyString())
    COPY_OPT_STRING_FIELD(mGateway, EmptyString())
    COPY_OPT_STRING_FIELD(mGateway_str, EmptyString())
    COPY_SEQUENCE_FIELD(mHostnames)
    COPY_OPT_STRING_FIELD(mIfname, EmptyString())
    COPY_OPT_STRING_FIELD(mIp, EmptyString())
    COPY_OPT_STRING_FIELD(mNetmask, EmptyString())
    COPY_OPT_STRING_FIELD(mOldIfname, EmptyString())
    COPY_OPT_STRING_FIELD(mMode, EmptyString())
    COPY_OPT_FIELD(mReport, false)
    COPY_OPT_FIELD(mIsAsync, true)
    COPY_OPT_FIELD(mEnabled, false)
    COPY_OPT_STRING_FIELD(mWifictrlinterfacename, EmptyString())
    COPY_OPT_STRING_FIELD(mInternalIfname, EmptyString())
    COPY_OPT_STRING_FIELD(mExternalIfname, EmptyString())
    COPY_OPT_FIELD(mEnable, false)
    COPY_OPT_STRING_FIELD(mSsid, EmptyString())
    COPY_OPT_STRING_FIELD(mSecurity, EmptyString())
    COPY_OPT_STRING_FIELD(mKey, EmptyString())
    COPY_OPT_STRING_FIELD(mPrefix, EmptyString())
    COPY_OPT_STRING_FIELD(mLink, EmptyString())
    COPY_SEQUENCE_FIELD(mInterfaceList)
    COPY_OPT_STRING_FIELD(mWifiStartIp, EmptyString())
    COPY_OPT_STRING_FIELD(mWifiEndIp, EmptyString())
    COPY_OPT_STRING_FIELD(mUsbStartIp, EmptyString())
    COPY_OPT_STRING_FIELD(mUsbEndIp, EmptyString())
    COPY_OPT_STRING_FIELD(mDns1, EmptyString())
    COPY_OPT_STRING_FIELD(mDns2, EmptyString())
    COPY_OPT_FIELD(mRxBytes, -1)
    COPY_OPT_FIELD(mTxBytes, -1)
    COPY_OPT_STRING_FIELD(mDate, EmptyString())
    COPY_OPT_STRING_FIELD(mStartIp, EmptyString())
    COPY_OPT_STRING_FIELD(mEndIp, EmptyString())
    COPY_OPT_STRING_FIELD(mServerIp, EmptyString())
    COPY_OPT_STRING_FIELD(mMaskLength, EmptyString())
    COPY_OPT_STRING_FIELD(mPreInternalIfname, EmptyString())
    COPY_OPT_STRING_FIELD(mPreExternalIfname, EmptyString())
    COPY_OPT_STRING_FIELD(mCurInternalIfname, EmptyString())
    COPY_OPT_STRING_FIELD(mCurExternalIfname, EmptyString())
    COPY_OPT_FIELD(mThreshold, -1)

#undef COPY_SEQUENCE_FIELD
#undef COPY_OPT_STRING_FIELD
#undef COPY_OPT_FIELD
#undef COPY_FIELD
  }

  int32_t mId;
  nsString mCmd;
  nsString mDns1_str;
  nsString mDns2_str;
  nsString mGateway;
  nsString mGateway_str;
  nsTArray<nsString> mHostnames;
  nsString mIfname;
  nsString mIp;
  nsString mNetmask;
  nsString mOldIfname;
  nsString mMode;
  bool mReport;
  bool mIsAsync;
  bool mEnabled;
  nsString mWifictrlinterfacename;
  nsString mInternalIfname;
  nsString mExternalIfname;
  bool mEnable;
  nsString mSsid;
  nsString mSecurity;
  nsString mKey;
  nsString mPrefix;
  nsString mLink;
  nsTArray<nsCString> mInterfaceList;
  nsString mWifiStartIp;
  nsString mWifiEndIp;
  nsString mUsbStartIp;
  nsString mUsbEndIp;
  nsString mDns1;
  nsString mDns2;
  float mRxBytes;
  float mTxBytes;
  nsString mDate;
  nsString mStartIp;
  nsString mEndIp;
  nsString mServerIp;
  nsString mMaskLength;
  nsString mPreInternalIfname;
  nsString mPreExternalIfname;
  nsString mCurInternalIfname;
  nsString mCurExternalIfname;
  long mThreshold;
};

// CommandChain store the necessary information to execute command one by one.
// Including :
// 1. Command parameters.
// 2. Command list.
// 3. Error callback function.
// 4. Index of current execution command.
class CommandChain MOZ_FINAL
{
public:
  CommandChain(const NetworkParams& aParams,
               COMMAND aCmds[],
               uint32_t aLength,
               ERROR_CALLBACK aError) 
  : mIndex(-1)
  , mParams(aParams)
  , mCommands(aCmds)
  , mLength(aLength)
  , mError(aError) {
  }

  NetworkParams&
  getParams()
  {
    return mParams;
  };

  COMMAND
  getNextCommand()
  {
    mIndex++;
    return mIndex < mLength ? mCommands[mIndex] : NULL;
  };

  ERROR_CALLBACK
  getErrorCallback() const
  {
    return mError;
  };

private:
  uint32_t mIndex;
  NetworkParams mParams;
  COMMAND* mCommands;
  uint32_t mLength;
  ERROR_CALLBACK mError;
};

class NetworkUtils MOZ_FINAL
{
public:
  NetworkUtils(POSTMESSAGE post);
  ~NetworkUtils();

  void ExecuteCommand(NetworkParams aOptions);
  void onNetdMessage(const mozilla::ipc::NetdCommand& aCommand);

  bool setDNS(NetworkParams& aOptions);
  bool setDefaultRouteAndDNS(NetworkParams& aOptions);
  bool addHostRoute(NetworkParams& aOptions);
  bool removeDefaultRoute(NetworkParams& aOptions);
  bool removeHostRoute(NetworkParams& aOptions);
  bool removeHostRoutes(NetworkParams& aOptions);
  bool removeNetworkRoute(NetworkParams& aOptions);
  bool getNetworkInterfaceStats(NetworkParams& aOptions);  
  bool setNetworkInterfaceAlarm(NetworkParams& aOptions);
  bool enableNetworkInterfaceAlarm(NetworkParams& aOptions);
  bool disableNetworkInterfaceAlarm(NetworkParams& aOptions);
  bool setWifiOperationMode(NetworkParams& aOptions);
  bool setDhcpServer(NetworkParams& aOptions);
  bool setWifiTethering(NetworkParams& aOptions);
  bool setUSBTethering(NetworkParams& aOptions);
  bool enableUsbRndis(NetworkParams& aOptions);
  bool updateUpStream(NetworkParams& aOptions);

  POSTMESSAGE mPostCallback;
  nsAutoPtr<NetUtils> mNetUtils;

private:
  void checkUsbRndisState(NetworkParams& aOptions);

  void dumpParams(NetworkParams& aOptions, const char* aType);
};

#endif
