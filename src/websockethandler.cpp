/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "websockethandler.h"
#include "constants.h"
#include "leakdetector.h"
#include "logger.h"
#include "mozillavpn.h"
#include "networkrequest.h"
#include "settingsholder.h"
#include "urlopener.h"

namespace {
Logger logger(LOG_MAIN, "WebSocketHandler");
}  // namespace

WebSocketHandler::WebSocketHandler() {
  MVPN_COUNT_CTOR(WebSocketHandler)

  connect(&m_webSocket, &QWebSocket::connected, this,
          &WebSocketHandler::onConnected);
  connect(&m_webSocket, &QWebSocket::disconnected, this,
          &WebSocketHandler::onClose);
  connect(&m_webSocket,
          QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
          &WebSocketHandler::onError);
  connect(&m_webSocket, &QWebSocket::pong, this, &WebSocketHandler::onPong);

  connect(&m_pingTimer, &QTimer::timeout, this,
          &WebSocketHandler::onPingTimeout);
}

// static
QString WebSocketHandler::webSocketServerUrl() {
  if (WebSocketHandler::m_customWebSocketServerUrl.count() > 0) {
    return WebSocketHandler::m_customWebSocketServerUrl;
  }

  QString httpServerUrl;
  if (Constants::inProduction()) {
    httpServerUrl = Constants::API_PRODUCTION_URL;
  } else {
    httpServerUrl = Constants::getStagingServerAddress();
  }

  return httpServerUrl.toLower().replace("http", "ws");
}

// static
bool WebSocketHandler::isUserAuthenticated() {
  MozillaVPN* vpn = MozillaVPN::instance();
  Q_ASSERT(vpn);

  return vpn->userState() == MozillaVPN::UserAuthenticated;
}

// static
QString WebSocketHandler::m_customWebSocketServerUrl = "";

// static
void WebSocketHandler::testOverrideWebSocketServerUrl(QString url) {
  WebSocketHandler::m_customWebSocketServerUrl = url;
}

void WebSocketHandler::testOverridePingInterval(int newInterval) {
  m_pingInterval = newInterval;
}

void WebSocketHandler::testOverrideRetryInterval(int newInterval) {
  m_retryInterval = newInterval;
}

void WebSocketHandler::initialize() {
  logger.debug() << "Initialize";

  MozillaVPN* vpn = MozillaVPN::instance();
  Q_ASSERT(vpn);

  connect(vpn, &MozillaVPN::userStateChanged, this,
          &WebSocketHandler::onUserStateChanged);
}

/**
 * @brief Acknowledges the user state has changed.
 *
 * When the user has authenticated, a WebSocket connection is openened.
 * Otherwise it's closed.
 *
 * The Guardian WebSocket does not accept unauthenticated users. Attempting to
 * connect while unauthenticated would result is an infinite retry loop.
 */
void WebSocketHandler::onUserStateChanged() {
  logger.debug() << "User state change detected:"
                 << MozillaVPN::instance()->userState();

  if (WebSocketHandler::isUserAuthenticated()) {
    open();
  } else {
    close();
  }
}

/**
 * @brief Opens the websocket connection.
 *
 * No-op in case the connection is already open.
 */
void WebSocketHandler::open() {
  if (m_webSocket.state() != QAbstractSocket::UnconnectedState &&
      m_webSocket.state() != QAbstractSocket::ClosingState) {
    logger.debug()
        << "Attempted to open a WebSocket connection, but it's already open.";
    return;
  }

  logger.debug() << "Attempting to open WebSocket connection."
                 << WebSocketHandler::webSocketServerUrl();

  QNetworkRequest request;
  request.setRawHeader("Authorization",
                       SettingsHolder::instance()->token().toLocal8Bit());
  request.setUrl(QUrl(WebSocketHandler::webSocketServerUrl()));
  m_webSocket.open(request);
}

/**
 * @brief Ackowledges the WebSocket has been succesfully connected.
 */
void WebSocketHandler::onConnected() {
  logger.debug() << "WebSocket connected";

  connect(&m_webSocket, &QWebSocket::textMessageReceived, this,
          &WebSocketHandler::onMessageReceived);

  sendPing();
}

/**
 * @brief Closes the websocket connection.
 *
 * No-op in case the connection is already closed.
 */
void WebSocketHandler::close() {
  if (m_webSocket.state() == QAbstractSocket::UnconnectedState ||
      m_webSocket.state() == QAbstractSocket::ClosingState) {
    logger.debug()
        << "Attempted to close a WebSocket, but it's already closed.";
    return;
  }

  logger.debug() << "Closing WebSocket";
  m_webSocket.close();
}

/**
 * @brief Ackowledges the WebSocket has been closed.
 *
 * May trigger a reconnection attempt depending on whether the user is
 * authenticated.
 */
void WebSocketHandler::onClose() {
  logger.debug() << "WebSocket closed";

  if (WebSocketHandler::isUserAuthenticated()) {
    logger.debug()
        << "User is authenticated. Attempting to reopen WebSocket in:"
        << m_retryInterval;

    QTimer::singleShot(m_retryInterval, this, &WebSocketHandler::open);
  } else {
    logger.debug()
        << "User is not authenticated. Will not attempt to reopen WebSocket.";
  }
}

/**
 * @brief Sends a ping to the WebSocket server.
 *
 * WebSocket connections may silently be broken. Periodically pinging the server
 * keeps track of the connection. Everytime a new ping is sent, a timer is
 * started. If a response is received before the timer is up, the connection is
 * alive and a new ping is scheduled. Otherwise the WebSocket is closed a a
 * reconnection attempt is scheduled.
 */
void WebSocketHandler::sendPing() {
  m_webSocket.ping();

  m_pingTimer.setSingleShot(true);
  m_pingTimer.start(m_pingInterval);
}

/**
 * @brief Acknowledges a pong response from the WebSocket server. Schedules the
 * next ping.
 *
 * @param elapsedTime How long it took to get the response since a ping was sent
 * out.
 */
void WebSocketHandler::onPong(quint64 elapsedTime) {
  qDebug() << "PONG PONG PONG";

  logger.debug() << "WebSocket pong" << elapsedTime;

  m_pingTimer.stop();
  QTimer::singleShot(m_pingInterval, this, &WebSocketHandler::sendPing);
}

/**
 * @brief Handles the timeout of the ping timer.
 *
 * Reaching this timeout means connection with the server was silently broken
 * e.g. due to the network being disconnected. The connection will be explicitly
 * closed and a reconnection attempt scheduled.
 */
void WebSocketHandler::onPingTimeout() {
  logger.debug() << "Timed out waiting for ping response";
  close();
}

/**
 * @brief Ackowledges there was a WebSocket error.
 *
 * Closes the websocket, is the user is authenticated a new connection attempt
 * will be triggered after WEBSOCKET_RETRY_INTERVAL.
 */
void WebSocketHandler::onError(QAbstractSocket::SocketError error) {
  logger.debug() << "WebSocket error:" << error;
  close();
}

/**
 * @brief Acknowledges a message was received from the WebSocket server.
 *
 * Each message will trigger a different task on the VPN client.
 */
void WebSocketHandler::onMessageReceived(const QString& message) {
  logger.debug() << "Message received:" << message;

  // TODO: DO SOMETHING WITH THE MESSAGE.
}
