/* Copyright 2012 Mozilla Foundation and Mozilla contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "NetworkUtils.h"

#include <android/log.h>
#include <cutils/properties.h>
#include "mozilla/dom/network/NetUtils.h"

#define USE_DEBUG 1

#define WARN(args...)   __android_log_print(ANDROID_LOG_WARN,  "NetworlUtils", ## args)
#define ERROR(args...)  __android_log_print(ANDROID_LOG_ERROR,  "NetworkUtils", ## args)

#if USE_DEBUG
#define DEBUG(args...)  __android_log_print(ANDROID_LOG_DEBUG, "NetworkUtils" , ## args)
#else
#define DEBUG(args...)
#endif

using namespace mozilla::dom;
using namespace mozilla::ipc;

static const char* PERSIST_SYS_USB_CONFIG_PROPERTY = "persist.sys.usb.config";
static const char* SYS_USB_CONFIG_PROPERTY         = "sys.usb.config";
static const char* SYS_USB_STATE_PROPERTY          = "sys.usb.state";

static const char* USB_FUNCTION_RNDIS  = "rndis";
static const char* USB_FUNCTION_ADB    = "adb";

// Use this command to continue the function chain.
static const char* DUMMY_COMMAND = "tether status";

// Retry 20 times (2 seconds) for usb state transition.
static const uint32_t USB_FUNCTION_RETRY_TIMES = 20;
// Check "sys.usb.state" every 100ms. 
static const uint32_t USB_FUNCTION_RETRY_INTERVAL = 100;

// 1xx - Requested action is proceeding
static const uint32_t NETD_COMMAND_PROCEEDING   = 100;
// 2xx - Requested action has been successfully completed
static const uint32_t NETD_COMMAND_OKAY         = 200;
// 4xx - The command is accepted but the requested action didn't
// take place.
static const uint32_t NETD_COMMAND_FAIL         = 400;
// 5xx - The command syntax or parameters error
static const uint32_t NETD_COMMAND_ERROR        = 500;
// 6xx - Unsolicited broadcasts
static const uint32_t NETD_COMMAND_UNSOLICITED  = 600;

// Broadcast messages
static const uint32_t NETD_COMMAND_INTERFACE_CHANGE     = 600;
static const uint32_t NETD_COMMAND_BANDWIDTH_CONTROLLER = 601;

static const char* INTERFACE_DELIMIT = "\0";
static const char* USB_CONFIG_DELIMIT = ",";
static const char* NETD_MESSAGE_DELIMIT = " ";

static const uint32_t BUF_SIZE = 1024;

static uint32_t SDK_VERSION;

inline uint32_t netdResponseType(uint32_t code)
{
  return (code / 100) * 100;
}

inline bool isBroadcastMessage(uint32_t code)
{
  uint32_t type = netdResponseType(code);
  return type == NETD_COMMAND_UNSOLICITED;
}

inline bool isError(uint32_t code)
{
  uint32_t type = netdResponseType(code);
  return type != NETD_COMMAND_PROCEEDING && type != NETD_COMMAND_OKAY;
}

inline bool isComplete(uint32_t code)
{
  uint32_t type = netdResponseType(code);
  return type != NETD_COMMAND_PROCEEDING;
}

inline bool isProceeding(uint32_t code)
{
  uint32_t type = netdResponseType(code);
  return type == NETD_COMMAND_PROCEEDING;
}

struct IFProperties {
  char gateway[PROPERTY_VALUE_MAX];
  char dns1[PROPERTY_VALUE_MAX];
  char dns2[PROPERTY_VALUE_MAX];
};

typedef Tuple3<NetdCommand*, CALLBACK, CommandChain*> QueueData;

static NetworkUtils* gNetworkUtils;

static nsTArray<QueueData> gCommandQueue;
static char gCurrentCommand[MAX_COMMAND_SIZE];
static CALLBACK gCurrentCallback = NULL;
static CommandChain* gCurrentChain = NULL;
static bool gPending = false;
static nsTArray<nsCString> gReason;

static void next(CommandChain* aChain, bool aError, NetworkResultOptions& aResult)
{
  if (aError) {
    ERROR_CALLBACK onError = aChain->getErrorCallback();
    if(onError) {
      aResult.mError = true;
      (*onError)(aChain->getParams(), aResult);
    }
    delete aChain;
    return;
  }
  COMMAND f = aChain->getNextCommand();
  if (!f) {
    delete aChain;
    return;
  }
  
  (*f)(aChain, next, aResult);
}

/**
 * Send command to netd.
 */
static void nextNetdCommand()
{
  if (gCommandQueue.IsEmpty() || gPending) {
    return;
  }
  QueueData data = gCommandQueue[0];
  gCommandQueue.RemoveElementAt(0);

  sprintf(gCurrentCommand, "%s", data.a->mData);
  gCurrentCallback = data.b;
  gCurrentChain = data.c;

  gPending = true;

  DEBUG("Sending \'%s\' command to netd.", gCurrentCommand);
  SendNetdCommand(data.a);
}

