/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef EVENT_METRIC_H
#define EVENT_METRIC_H

class EventMetric final {
 public:
  constexpr explicit EventMetric(int aId) : m_id(aId) {}

  void record() const;

 private:
  int m_id;
};

#endif  // EVENT_METRIC_H
