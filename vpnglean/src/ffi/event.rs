// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::metrics::generated as metric_maps;
use glean::traits::EventRecordingError;

#[no_mangle]
pub extern "C" fn glean_event_record(id: u32) {
    // TODO: Extra keys stuff

    match metric_maps::record_event_by_id(id /* , extra*/) {
        Ok(()) => {}
        Err(EventRecordingError::InvalidId) => panic!("No event for id {}", id),
        Err(EventRecordingError::InvalidExtraKey) => {
            // TODO: Record an error. bug 1704504.
        }
    }
}

#[no_mangle]
pub extern "C" fn glean_event_test_get_error(id: u32) -> bool {
    let err = metric_maps::event_test_get_error(id);
    err.is_some()
}
