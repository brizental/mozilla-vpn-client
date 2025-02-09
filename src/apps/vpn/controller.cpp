/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "controller.h"

#include "controllerimpl.h"
#include "dnshelper.h"
#include "feature.h"
#include "frontend/navigator.h"
#include "ipaddress.h"
#include "leakdetector.h"
#include "logger.h"
#include "models/server.h"
#include "mozillavpn.h"
#include "rfc/rfc1112.h"
#include "rfc/rfc1918.h"
#include "rfc/rfc4193.h"
#include "rfc/rfc4291.h"
#include "serveri18n.h"
#include "settingsholder.h"
#include "tasks/controlleraction/taskcontrolleraction.h"
#include "tasks/heartbeat/taskheartbeat.h"
#include "taskscheduler.h"

#if defined(MZ_LINUX)
#  include "platforms/linux/linuxcontroller.h"
#elif defined(MZ_MACOS) || defined(MZ_WINDOWS)
#  include "localsocketcontroller.h"
#elif defined(MZ_IOS)
#  include "platforms/ios/ioscontroller.h"
#elif defined(MZ_ANDROID)
#  include "platforms/android/androidcontroller.h"
#else
#  include "platforms/dummy/dummycontroller.h"
#endif

constexpr const uint32_t TIMER_MSEC = 1000;

// X connection retries.
constexpr const int CONNECTION_MAX_RETRY = 9;

constexpr const uint32_t CONFIRMING_TIMOUT_SEC = 10;
constexpr const uint32_t HANDSHAKE_TIMEOUT_SEC = 15;

#ifndef MZ_IOS
// The Mullvad proxy services are located at internal IPv4 addresses in the
// 10.124.0.0/20 address range, which is a subset of the 10.0.0.0/8 Class-A
// private address range.
constexpr const char* MULLVAD_PROXY_RANGE = "10.124.0.0";
constexpr const int MULLVAD_PROXY_RANGE_LENGTH = 20;
#endif

namespace {
Logger logger("Controller");

Controller::Reason stateToReason(Controller::State state) {
  if (state == Controller::StateSwitching) {
    return Controller::ReasonSwitching;
  }

  if (state == Controller::StateConfirming) {
    return Controller::ReasonConfirming;
  }

  return Controller::ReasonNone;
}

}  // namespace

Controller::Controller() {
  MZ_COUNT_CTOR(Controller);

  m_connectingTimer.setSingleShot(true);
  m_handshakeTimer.setSingleShot(true);

  connect(&m_timer, &QTimer::timeout, this, &Controller::timerTimeout);

  connect(&m_connectingTimer, &QTimer::timeout, this, [this]() {
    m_enableDisconnectInConfirming = true;
    emit enableDisconnectInConfirmingChanged();
  });

  connect(&m_handshakeTimer, &QTimer::timeout, this,
          &Controller::handshakeTimeout);
}

Controller::~Controller() { MZ_COUNT_DTOR(Controller); }

Controller::State Controller::state() const { return m_state; }

void Controller::initialize() {
  logger.debug() << "Initializing the controller";

  if (m_state != StateInitializing) {
    setState(StateInitializing);
  }

  // Let's delete the previous controller before creating a new one.
  if (m_impl) {
    m_impl.reset(nullptr);
  }

#if defined(MZ_LINUX)
  m_impl.reset(new LinuxController());
#elif defined(MZ_MACOS) || defined(MZ_WINDOWS)
  m_impl.reset(new LocalSocketController());
#elif defined(MZ_IOS)
  m_impl.reset(new IOSController());
#elif defined(MZ_ANDROID)
  m_impl.reset(new AndroidController());
#else
  m_impl.reset(new DummyController());
#endif

  connect(m_impl.get(), &ControllerImpl::connected, this,
          &Controller::connected);
  connect(m_impl.get(), &ControllerImpl::disconnected, this,
          &Controller::disconnected);
  connect(m_impl.get(), &ControllerImpl::initialized, this,
          &Controller::implInitialized);
  connect(m_impl.get(), &ControllerImpl::statusUpdated, this,
          &Controller::statusUpdated);
  connect(this, &Controller::stateChanged, this,
          &Controller::maybeEnableDisconnectInConfirming);

  connect(&m_ping_canary, &PingHelper::pingSentAndReceived, this, [this]() {
    m_ping_canary.stop();
    m_ping_received = true;
    logger.info() << "Canary Ping Succeeded";
  });

  connect(SettingsHolder::instance(), &SettingsHolder::transactionBegan, this,
          [this]() { m_connectedBeforeTransaction = m_state == StateOn; });

  connect(SettingsHolder::instance(),
          &SettingsHolder::transactionAboutToRollBack, this, [this]() {
            if (!m_connectedBeforeTransaction)
              MozillaVPN::instance()->deactivate();
          });

  connect(SettingsHolder::instance(), &SettingsHolder::transactionRolledBack,
          this, [this]() {
            if (m_connectedBeforeTransaction)
              MozillaVPN::instance()->activate();
          });

  MozillaVPN* vpn = MozillaVPN::instance();

  const Device* device = vpn->deviceModel()->currentDevice(vpn->keys());
  m_impl->initialize(device, vpn->keys());

  connect(SettingsHolder::instance(), &SettingsHolder::serverDataChanged, this,
          &Controller::serverDataChanged);
}