static void doCommand(const char* aCommand, CommandChain* aChain, CALLBACK aCallback)
{
  DEBUG("Preparing to send \'%s\' command...", aCommand);

  NetdCommand* netdCommand = new NetdCommand();

  // Android JB version adds sequence number to netd command.
  if (SDK_VERSION >= 16) {
    sprintf((char*)netdCommand->mData, "0 %s", aCommand);
  } else {
    sprintf((char*)netdCommand->mData, "%s", aCommand);
  }
  netdCommand->mSize = strlen((char*)netdCommand->mData) + 1;

  gCommandQueue.AppendElement(QueueData(netdCommand, aCallback, aChain));

  nextNetdCommand();
}

static void postMessage(NetworkResultOptions& aResult)
{
  (*(gNetworkUtils->mPostCallback))(aResult);
}

static void postMessage(NetworkParams& aOptions, NetworkResultOptions& aResult)
{
  aResult.mId = aOptions.mId;
  (*(gNetworkUtils->mPostCallback))(aResult);
}

static void sendBroadcastMessage(uint32_t code, char* reason)
{
  NetworkResultOptions result;
  switch(code) {
    case NETD_COMMAND_INTERFACE_CHANGE:
      result.mTopic = NS_ConvertUTF8toUTF16("netd-interface-change");
      break;
    case NETD_COMMAND_BANDWIDTH_CONTROLLER:
      result.mTopic = NS_ConvertUTF8toUTF16("netd-bandwidth-control");
      break;
    default:
      return;
  }

  result.mBroadcast = true;
  result.mReason = NS_ConvertUTF8toUTF16(reason);
  postMessage(result);
}

/**
 * Helper function to get the bit length from given mask.
 */
static uint32_t getMaskLength(const uint32_t mask)
{
  uint32_t netmask = ntohl(mask);
  uint32_t len = 0;
  while (netmask & 0x80000000) {
    len++;
    netmask = netmask << 1;
  }
  return len;
}

/**
 * Helper function to split string by seperator, store split result as an nsTArray.
 */
static void split(char* str, const char* sep, nsTArray<nsCString>& result)
{
  char *s = strtok(str, sep);
  while (s != NULL) {
    result.AppendElement(s);
    s = strtok(NULL, sep);
  }
}

/**
 * Helper function that implement join function.
 */
static void join(nsTArray<nsCString>& array, const char* sep, char* result)
{
  if (array.Length() > 0) {
    strcpy(result, array[0].get());
    for (uint32_t i = 1; i < array.Length(); i++) {
      strcat(result, sep);
      strcat(result, array[i].get());
    }
  }
}

/**
 * Get network interface properties from the system property table.
 */
static void getIFProperties(const char* ifname, IFProperties& prop)
{
  char key[PROPERTY_KEY_MAX];
  sprintf(key, "net.%s.gw", ifname);
  property_get(key, prop.gateway, "");
  sprintf(key, "net.%s.dns1", ifname);
  property_get(key, prop.dns1, "");
  sprintf(key, "net.%s.dns2", ifname);
  property_get(key, prop.dns2, "");
}

/*
 * Netd command function
 */
#define GET_CHAR(prop) NS_ConvertUTF16toUTF8(aChain->getParams().prop).get()
#define GET_FIELD(prop) aChain->getParams().prop

void wifiFirmwareReload(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "softap fwreload %s %s", GET_CHAR(mIfname), GET_CHAR(mMode));

  doCommand(command, aChain, aCallback);
}

void startAccessPointDriver(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  // Skip the command for sdk version >= 16.
  if (SDK_VERSION >= 16) {
    aResult.mResultCode = 0;
    aResult.mResultReason = NS_ConvertUTF8toUTF16("");
    aCallback(aChain, false, aResult);
    return;
  }

  char command[MAX_COMMAND_SIZE];
  sprintf(command, "softap start %s", GET_CHAR(mIfname));

  doCommand(command, aChain, aCallback);
}

void stopAccessPointDriver(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  // Skip the command for sdk version >= 16.
  if (SDK_VERSION >= 16) {
    aResult.mResultCode = 0;
    aResult.mResultReason = NS_ConvertUTF8toUTF16("");
    aCallback(aChain, false, aResult);
    return;
  }

  char command[MAX_COMMAND_SIZE];
  sprintf(command, "softap stop %s", GET_CHAR(mIfname));

  doCommand(command, aChain, aCallback);
}

/**
 * Command format for sdk version < 16
 *   Arguments:
 *     argv[2] - wlan interface
 *     argv[3] - SSID
 *     argv[4] - Security
 *     argv[5] - Key
 *     argv[6] - Channel
 *     argv[7] - Preamble
 *     argv[8] - Max SCB
 *
 * Command format for sdk version >= 16
 *   Arguments:
 *     argv[2] - wlan interface
 *     argv[3] - SSID
 *     argv[4] - Security
 *     argv[5] - Key
 */
void setAccessPoint(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  if (SDK_VERSION >= 16) {
    sprintf(command, "softap set %s \"%s\" %s \"%s\"",
                     GET_CHAR(mIfname),
                     GET_CHAR(mSsid),
                     GET_CHAR(mSecurity),
                     GET_CHAR(mKey));
  } else {
    sprintf(command, "softap set %s %s \"%s\" %s \"%s\" 6 0 8",
                     GET_CHAR(mIfname),
                     GET_CHAR(mWifictrlinterfacename),
                     GET_CHAR(mSsid),
                     GET_CHAR(mSecurity),
                     GET_CHAR(mKey));
  }

  doCommand(command, aChain, aCallback);
}

