/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef LOCALIZER_H
#define LOCALIZER_H

#include <QAbstractListModel>
#include <QLocale>
#include <QTranslator>

class Collator;
class SettingsHolder;

class Localizer final : public QAbstractListModel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(Localizer)

  Q_PROPERTY(bool hasLanguages READ hasLanguages CONSTANT)
  Q_PROPERTY(QLocale locale READ locale NOTIFY localeChanged)
  Q_PROPERTY(bool isRightToLeft READ isRightToLeft NOTIFY localeChanged)

  struct Language {
    QString m_code;
    QString m_languageCode;
    QString m_countryCode;
    QString m_name;
    QString m_localizedName;
    bool m_rtl;
  };

 public:
  enum ServerCountryRoles {
    LanguageRole = Qt::UserRole + 1,
    LocalizedLanguageRole,
    CodeRole,
    RTLRole,
  };

  static Localizer* instance();

  Localizer();
  ~Localizer();

  void initialize();

  bool hasLanguages() const { return !m_languages.isEmpty(); }

  // This returns the language code from the settings, and, if it's null, it
  // returns the system language code.
  QString languageCodeOrSystem() const;

  QStringList languages() const;

  Q_INVOKABLE QString localizeCurrency(double value,
                                       const QString& currencyIso4217);

  // Returns the major part of the string in case the language code is in the
  // format 'en-FO' or 'en_FO'
  static QString majorLanguageCode(const QString& code);

  QLocale locale() const { return m_locale; }

  bool isRightToLeft() const;

  static QList<QPair<QString, QString>> parseBCP47Languages(
      const QStringList& languages);
  static QList<QPair<QString, QString>> parseIOSLanguages(
      const QStringList& languages);

  // QAbstractListModel methods

  QHash<int, QByteArray> roleNames() const override;

  int rowCount(const QModelIndex&) const override;

  QVariant data(const QModelIndex& index, int role) const override;

 signals:
  void localeChanged();

 private:
  static QString languageName(const QLocale& locale, const QString& code);
  static QString localizedLanguageName(const QLocale& locale,
                                       const QString& code);
  static bool languageSort(const Language& a, const Language& b,
                           Collator* collator);

  QString systemLanguageCode() const;

  void loadLanguagesFromI18n();
  bool loadLanguage(const QString& code);
  QString findLanguageCode(const QString& languageCode,
                           const QString& countryCode) const;

  // This method is not used. It exists just to add the installer strings into
  // the QT language files.
  static void macOSInstallerStrings();

  static QString retrieveCurrencySymbolFallback(const QString& currencyIso4217,
                                                const QLocale& currentLocale);

  void settingsChanged();

 private:
  QTranslator m_translator;

  QString m_code;

  QLocale m_locale;

  QList<Language> m_languages;
};

#endif  // LOCALIZER_H