void Controller::implInitialized(bool status, bool a_connected,
                                 const QDateTime& connectionDate) {
  logger.debug() << "Controller initialized with status:" << status
                 << "connected:" << a_connected
                 << "connectionDate:" << connectionDate.toString();

  Q_ASSERT(m_state == StateInitializing);

  if (!status) {
    REPORTERROR(ErrorHandler::ControllerError, "controller");
    setState(StateOff);
    return;
  }

  if (processNextStep()) {
    setState(StateOff);
    return;
  }

  setState(a_connected ? StateOn : StateOff);

  // If we are connected already at startup time, we can trigger the connection
  // sequence of tasks.
  if (a_connected) {
    m_connectedTimeInUTC = connectionDate.isValid()
                               ? connectionDate.toUTC()
                               : QDateTime::currentDateTimeUtc();
    emit timeChanged();
    m_timer.start(TIMER_MSEC);
  }
}

bool Controller::activate() {
  logger.debug() << "Activation" << m_state;

  if (m_state != StateOff && m_state != StateSwitching) {
    logger.debug() << "Already connected";
    return false;
  }

  if (m_state == StateOff) {
    if (m_portalDetected) {
      emit activationBlockedForCaptivePortal();
      Navigator::instance()->requestScreen(Navigator::ScreenCaptivePortal);

      m_portalDetected = false;
      return true;
    }
    setState(StateConnecting);
  }

  clearRetryCounter();

  activateInternal(stateToReason(m_state));
  return true;
}