void cleanUpStream(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "nat disable %s %s 0", GET_CHAR(mPreInternalIfname), GET_CHAR(mPreExternalIfname));

  doCommand(command, aChain, aCallback);
}

void createUpStream(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "nat enable %s %s 0", GET_CHAR(mCurInternalIfname), GET_CHAR(mCurExternalIfname));

  doCommand(command, aChain, aCallback);
}

void startSoftAP(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  const char* command= "softap startap";
  doCommand(command, aChain, aCallback);
}

void stopSoftAP(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  const char* command= "softap stopap";
  doCommand(command, aChain, aCallback);
}

void getRxBytes(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "interface readrxcounter %s", GET_CHAR(mIfname));

  doCommand(command, aChain, aCallback);
}

void getTxBytes(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  NetworkParams& options = aChain->getParams();
  options.mRxBytes = atof(NS_ConvertUTF16toUTF8(aResult.mResultReason).get());

  char command[MAX_COMMAND_SIZE];
  sprintf(command, "interface readtxcounter %s", GET_CHAR(mIfname));

  doCommand(command, aChain, aCallback);
}

void enableAlarm(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  const char* command= "bandwidth enable";
  doCommand(command, aChain, aCallback);
}

void disableAlarm(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  const char* command= "bandwidth disable";
  doCommand(command, aChain, aCallback);
}

void setQuota(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "bandwidth setiquota %s %lld", GET_CHAR(mIfname), atoll("0xffffffffffffffff"));

  doCommand(command, aChain, aCallback);
}

void removeQuota(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "bandwidth removeiquota %s", GET_CHAR(mIfname));

  doCommand(command, aChain, aCallback);
}

void setAlarm(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "bandwidth setinterfacealert %s %ld", GET_CHAR(mIfname), GET_FIELD(mThreshold));

  doCommand(command, aChain, aCallback);
}

void setInterfaceUp(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  if (SDK_VERSION >= 16) {
    sprintf(command, "interface setcfg %s %s %s %s",
                     GET_CHAR(mIfname),
                     GET_CHAR(mIp),
                     GET_CHAR(mPrefix),
                     GET_CHAR(mLink));
  } else {
    sprintf(command, "interface setcfg %s %s %s [%s]",
                     GET_CHAR(mIfname),
                     GET_CHAR(mIp),
                     GET_CHAR(mPrefix),
                     GET_CHAR(mLink));
  }
  doCommand(command, aChain, aCallback);
}

void tetherInterface(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "tether interface add %s", GET_CHAR(mIfname));

  doCommand(command, aChain, aCallback);
}

void preTetherInterfaceList(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  if (SDK_VERSION >= 16) {
    sprintf(command, "tether interface list");
  } else {
    sprintf(command, "tether interface list 0");
  }

  doCommand(command, aChain, aCallback);
}

void postTetherInterfaceList(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  // Send the dummy command to continue the function chain.
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "%s", DUMMY_COMMAND);

  char buf[BUF_SIZE];
  const char* reason = NS_ConvertUTF16toUTF8(aResult.mResultReason).get();
  memcpy(buf, reason, strlen(reason));
  split(buf, INTERFACE_DELIMIT, GET_FIELD(mInterfaceList));

  doCommand(command, aChain, aCallback);
}

void setIpForwardingEnabled(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];

  if (GET_FIELD(mEnable)) {
    sprintf(command, "ipfwd enable");
  } else {
    // Don't disable ip forwarding because others interface still need it.
    // Send the dummy command to continue the function chain.
    if (GET_FIELD(mInterfaceList).Length() > 1) {
      sprintf(command, "%s", DUMMY_COMMAND);
    } else {
      sprintf(command, "ipfwd disable");
    }
  }

  doCommand(command, aChain, aCallback);
}

void tetheringStatus(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  const char* command= "tether status";
  doCommand(command, aChain, aCallback);
}

void stopTethering(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];

  // Don't stop tethering because others interface still need it.
  // Send the dummy to continue the function chain.
  if (GET_FIELD(mInterfaceList).Length() > 1) {
    sprintf(command, "%s", DUMMY_COMMAND);
  } else {
    sprintf(command, "tether stop");
  }

  doCommand(command, aChain, aCallback);
}

void startTethering(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];

  // We don't need to start tethering again.
  // Send the dummy command to continue the function chain.
  if (aResult.mResultReason.Find("started") != kNotFound) {
    sprintf(command, "%s", DUMMY_COMMAND);
  } else {
    sprintf(command, "tether start %s %s", GET_CHAR(mWifiStartIp), GET_CHAR(mWifiEndIp));

    // If usbStartIp/usbEndIp is not valid, don't append them since
    // the trailing white spaces will be parsed to extra empty args
    // See: http://androidxref.com/4.3_r2.1/xref/system/core/libsysutils/src/FrameworkListener.cpp#78
    if (!GET_FIELD(mUsbStartIp).IsEmpty() && !GET_FIELD(mUsbEndIp).IsEmpty()) {
      sprintf(command, "%s %s %s", command, GET_CHAR(mUsbStartIp), GET_CHAR(mUsbEndIp));
    }
  }

  doCommand(command, aChain, aCallback);
}

void untetherInterface(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "tether interface remove %s", GET_CHAR(mIfname));

  doCommand(command, aChain, aCallback);
}

void setDnsForwarders(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "tether dns set %s %s", GET_CHAR(mDns1), GET_CHAR(mDns2));

  doCommand(command, aChain, aCallback);
}

