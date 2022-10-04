/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import Foundation
import os.log

public class IOSLogger {
    static var global: IOSLogger?

    var tag: String

    init(tagged tag: String) {
        self.tag = tag
    }

    deinit {}

    func log(message: String) {
        write_msg_to_log(tag, message.trimmingCharacters(in: .newlines))
    }

    func writeLog(to targetFile: String) -> Bool {
        return true;
    }

    static func configureGlobal(tagged tag: String, withFilePath filePath: String?) {
        if IOSLogger.global != nil {
            return
        }

        IOSLogger.global = IOSLogger(tagged: tag)

        var appVersion = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "Unknown version"

        if let appBuild = Bundle.main.infoDictionary?["CFBundleVersion"] as? String {
            appVersion += " (\(appBuild))"
        }

        let goBackendVersion = WIREGUARD_GO_VERSION
        IOSLogger.global?.log(message: "App version: \(appVersion); Go backend version: \(goBackendVersion)")
    }
}

func wg_log(_ type: OSLogType, staticMessage msg: StaticString) {
    os_log(msg, log: OSLog.default, type: type)
    IOSLogger.global?.log(message: "\(msg)")
}

func wg_log(_ type: OSLogType, message msg: String) {
    os_log("%{public}s", log: OSLog.default, type: type, msg)
    IOSLogger.global?.log(message: msg)
}