void Controller::activateInternal(Reason reason, bool forcePort53) {
  logger.debug() << "Activation internal";
  Q_ASSERT(m_impl);

  clearConnectedTime();
  m_handshakeTimer.stop();
  m_activationQueue.clear();

  MozillaVPN* vpn = MozillaVPN::instance();

  Server exitServer =
      Server::weightChooser(vpn->currentServer()->exitServers());
  if (!exitServer.initialized()) {
    logger.error() << "Empty exit server list in state" << m_state;
    serverUnavailable();
    return;
  }

  vpn->currentServer()->setExitServerPublicKey(exitServer.publicKey());

  // Prepare the exit server's connection data.
  HopConnection exitHop;
  exitHop.m_server = exitServer;
  exitHop.m_hopindex = 0;
  exitHop.m_allowedIPAddressRanges = getAllowedIPAddressRanges(exitServer);
  exitHop.m_excludedAddresses = getExcludedAddresses();
  exitHop.m_dnsServer =
      QHostAddress(DNSHelper::getDNS(exitServer.ipv4Gateway()));
  logger.debug() << "DNS Set" << exitHop.m_dnsServer.toString();

  SettingsHolder* settingsHolder = SettingsHolder::instance();
  // Splittunnel-feature could have been disabled due to a driver conflict.
  if (Feature::get(Feature::Feature_splitTunnel)->isSupported() &&
      settingsHolder->protectSelectedApps()) {
    exitHop.m_vpnDisabledApps = settingsHolder->vpnDisabledApps();
  }

  // For single-hop connections, exclude the entry server
  if (!Feature::get(Feature::Feature_multiHop)->isSupported() ||
      !vpn->currentServer()->multihop()) {
    exitHop.m_excludedAddresses.append(exitHop.m_server.ipv4AddrIn());
    exitHop.m_excludedAddresses.append(exitHop.m_server.ipv6AddrIn());

    // If requested, force the use of port 53/DNS.
    if (forcePort53) {
      exitHop.m_server.forcePort(53);
    }
    // For single-hop, they are the same
    vpn->currentServer()->setEntryServerPublicKey(exitServer.publicKey());
  }
  // For controllers that support multiple hops, create a queue of connections.
  // The entry server should start first, followed by the exit server.
  else if (m_impl->multihopSupported()) {
    HopConnection hop;
    hop.m_server = Server::weightChooser(vpn->currentServer()->entryServers());
    vpn->currentServer()->setEntryServerPublicKey(hop.m_server.publicKey());
    if (!hop.m_server.initialized()) {
      logger.error() << "Empty entry server list in state" << m_state;
      serverUnavailable();
      return;
    }
    // If requested, force the use of port 53/DNS.
    if (forcePort53) {
      hop.m_server.forcePort(53);
    }

    hop.m_hopindex = 1;
    hop.m_allowedIPAddressRanges.append(IPAddress(exitServer.ipv4AddrIn()));
    hop.m_allowedIPAddressRanges.append(IPAddress(exitServer.ipv6AddrIn()));
    hop.m_excludedAddresses.append(hop.m_server.ipv4AddrIn());
    hop.m_excludedAddresses.append(hop.m_server.ipv6AddrIn());
    m_activationQueue.append(hop);
  }
  // Otherwise, we can approximate multihop support by redirecting the
  // connection to the exit server via the multihop port.
  else {
    Server entryServer =
        Server::weightChooser(vpn->currentServer()->entryServers());
    vpn->currentServer()->setEntryServerPublicKey(entryServer.publicKey());
    if (!entryServer.initialized()) {
      logger.error() << "Empty entry server list in state" << m_state;
      serverUnavailable();
      return;
    }
    // NOTE: For platforms without multihop support, we cannot emulate multihop
    // and use port 53 at the same time. If the user has selected both options
    // then let's choose multihop.
    exitHop.m_server.fromMultihop(exitHop.m_server, entryServer);
    exitHop.m_excludedAddresses.append(entryServer.ipv4AddrIn());
    exitHop.m_excludedAddresses.append(entryServer.ipv6AddrIn());
  }

  m_activationQueue.append(exitHop);
  m_ping_received = false;
  m_ping_canary.start(m_activationQueue.first().m_server.ipv4AddrIn(),
                      "0.0.0.0/0");
  logger.info() << "Canary Ping Started";
  activateNext(reason);
}

void Controller::activateNext(Reason reason) {
  MozillaVPN* vpn = MozillaVPN::instance();
  const Device* device = vpn->deviceModel()->currentDevice(vpn->keys());
  if (device == nullptr) {
    REPORTERROR(ErrorHandler::AuthenticationError, "controller");
    vpn->reset(false);
    return;
  }
  const HopConnection& hop = m_activationQueue.first();

  logger.debug() << "Activating peer" << logger.keys(hop.m_server.publicKey());
  m_handshakeTimer.start(HANDSHAKE_TIMEOUT_SEC * 1000);
  m_impl->activate(hop, device, vpn->keys(), reason);

  // Move to the confirming state if we are awaiting any connection handshakes.
  setState(StateConfirming);
}

bool Controller::silentSwitchServers() {
  logger.debug() << "Silently switch servers";

  if (m_state != StateOn) {
    logger.warning() << "Cannot silent switch if not on";
    return false;
  }

  MozillaVPN* vpn = MozillaVPN::instance();

  // Set a cooldown timer on the current server.
  QList<Server> servers = vpn->currentServer()->exitServers();
  Q_ASSERT(!servers.isEmpty());

  if (servers.length() <= 1) {
    logger.warning()
        << "Cannot silent switch servers because there is only one available";
    return false;
  }
  vpn->serverCountryModel()->setServerCooldown(
      vpn->currentServer()->exitServerPublicKey());

  // Activate the first connection to kick off the server switch.
  activateInternal(ReasonSwitching);
  return true;
}