void enableNat(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "nat enable %s %s 0", GET_CHAR(mInternalIfname), GET_CHAR(mExternalIfname));

  doCommand(command, aChain, aCallback);
}

void disableNat(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  char command[MAX_COMMAND_SIZE];
  sprintf(command, "nat disable %s %s 0", GET_CHAR(mInternalIfname), GET_CHAR(mExternalIfname));

  doCommand(command, aChain, aCallback);
}

#undef GET_CHAR
#undef GET_FIELD

static COMMAND gWifiFailChain[] = {stopSoftAP,
                                   setIpForwardingEnabled,
                                   stopTethering};

static COMMAND gUSBFailChain[] = {stopSoftAP,
                                  setIpForwardingEnabled,
                                  stopTethering};

/*
 * Netd command success/fail function
 */
#define ASSIGN_FIELD(prop)  aResult.prop = aChain->getParams().prop;
#define ASSIGN_FIELD_VALUE(prop, value)  aResult.prop = value;

#define RUN_CHAIN(param, cmds, err)  uint32_t size = sizeof(cmds) / sizeof(COMMAND);  \
                                     CommandChain* chain = new CommandChain(param, cmds, size, err);  \
                                     NetworkResultOptions result;  \
                                     next(chain, false, result);

void wifiTetheringFail(NetworkParams& aOptions, NetworkResultOptions& aResult)
{
  // Notify the main thread.
  postMessage(aOptions, aResult);

  // If one of the stages fails, we try roll back to ensure
  // we don't leave the network systems in limbo.
  ASSIGN_FIELD_VALUE(mEnable, false)
  RUN_CHAIN(aOptions, gWifiFailChain, NULL)
}

void wifiTetheringSuccess(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  ASSIGN_FIELD(mEnable)
  postMessage(aChain->getParams(), aResult);
}

void usbTetheringFail(NetworkParams& aOptions, NetworkResultOptions& aResult)
{
  // Notify the main thread.
  postMessage(aOptions, aResult);
  // Try to roll back to ensure
  // we don't leave the network systems in limbo.
  // This parameter is used to disable ipforwarding.
  {
    aOptions.mEnable = false;
    RUN_CHAIN(aOptions, gUSBFailChain, NULL)
  }

  // Disable usb rndis function.
  {
    NetworkParams options;
    options.mEnable = false;
    options.mReport = false;
    gNetworkUtils->enableUsbRndis(options);
  }
}

void usbTetheringSuccess(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  ASSIGN_FIELD(mEnable)
  postMessage(aChain->getParams(), aResult);
}

void networkInterfaceStatsFail(NetworkParams& aOptions, NetworkResultOptions& aResult)
{
  postMessage(aOptions, aResult);
}

void networkInterfaceStatsSuccess(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  ASSIGN_FIELD(mRxBytes)
  ASSIGN_FIELD_VALUE(mTxBytes, atof(NS_ConvertUTF16toUTF8(aResult.mResultReason).get()))
  postMessage(aChain->getParams(), aResult);
}

void networkInterfaceAlarmFail(NetworkParams& aOptions, NetworkResultOptions& aResult)
{
  postMessage(aOptions, aResult);
}

void networkInterfaceAlarmSuccess(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  // TODO : error is not used , and it is conflict with boolean type error.
  // params.error = parseFloat(params.resultReason);
  postMessage(aChain->getParams(), aResult);
}

void updateUpStreamFail(NetworkParams& aOptions, NetworkResultOptions& aResult)
{
  postMessage(aOptions, aResult);
}

void updateUpStreamSuccess(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  ASSIGN_FIELD(mCurExternalIfname)
  ASSIGN_FIELD(mCurInternalIfname)
  postMessage(aChain->getParams(), aResult);
}

void setDhcpServerFail(NetworkParams& aOptions, NetworkResultOptions& aResult)
{
  aResult.mSuccess = false;
  postMessage(aOptions, aResult);
}

void setDhcpServerSuccess(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  aResult.mSuccess = true;
  postMessage(aChain->getParams(), aResult);
}

void wifiOperationModeFail(NetworkParams& aOptions, NetworkResultOptions& aResult)
{
  postMessage(aOptions, aResult);
}

void wifiOperationModeSuccess(CommandChain* aChain, CALLBACK aCallback, NetworkResultOptions& aResult)
{
  postMessage(aChain->getParams(), aResult);
}

#undef ASSIGN_FIELD
#undef ASSIGN_FIELD_VALUE

static COMMAND gUSBEnableChain[] = {setInterfaceUp,
                                    enableNat,
                                    setIpForwardingEnabled,
                                    tetherInterface,
                                    tetheringStatus,
                                    startTethering,
                                    setDnsForwarders,
                                    usbTetheringSuccess};

static COMMAND gUSBDisableChain[] = {untetherInterface,
                                     preTetherInterfaceList,
                                     postTetherInterfaceList,
                                     disableNat,
                                     setIpForwardingEnabled,
                                     stopTethering,
                                     usbTetheringSuccess};

static COMMAND gWifiEnableChain[] = {wifiFirmwareReload,
                                     startAccessPointDriver,
                                     setAccessPoint,
                                     startSoftAP,
                                     setInterfaceUp,
                                     tetherInterface,
                                     setIpForwardingEnabled,
                                     tetheringStatus,
                                     startTethering,
                                     setDnsForwarders,
                                     enableNat,
                                     wifiTetheringSuccess};

