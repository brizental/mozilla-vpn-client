/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "event.h"
#include "glean/extern.h"

void EventMetric::record() const { glean_event_record(m_id); }