bool Controller::deactivate() {
  logger.debug() << "Deactivation" << m_state;

  if ((m_state != StateOn) && (m_state != StateSwitching) &&
      (m_state != StateConfirming) && (m_state != StateConnecting)) {
    logger.warning() << "Already disconnected";
    return false;
  }

  if ((m_state == StateOn) || (m_state == StateConfirming) ||
      (m_state == StateConnecting)) {
    setState(StateDisconnecting);
  }
  m_ping_canary.stop();
  m_handshakeTimer.stop();
  m_activationQueue.clear();
  clearConnectedTime();
  clearRetryCounter();

  Q_ASSERT(m_impl);
  m_impl->deactivate(stateToReason(m_state));
  return true;
}

void Controller::connected(const QString& pubkey) {
  logger.debug() << "handshake completed with:" << logger.keys(pubkey);
  if (m_activationQueue.isEmpty()) {
    MozillaVPN* vpn = MozillaVPN::instance();
    Q_ASSERT(vpn);
    if (vpn->currentServer()->exitServerPublicKey() != pubkey) {
      logger.warning() << "Unexpected handshake: no pending connections.";
      return;
    }
    // Continue anyways if the VPN service was activated externally.
    logger.info() << "Unexpected handshake: external VPN activation.";
  } else if (m_activationQueue.first().m_server.publicKey() != pubkey) {
    logger.warning() << "Unexpected handshake: public key mismatch.";
    return;
  } else {
    // Start the next connection if there is more work to do.
    m_activationQueue.removeFirst();
    if (!m_activationQueue.isEmpty()) {
      activateNext(stateToReason(m_state));
      return;
    }
  }
  m_handshakeTimer.stop();
  m_ping_canary.stop();

  // Clear the retry counter after all connections have succeeded.
  m_connectionRetry = 0;
  emit connectionRetryChanged();

  // We have succesfully completed all pending connections.
  logger.debug() << "Connected from state:" << m_state;
  setState(StateOn);
  resetConnectedTime();

  if (m_nextStep != None) {
    deactivate();
    return;
  }
}

void Controller::handshakeTimeout() {
  logger.debug() << "Timeout while waiting for handshake";

  MozillaVPN* vpn = MozillaVPN::instance();
  Q_ASSERT(!m_activationQueue.isEmpty());

  // Block the offending server and try again.
  HopConnection& hop = m_activationQueue.first();
  vpn->serverCountryModel()->setServerCooldown(hop.m_server.publicKey());

  emit handshakeFailed(hop.m_server.publicKey());

  if (m_nextStep != None) {
    deactivate();
    return;
  }

  // Try again, again if there are sufficient retries left.
  ++m_connectionRetry;
  emit connectionRetryChanged();
  if (m_connectionRetry == 1) {
    logger.info() << "Connection Attempt: Using Port 53 Option this time.";
    // On the first retry, opportunisticly try again using the port 53
    // option enabled, if that feature is disabled.
    activateInternal(stateToReason(m_state), true);
    return;
  } else if (m_connectionRetry < CONNECTION_MAX_RETRY) {
    activateInternal(stateToReason(m_state));
    return;
  }

  // Otherwise, the give up and report the location as unavailable.
  logger.error() << "Connection retries exhausted, giving up";
  serverUnavailable();
}

void Controller::disconnected() {
  logger.debug() << "Disconnected from state:" << m_state;

  clearConnectedTime();
  clearRetryCounter();

  NextStep nextStep = m_nextStep;

  if (processNextStep()) {
    setState(StateOff);
    return;
  }

  if (nextStep == None && m_state == StateSwitching) {
    activate();
    return;
  }

  setState(StateOff);
}

void Controller::timerTimeout() {
  Q_ASSERT(m_state == StateOn);
  emit timeChanged();
}

void Controller::quit() {
  logger.debug() << "Quitting";

  if (m_state == StateInitializing || m_state == StateOff) {
    emit readyToQuit();
    return;
  }

  m_nextStep = Quit;

  if ((m_state == StateOn) || (m_state == StateSwitching) ||
      (m_state == StateConnecting)) {
    deactivate();
    return;
  }
}

void Controller::backendFailure() {
  logger.error() << "backend failure";

  if (m_state == StateInitializing || m_state == StateOff) {
    emit readyToBackendFailure();
    return;
  }

  m_nextStep = BackendFailure;

  if ((m_state == StateOn) || (m_state == StateSwitching) ||
      (m_state == StateConnecting) || (m_state == StateConfirming)) {
    deactivate();
    return;
  }
}