static COMMAND gWifiDisableChain[] = {stopSoftAP,
                                      stopAccessPointDriver,
                                      wifiFirmwareReload,
                                      untetherInterface,
                                      preTetherInterfaceList,
                                      postTetherInterfaceList,
                                      disableNat,
                                      setIpForwardingEnabled,
                                      stopTethering,
                                      wifiTetheringSuccess};

static COMMAND gStartDhcpServerChain[] = {setInterfaceUp,
                                          startTethering,
                                          setDhcpServerSuccess};

static COMMAND gStopDhcpServerChain[] = {stopTethering,
                                         setDhcpServerSuccess};

static COMMAND gNetworkInterfaceStatsChain[] = {getRxBytes,
                                                getTxBytes,
                                                networkInterfaceStatsSuccess};

static COMMAND gNetworkInterfaceEnableAlarmChain[] = {enableAlarm,
                                                      setQuota,
                                                      setAlarm,
                                                      networkInterfaceAlarmSuccess};

static COMMAND gNetworkInterfaceDisableAlarmChain[] = {removeQuota,
                                                       disableAlarm,
                                                       networkInterfaceAlarmSuccess};

static COMMAND gNetworkInterfaceSetAlarmChain[] = {setAlarm,
                                                   networkInterfaceAlarmSuccess};

static COMMAND gWifiOperationModeChain[] = {wifiFirmwareReload,
                                            wifiOperationModeSuccess};

static COMMAND gUpdateUpStreamChain[] = {cleanUpStream,
                                         createUpStream,
                                         updateUpStreamSuccess};

NetworkUtils::NetworkUtils(POSTMESSAGE post)
 : mPostCallback(post)
{
  mNetUtils = new NetUtils();

  char value[PROPERTY_VALUE_MAX];
  property_get("ro.build.version.sdk", value, NULL);
  SDK_VERSION = atoi(value);

  gNetworkUtils = this;
}

NetworkUtils::~NetworkUtils()
{
  delete mNetUtils;
}

#define GET_CHAR(prop) NS_ConvertUTF16toUTF8(aOptions.prop).get()

void NetworkUtils::ExecuteCommand(NetworkParams aOptions)
{
  DEBUG("received message: %s", GET_CHAR(mCmd));
  bool ret = true;

  if (aOptions.mCmd.EqualsLiteral("removeNetworkRoute")) {
    removeNetworkRoute(aOptions);                                            // OK
  } else if (aOptions.mCmd.EqualsLiteral("setDNS")) {
    setDNS(aOptions);                                                        // OK
  } else if (aOptions.mCmd.EqualsLiteral("setDefaultRouteAndDNS")) {
    setDefaultRouteAndDNS(aOptions);                                         // Ok
  } else if (aOptions.mCmd.EqualsLiteral("removeDefaultRoute")) {
    removeDefaultRoute(aOptions);                                            // OK
  } else if (aOptions.mCmd.EqualsLiteral("addHostRoute")) {
    addHostRoute(aOptions);                                                  // Ok
  } else if (aOptions.mCmd.EqualsLiteral("removeHostRoute")) {
    removeHostRoute(aOptions);                                               // Ok
  } else if (aOptions.mCmd.EqualsLiteral("removeHostRoutes")) {
    removeHostRoutes(aOptions);                                              // Ok
  } else if (aOptions.mCmd.EqualsLiteral("getNetworkInterfaceStats")) {
    getNetworkInterfaceStats(aOptions);                                      // Ok
  } else if (aOptions.mCmd.EqualsLiteral("setNetworkInterfaceAlarm")) {
    setNetworkInterfaceAlarm(aOptions);
  } else if (aOptions.mCmd.EqualsLiteral("enableNetworkInterfaceAlarm")) {
    enableNetworkInterfaceAlarm(aOptions);
  } else if (aOptions.mCmd.EqualsLiteral("disableNetworkInterfaceAlarm")) {
    disableNetworkInterfaceAlarm(aOptions);
  } else if (aOptions.mCmd.EqualsLiteral("setWifiOperationMode")) {
    setWifiOperationMode(aOptions);                                          // Ok
  } else if (aOptions.mCmd.EqualsLiteral("setDhcpServer")) {
    setDhcpServer(aOptions);                                                 // Ok
  } else if (aOptions.mCmd.EqualsLiteral("setWifiTethering")) {
    setWifiTethering(aOptions);                                              // Disable Not Ok
  } else if (aOptions.mCmd.EqualsLiteral("setUSBTethering")) {
    setUSBTethering(aOptions);                                               // Disable Not OK
  } else if (aOptions.mCmd.EqualsLiteral("enableUsbRndis")) {
    enableUsbRndis(aOptions);                                                // Ok
  } else if (aOptions.mCmd.EqualsLiteral("updateUpStream")) {
    updateUpStream(aOptions);                                                // Ok
  } else {
    WARN("unknon message");
    return;
  }

  if (!aOptions.mIsAsync) {
    NetworkResultOptions result;
    result.mRet = ret;
    postMessage(aOptions, result);
  }
}

/**
 * Handle received data from netd.
 */
