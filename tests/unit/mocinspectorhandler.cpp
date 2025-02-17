/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "inspector/inspectorwebsocketconnection.h"

InspectorHandler::InspectorHandler(QObject*) {}

InspectorHandler::~InspectorHandler() = default;

QString InspectorHandler::appVersionForUpdate() { return "42"; }

bool InspectorHandler::forceRTL() { return false; }