void Controller::serverUnavailable() {
  logger.error() << "server unavailable";

  m_nextStep = ServerUnavailable;

  if ((m_state == StateOn) || (m_state == StateSwitching) ||
      (m_state == StateConnecting) || (m_state == StateConfirming)) {
    deactivate();
    return;
  }
}

void Controller::updateRequired() {
  logger.warning() << "Update required";

  if (m_state == StateOff) {
    emit readyToUpdate();
    return;
  }

  m_nextStep = Update;

  if (m_state == StateOn) {
    deactivate();
    return;
  }
}

void Controller::logout() {
  logger.debug() << "Logout";

  MozillaVPN::instance()->logout();

  if (m_state == StateOff) {
    return;
  }

  m_nextStep = Disconnect;

  if (m_state == StateOn) {
    deactivate();
    return;
  }
}

bool Controller::processNextStep() {
  NextStep nextStep = m_nextStep;
  m_nextStep = None;

  if (nextStep == Quit) {
    emit readyToQuit();
    return true;
  }

  if (nextStep == Update) {
    emit readyToUpdate();
    return true;
  }

  if (nextStep == BackendFailure) {
    emit readyToBackendFailure();
    return true;
  }

  if (nextStep == ServerUnavailable) {
    logger.info() << "Server Unavailable - Ping succeeded: " << m_ping_received;

    emit readyToServerUnavailable(m_ping_received);
    return true;
  }

  return false;
}

void Controller::maybeEnableDisconnectInConfirming() {
  if (m_state == StateConfirming) {
    m_enableDisconnectInConfirming = false;
    emit enableDisconnectInConfirmingChanged();
    m_connectingTimer.start(CONFIRMING_TIMOUT_SEC * 1000);
  } else {
    m_enableDisconnectInConfirming = false;
    emit enableDisconnectInConfirmingChanged();
    m_connectingTimer.stop();
  }
}

void Controller::setState(State state) {
  if (m_state == state) {
    return;
  }
  logger.debug() << "Setting state:" << state;
  m_state = state;
  emit stateChanged();
}

qint64 Controller::time() const {
  if (m_connectedTimeInUTC.isValid()) {
    return m_connectedTimeInUTC.secsTo(QDateTime::currentDateTimeUtc());
  }
  return 0;
}

void Controller::getBackendLogs(
    std::function<void(const QString&)>&& a_callback) {
  std::function<void(const QString&)> callback = std::move(a_callback);

  if (!m_impl) {
    callback(QString());
    return;
  }

  m_impl->getBackendLogs(std::move(callback));
}

void Controller::cleanupBackendLogs() {
  if (m_impl) {
    m_impl->cleanupBackendLogs();
  }
}

void Controller::getStatus(
    std::function<void(const QString& serverIpv4Gateway,
                       const QString& deviceIpv4Address, uint64_t txByte,
                       uint64_t rxBytes)>&& a_callback) {
  logger.debug() << "check status";

  std::function<void(const QString& serverIpv4Gateway,
                     const QString& deviceIpv4Address, uint64_t txBytes,
                     uint64_t rxBytes)>
      callback = std::move(a_callback);

  if (m_state != StateOn && m_state != StateConfirming) {
    callback(QString(), QString(), 0, 0);
    return;
  }

  bool requestStatus = m_getStatusCallbacks.isEmpty();

  m_getStatusCallbacks.append(std::move(callback));

  if (m_impl && requestStatus) {
    m_impl->checkStatus();
  }
}

void Controller::statusUpdated(const QString& serverIpv4Gateway,
                               const QString& deviceIpv4Address,
                               uint64_t txBytes, uint64_t rxBytes) {
  logger.debug() << "Status updated";
  QList<std::function<void(const QString& serverIpv4Gateway,
                           const QString& deviceIpv4Address, uint64_t txBytes,
                           uint64_t rxBytes)>>
      list;

  list.swap(m_getStatusCallbacks);
  for (const std::function<void(
           const QString&serverIpv4Gateway, const QString&deviceIpv4Address,
           uint64_t txBytes, uint64_t rxBytes)>&func : list) {
    func(serverIpv4Gateway, deviceIpv4Address, txBytes, rxBytes);
  }
}