void NetworkUtils::onNetdMessage(const NetdCommand& aCommand)
{  
  char* data = (char*)aCommand.mData;

  // get code & reason.
  char* result = strtok(data, NETD_MESSAGE_DELIMIT);

  if (!result) {
    nextNetdCommand();
    return;
  }
  uint32_t code = atoi(result);
  char* reason = NULL;

  if (!isBroadcastMessage(code) && SDK_VERSION >= 16) {
    strtok(NULL, NETD_MESSAGE_DELIMIT);
    reason = strtok(NULL, "\0");
  }

  if (isBroadcastMessage(code)) {
    DEBUG("Receiving broadcast message from netd.");
    DEBUG("          ==> Code: %d  Reason: %s", code, reason);
    sendBroadcastMessage(code, reason);
    nextNetdCommand();
    return;
  }

   // Set pending to false before we handle next command.
  DEBUG("Receiving \"%s\" command response from netd.", gCurrentCommand);
  DEBUG("          ==> Code: %d  Reason: %s", code, reason);

  gReason.AppendElement(nsCString(reason));

  // 1xx response code regards as command is proceeding, we need to wait for
  // final response code such as 2xx, 4xx and 5xx before sending next command.
  if (isProceeding(code)) {
    return;
  }

  if (isComplete(code)) {
    gPending = false;
  }

  if (gCurrentCallback) {
    char buf[BUF_SIZE];
    join(gReason, INTERFACE_DELIMIT, buf);

    NetworkResultOptions result;
    result.mResultCode = code;
    result.mResultReason = NS_ConvertUTF8toUTF16(buf);
    join(gReason, INTERFACE_DELIMIT, buf);
    (*gCurrentCallback)(gCurrentChain, isError(code), result);
    gReason.Clear();
  }

  // Handling pending commands if any.
  if (isComplete(code)) {
    nextNetdCommand();
  }
}

/**
 * Start/Stop DHCP server.
 */
bool NetworkUtils::setDhcpServer(NetworkParams& aOptions)
{
  if (aOptions.mEnabled) {
    aOptions.mWifiStartIp = aOptions.mStartIp;
    aOptions.mWifiEndIp = aOptions.mEndIp;
    aOptions.mIp = aOptions.mServerIp;
    aOptions.mPrefix = aOptions.mMaskLength;
    aOptions.mLink = NS_ConvertUTF8toUTF16("up");

    RUN_CHAIN(aOptions, gStartDhcpServerChain, setDhcpServerFail)
  } else {
    RUN_CHAIN(aOptions, gStopDhcpServerChain, setDhcpServerFail)
  }
  return true;
}

/**
 * Set DNS servers for given network interface.
 */
bool NetworkUtils::setDNS(NetworkParams& aOptions)
{
  IFProperties interfaceProperties;
  getIFProperties(GET_CHAR(mIfname), interfaceProperties);

  if (aOptions.mDns1_str.IsEmpty()) {
    property_set("net.dns1", interfaceProperties.dns1);
  } else {
    property_set("net.dns1", GET_CHAR(mDns1_str));
  }

  if (aOptions.mDns2_str.IsEmpty()) {
    property_set("net.dns2", interfaceProperties.dns2);
  } else {
    property_set("net.dns2", GET_CHAR(mDns2_str));
  }

  // Bump the DNS change property.
  char dnschange[PROPERTY_VALUE_MAX];
  property_get("net.dnschange", dnschange, "0");

  char num[PROPERTY_VALUE_MAX];
  sprintf(num, "%d", atoi(dnschange) + 1);
  property_set("net.dnschange", num);

  return true;
}

/**
 * Set default route and DNS servers for given network interface.
 */
bool NetworkUtils::setDefaultRouteAndDNS(NetworkParams& aOptions)
{
  if (!aOptions.mOldIfname.IsEmpty()) {
    mNetUtils->do_ifc_remove_default_route(GET_CHAR(mOldIfname));
  }

  IFProperties ifprops;
  getIFProperties(GET_CHAR(mIfname), ifprops);

  if (aOptions.mGateway_str.IsEmpty()) {
    mNetUtils->do_ifc_set_default_route(GET_CHAR(mIfname), inet_addr(ifprops.gateway));
  } else {
    mNetUtils->do_ifc_set_default_route(GET_CHAR(mIfname), inet_addr(GET_CHAR(mGateway_str)));
  }

  setDNS(aOptions);
  return true;
}

/**
 * Remove default route for given network interface.
 */
bool NetworkUtils::removeDefaultRoute(NetworkParams& aOptions)
{
  mNetUtils->do_ifc_remove_default_route(GET_CHAR(mIfname));
  return true;
}

/**
 * Add host route for given network interface.
 */
bool NetworkUtils::addHostRoute(NetworkParams& aOptions)
{
  uint32_t length = aOptions.mHostnames.Length();
  for (uint32_t i = 0; i < length; i++) {
    mNetUtils->do_ifc_add_route(GET_CHAR(mIfname), GET_CHAR(mHostnames[i]), 32, GET_CHAR(mGateway));
  }
  return true;
}

/**
 * Remove host route for given network interface.
 */
bool NetworkUtils::removeHostRoute(NetworkParams& aOptions)
{
  uint32_t length = aOptions.mHostnames.Length();
  for (uint32_t i = 0; i < length; i++) {
    mNetUtils->do_ifc_remove_route(GET_CHAR(mIfname), GET_CHAR(mHostnames[i]), 32, GET_CHAR(mGateway));
  }
  return true;
}

