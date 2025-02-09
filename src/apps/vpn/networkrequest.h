/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NETWORKREQUEST_H
#define NETWORKREQUEST_H

#include <QHostAddress>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QTimer>

class QNetworkAccessManager;
#ifndef QT_NO_SSL
class QSslCertificate;
#endif
class Task;

class NetworkRequest final : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(NetworkRequest)

 public:
  ~NetworkRequest();

  // This object deletes itself at the end of the operation.

  static NetworkRequest* createForGetUrl(Task* parent, const QString& url,
                                         int status = 0);

  static NetworkRequest* createForGetHostAddress(Task* parent,
                                                 const QString& url,
                                                 const QHostAddress& address);

  static NetworkRequest* createForUploadData(Task* parent, const QString& url,
                                             QIODevice* uploadData);

  static NetworkRequest* createForUploadDataHostAddress(
      Task* parent, const QString& url, QIODevice* uploadData,
      const QHostAddress& address);

  static NetworkRequest* createForAuthenticationVerification(
      Task* parent, const QString& pkceCodeSuccess,
      const QString& pkceCodeVerifier);

  static NetworkRequest* createForAdjustProxy(
      Task* parent, const QString& method, const QString& path,
      const QList<QPair<QString, QString>>& headers,
      const QString& queryParameters, const QString& bodyParameters,
      const QList<QString>& unknownParameters);

  static NetworkRequest* createForDeviceCreation(Task* parent,
                                                 const QString& deviceName,
                                                 const QString& pubKey,
                                                 const QString& deviceId);

  static NetworkRequest* createForDeviceRemoval(Task* parent,
                                                const QString& pubKey);

  static NetworkRequest* createForServers(Task* parent);

  static NetworkRequest* createForAccount(Task* parent);

  static NetworkRequest* createForGetSubscriptionDetails(Task* parent);

  static NetworkRequest* createForVersions(Task* parent);

  static NetworkRequest* createForIpInfo(
      Task* parent, const QHostAddress& address = QHostAddress());

  static NetworkRequest* createForCaptivePortalDetection(
      Task* parent, const QUrl& url, const QByteArray& host);

  static NetworkRequest* createForCaptivePortalLookup(Task* parent);

  static NetworkRequest* createForHeartbeat(Task* parent);

  static NetworkRequest* createForFeedback(Task* parent,
                                           const QString& feedbackText,
                                           const QString& logs,
                                           const qint8 rating,
                                           const QString& category);

  static NetworkRequest* createForSupportTicket(
      Task* parent, const QString& email, const QString& subject,
      const QString& issueText, const QString& logs, const QString& category);

  static NetworkRequest* createForGetFeatureList(Task* parent);

  static NetworkRequest* createForFxaAccountStatus(Task* parent,
                                                   const QString& emailAddress);

  static NetworkRequest* createForFxaAccountCreation(
      Task* parent, const QString& email, const QByteArray& authpw,
      const QString& fxaClientId, const QString& fxaDeviceId,
      const QString& fxaFlowId, double fxaFlowBeginTime);

  static NetworkRequest* createForFxaLogin(
      Task* parent, const QString& email, const QByteArray& authpw,
      const QString& originalLoginEmail, const QString& unblockCode,
      const QString& fxaClientId, const QString& fxaDeviceId,
      const QString& fxaFlowId, double fxaFlowBeginTime);

  static NetworkRequest* createForFxaSendUnblockCode(
      Task* parent, const QString& emailAddress);

  static NetworkRequest* createForFxaSessionVerifyByEmailCode(
      Task* parent, const QByteArray& sessionToken, const QString& code,
      const QString& fxaClientId, const QString& fxaScope);

  static NetworkRequest* createForFxaSessionVerifyByTotpCode(
      Task* parent, const QByteArray& sessionToken, const QString& code,
      const QString& fxaClientId, const QString& fxaScope);

  static NetworkRequest* createForFxaSessionResendCode(
      Task* parent, const QByteArray& sessionToken);

  static NetworkRequest* createForFxaAuthz(Task* parent,
                                           const QByteArray& sessionToken,
                                           const QString& fxaClientId,
                                           const QString& fxaState,
                                           const QString& fxaScope,
                                           const QString& fxaAccessType);

#ifdef UNIT_TEST
  static NetworkRequest* createForFxaTotpCreation(
      Task* parent, const QByteArray& sessionToken);
#endif

  static NetworkRequest* createForFxaAccountDeletion(
      Task* parent, const QByteArray& sessionToken, const QString& emailAddress,
      const QByteArray& authpw);

  static NetworkRequest* createForFxaAttachedClients(
      Task* parent, const QByteArray& sessionToken);

  static NetworkRequest* createForFxaSessionDestroy(
      Task* parent, const QByteArray& sessionToken);

  static NetworkRequest* createForSentry(Task* parent,
                                         const QByteArray& envelope);

  static NetworkRequest* createForProducts(Task* parent);

#ifdef MZ_IOS
  static NetworkRequest* createForIOSPurchase(Task* parent,
                                              const QString& receipt);
#endif
#ifdef MZ_ANDROID
  static NetworkRequest* createForAndroidPurchase(Task* parent,
                                                  const QString& sku,
                                                  const QString& purchaseToken);
#endif
#ifdef MZ_WASM
  static NetworkRequest* createForWasmPurchase(Task* parent,
                                               const QString& productId);
#endif

  void disableTimeout();

  int statusCode() const;

  QByteArray rawHeader(const QByteArray& headerName) const;
  QUrl url() const { return m_reply ? m_reply->url() : m_request.url(); }

  void abort();
  bool isAborted() const { return m_aborted; }

  static QString apiBaseUrl();

  void processData(QNetworkReply::NetworkError error,
                   const QString& errorString, int status,
                   const QByteArray& data);

 private:
  NetworkRequest(Task* parent, int status, bool setAuthorizationHeader);

  void deleteRequest();
  void getRequest();
  void postRequest(const QByteArray& body);
  void uploadDataRequest(QIODevice* data);

  void handleReply(QNetworkReply* reply);
  void handleHeaderReceived();
  void handleRedirect(const QUrl& url);

#ifndef QT_NO_SSL
  bool checkSubjectName(const QSslCertificate& cert);
#endif

  bool isRedirect() const;

  void maybeDeleteLater();

 private slots:
  void replyFinished();
  void timeout();

#ifndef QT_NO_SSL
  void sslErrors(const QList<QSslError>& errors);
#endif

 signals:
  void requestHeaderReceived(NetworkRequest* request);
  void requestFailed(QNetworkReply::NetworkError error, const QByteArray& data);
  void requestRedirected(NetworkRequest* request, const QUrl& url);
  void requestCompleted(const QByteArray& data);
  void requestUpdated(qint64 bytesReceived, qint64 bytesTotal,
                      QNetworkReply* reply);
  void uploadProgressed(qint64 bytesReceived, qint64 bytesTotal,
                        QNetworkReply* reply);

 private:
  QNetworkRequest m_request;
  QTimer m_timer;

#ifndef QT_NO_SSL
  void enableSSLIntervention();
#endif

  QNetworkReply* m_reply = nullptr;
  int m_expectedStatusCode = 0;
#ifdef MZ_WASM
  // In wasm network request, m_reply is null. So we need to store the "status
  // code" in a variable member.
  int m_finalStatusCode = 0;
#endif

  bool m_completed = false;
  bool m_aborted = false;

#if QT_VERSION >= 0x060000
  QUrl m_redirectedUrl;
#endif
};

#endif  // NETWORKREQUEST_H