QList<IPAddress> Controller::getAllowedIPAddressRanges(
    const Server& exitServer) {
  logger.debug() << "Computing the allowed IP addresses";

  QList<IPAddress> list;

#ifdef MZ_IOS
  // Note: On iOS, we use the `excludeLocalNetworks` flag to ensure
  // LAN traffic is allowed through. This is in the swift code.

  Q_UNUSED(exitServer);

  logger.debug() << "Catch all IPv4";
  list.append(IPAddress("0.0.0.0/0"));

  logger.debug() << "Catch all IPv6";
  list.append(IPAddress("::0/0"));
#else
  QList<IPAddress> excludeIPv4s;
  QList<IPAddress> excludeIPv6s;
  // For multi-hop connections, the last entry in the server list is the
  // ingress node to the network of wireguard servers, and must not be
  // routed through the VPN.

  // filtering out the RFC1918 local area network
  logger.debug() << "Filtering out the local area networks (rfc 1918)";
  excludeIPv4s.append(RFC1918::ipv4());

  logger.debug() << "Filtering out the local area networks (rfc 4193)";
  excludeIPv6s.append(RFC4193::ipv6());

  logger.debug() << "Filtering out multicast addresses";
  excludeIPv4s.append(RFC1112::ipv4MulticastAddressBlock());
  excludeIPv6s.append(RFC4291::ipv6MulticastAddressBlock());

  // Allow access to the internal gateway addresses.
  logger.debug() << "Allow the IPv4 gateway:" << exitServer.ipv4Gateway();
  list.append(IPAddress(QHostAddress(exitServer.ipv4Gateway()), 32));
  logger.debug() << "Allow the IPv6 gateway:" << exitServer.ipv6Gateway();
  list.append(IPAddress(QHostAddress(exitServer.ipv6Gateway()), 128));

  // Ensure that the Mullvad proxy services are always allowed.
  list.append(
      IPAddress(QHostAddress(MULLVAD_PROXY_RANGE), MULLVAD_PROXY_RANGE_LENGTH));

  // Allow access to everything not covered by an excluded address.
  QList<IPAddress> allowedIPv4 = {IPAddress("0.0.0.0/0")};
  list.append(IPAddress::excludeAddresses(allowedIPv4, excludeIPv4s));
  QList<IPAddress> allowedIPv6 = {IPAddress("::/0")};
  list.append(IPAddress::excludeAddresses(allowedIPv6, excludeIPv6s));
#endif

  return list;
}

QStringList Controller::getExcludedAddresses() {
  logger.debug() << "Computing the excluded IP addresses";

  QStringList list;

  // filtering out the captive portal endpoint
  if (Feature::get(Feature::Feature_captivePortal)->isSupported() &&
      SettingsHolder::instance()->captivePortalAlert()) {
    CaptivePortal* captivePortal = MozillaVPN::instance()->captivePortal();

    for (const QString& address : captivePortal->ipv4Addresses()) {
      logger.debug() << "Filtering out the captive portal address:" << address;
      list.append(address);
    }
    for (const QString& address : captivePortal->ipv6Addresses()) {
      logger.debug() << "Filtering out the captive portal address:" << address;
      list.append(address);
    }
  }

  return list;
}

void Controller::clearRetryCounter() {
  m_connectionRetry = 0;
  emit connectionRetryChanged();
}

void Controller::clearConnectedTime() {
  m_connectedTimeInUTC = QDateTime();
  emit timeChanged();
  m_timer.stop();
}

void Controller::resetConnectedTime() {
  m_connectedTimeInUTC = QDateTime::currentDateTimeUtc();
  emit timeChanged();
  m_timer.start(TIMER_MSEC);
}

void Controller::captivePortalGone() {
  m_portalDetected = false;
  logger.info() << "Captive-Portal Gone, next activation will not show prompt";
}

void Controller::captivePortalPresent() {
  if (m_portalDetected) {
    return;
  }
  m_portalDetected = true;
  logger.info() << "Captive-Portal Present, next activation will show prompt";
}

void Controller::serverDataChanged() {
  if (m_state == StateOff) {
    logger.debug() << "Server data changed but we are off";
    return;
  }

  TaskScheduler::deleteTasks();
  TaskScheduler::scheduleTask(
      new TaskControllerAction(TaskControllerAction::eSwitch));
}

bool Controller::switchServers() {
  if (m_state == StateOff) {
    logger.debug() << "Server data changed but we are off";
    return false;
  }

  clearConnectedTime();
  clearRetryCounter();

  logger.debug() << "Switching to a different server";

  setState(StateSwitching);
  deactivate();

  return true;
}