/**
 * Remove the routes associated with the named interface.
 */
bool NetworkUtils::removeHostRoutes(NetworkParams& aOptions)
{
  mNetUtils->do_ifc_remove_host_routes(GET_CHAR(mIfname));
  return true;
}

bool NetworkUtils::removeNetworkRoute(NetworkParams& aOptions)
{
  uint32_t ip = inet_addr(GET_CHAR(mIp));
  uint32_t netmask = inet_addr(GET_CHAR(mNetmask));
  uint32_t subnet = ip & netmask;
  uint32_t prefixLength = getMaskLength(netmask);
  const char* gateway = "0.0.0.0";
  struct in_addr addr;
  addr.s_addr = subnet;
  const char* dst = inet_ntoa(addr);

  mNetUtils->do_ifc_remove_default_route(GET_CHAR(mIfname));
  mNetUtils->do_ifc_remove_route(GET_CHAR(mIfname), dst, prefixLength, gateway);
  return true;
}

bool NetworkUtils::getNetworkInterfaceStats(NetworkParams& aOptions)
{
  DEBUG("getNetworkInterfaceStats: %s", GET_CHAR(mIfname));
  aOptions.mRxBytes = -1;
  aOptions.mTxBytes = -1;

  RUN_CHAIN(aOptions, gNetworkInterfaceStatsChain, networkInterfaceStatsFail);
  return  true;
}

bool NetworkUtils::setNetworkInterfaceAlarm(NetworkParams& aOptions)
{
  DEBUG("setNetworkInterfaceAlarms: %s", GET_CHAR(mIfname));
  RUN_CHAIN(aOptions, gNetworkInterfaceSetAlarmChain, networkInterfaceAlarmFail);
  return true;
}

bool NetworkUtils::enableNetworkInterfaceAlarm(NetworkParams& aOptions)
{
  DEBUG("enableNetworkInterfaceAlarm: %s", GET_CHAR(mIfname));
  RUN_CHAIN(aOptions, gNetworkInterfaceEnableAlarmChain, networkInterfaceAlarmFail);
  return true;
}

bool NetworkUtils::disableNetworkInterfaceAlarm(NetworkParams& aOptions)
{
  DEBUG("disableNetworkInterfaceAlarms: %s", GET_CHAR(mIfname));
  RUN_CHAIN(aOptions, gNetworkInterfaceDisableAlarmChain, networkInterfaceAlarmFail);
  return true;
}

/**
 * handling main thread's reload Wifi firmware request
 */
bool NetworkUtils::setWifiOperationMode(NetworkParams& aOptions)
{
  DEBUG("setWifiOperationMode: %s %s", GET_CHAR(mIfname), GET_CHAR(mMode));
  RUN_CHAIN(aOptions, gWifiOperationModeChain, wifiOperationModeFail);
  return true;
}

/**
 * handling main thread's enable/disable WiFi Tethering request
 */
bool NetworkUtils::setWifiTethering(NetworkParams& aOptions)
{
  bool enable = aOptions.mEnable;
  IFProperties interfaceProperties;
  getIFProperties(GET_CHAR(mExternalIfname), interfaceProperties);

  if (strcmp(interfaceProperties.dns1, "")) {
    aOptions.mDns1 = NS_ConvertUTF8toUTF16(interfaceProperties.dns1);
  }
  if (strcmp(interfaceProperties.dns2, "")) {
    aOptions.mDns2 = NS_ConvertUTF8toUTF16(interfaceProperties.dns2);
  }
  dumpParams(aOptions, "WIFI");

  if (enable) {
    DEBUG("Starting Wifi Tethering on %s <-> %s",
           GET_CHAR(mInternalIfname), GET_CHAR(mExternalIfname));
    RUN_CHAIN(aOptions, gWifiEnableChain, wifiTetheringFail)
  } else {
    DEBUG("Stopping Wifi Tethering on %s <-> %s",
           GET_CHAR(mInternalIfname), GET_CHAR(mExternalIfname));    
    RUN_CHAIN(aOptions, gWifiDisableChain, wifiTetheringFail)
  }
  return true;
}

bool NetworkUtils::setUSBTethering(NetworkParams& aOptions)
{
  bool enable = aOptions.mEnable;
  IFProperties interfaceProperties;
  getIFProperties(GET_CHAR(mExternalIfname), interfaceProperties);

  if (strcmp(interfaceProperties.dns1, "")) {
    aOptions.mDns1 = NS_ConvertUTF8toUTF16(interfaceProperties.dns1);
  }
  if (strcmp(interfaceProperties.dns2, "")) {
    aOptions.mDns2 = NS_ConvertUTF8toUTF16(interfaceProperties.dns2);
  }
  dumpParams(aOptions, "USB");

  if (enable) {
    DEBUG("Starting USB Tethering on %s <-> %s",
           GET_CHAR(mInternalIfname), GET_CHAR(mExternalIfname));
    RUN_CHAIN(aOptions, gUSBEnableChain, usbTetheringFail)
  } else {
    DEBUG("Stopping USB Tethering on %s <-> %s",
           GET_CHAR(mInternalIfname), GET_CHAR(mExternalIfname));
    RUN_CHAIN(aOptions, gUSBDisableChain, usbTetheringFail)
  }
  return true;
}

void NetworkUtils::checkUsbRndisState(NetworkParams& aOptions)
{
  static uint32_t retry = 0;

  char currentState[PROPERTY_VALUE_MAX];
  property_get(SYS_USB_STATE_PROPERTY, currentState, NULL);

  nsTArray<nsCString> stateFuncs;
  split(currentState, USB_CONFIG_DELIMIT, stateFuncs);
  bool rndisPresent = stateFuncs.Contains(nsCString(USB_FUNCTION_RNDIS));

  if (aOptions.mEnable == rndisPresent) {
    NetworkResultOptions result;
    result.mEnable = aOptions.mEnable;
    result.mResult = true;
    postMessage(aOptions, result);
    retry = 0;
    return;
  } 
  if (retry < USB_FUNCTION_RETRY_TIMES) {
    retry++;
    usleep(USB_FUNCTION_RETRY_INTERVAL * 1000);
    checkUsbRndisState(aOptions);
    return;
  }

  NetworkResultOptions result;
  result.mResult = false;
  postMessage(aOptions, result);
  retry = 0;
}

/**
 * Modify usb function's property to turn on USB RNDIS function
 */
bool NetworkUtils::enableUsbRndis(NetworkParams& aOptions)
{
  bool report = aOptions.mReport;

  // For some reason, rndis doesn't play well with diag,modem,nmea.
  // So when turning rndis on, we set sys.usb.config to either "rndis"
  // or "rndis,adb". When turning rndis off, we go back to
  // persist.sys.usb.config.
  //
  // On the otoro/unagi, persist.sys.usb.config should be one of:
  //
  //    diag,modem,nmea,mass_storage
  //    diag,modem,nmea,mass_storage,adb
  //
  // When rndis is enabled, sys.usb.config should be one of:
  //
  //    rdnis
  //    rndis,adb
  //
  // and when rndis is disabled, it should revert to persist.sys.usb.config

  char currentConfig[PROPERTY_VALUE_MAX];
  property_get(SYS_USB_CONFIG_PROPERTY, currentConfig, NULL);

  nsTArray<nsCString> configFuncs;
  split(currentConfig, USB_CONFIG_DELIMIT, configFuncs);

  char persistConfig[PROPERTY_VALUE_MAX];
  property_get(PERSIST_SYS_USB_CONFIG_PROPERTY, persistConfig, NULL);

  nsTArray<nsCString> persistFuncs;
  split(persistConfig, USB_CONFIG_DELIMIT, persistFuncs);

  if (aOptions.mEnable) {
    configFuncs.Clear();
    configFuncs.AppendElement(nsCString(USB_FUNCTION_RNDIS));
    if (persistFuncs.Contains(nsCString(USB_FUNCTION_ADB))) {
      configFuncs.AppendElement(nsCString(USB_FUNCTION_ADB));
    }
  } else {
    // We're turning rndis off, revert back to the persist setting.
    // adb will already be correct there, so we don't need to do any
    // further adjustments.
    configFuncs = persistFuncs;
  }

  char newConfig[PROPERTY_VALUE_MAX] = "";
  property_get(SYS_USB_CONFIG_PROPERTY, currentConfig, NULL);
  join(configFuncs, USB_CONFIG_DELIMIT, newConfig);
  if (strcmp(currentConfig, newConfig)) {
    property_set(SYS_USB_CONFIG_PROPERTY, newConfig);
  }

  // Trigger the timer to check usb state and report the result to NetworkManager.
  if (report) {
    usleep(USB_FUNCTION_RETRY_INTERVAL * 1000);
    checkUsbRndisState(aOptions);
  }
  return true;
}

/**
 * handling upstream interface change event.
 */
bool NetworkUtils::updateUpStream(NetworkParams& aOptions)
{
  RUN_CHAIN(aOptions, gUpdateUpStreamChain, updateUpStreamFail)
  return true;
}

void NetworkUtils::dumpParams(NetworkParams& aOptions, const char* aType)
{
#ifdef USE_DEBUG
  DEBUG("Dump params:");
  DEBUG("     ifname: %s", GET_CHAR(mIfname));
  DEBUG("     ip: %s", GET_CHAR(mIp));
  DEBUG("     link: %s", GET_CHAR(mLink));
  DEBUG("     prefix: %s", GET_CHAR(mPrefix));
  DEBUG("     wifiStartIp: %s", GET_CHAR(mWifiStartIp));
  DEBUG("     wifiEndIp: %s", GET_CHAR(mWifiEndIp));
  DEBUG("     usbStartIp: %s", GET_CHAR(mUsbStartIp));
  DEBUG("     usbEndIp: %s", GET_CHAR(mUsbEndIp));
  DEBUG("     dnsserver1: %s", GET_CHAR(mDns1));
  DEBUG("     dnsserver2: %s", GET_CHAR(mDns2));
  DEBUG("     internalIfname: %s", GET_CHAR(mInternalIfname));
  DEBUG("     externalIfname: %s", GET_CHAR(mExternalIfname));
  if (!strcmp(aType, "WIFI")) {
    DEBUG("     wifictrlinterfacename: %s", GET_CHAR(mWifictrlinterfacename));
    DEBUG("     ssid: %s", GET_CHAR(mSsid));
    DEBUG("     security: %s", GET_CHAR(mSecurity));
    DEBUG("     key: %s", GET_CHAR(mKey));
  }
#endif
}

#undef GET_CHAR
