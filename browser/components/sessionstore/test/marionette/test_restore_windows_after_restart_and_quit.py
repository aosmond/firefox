# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# add this directory to the path
import os
import sys

sys.path.append(os.path.dirname(__file__))

from session_store_test_case import SessionStoreTestCase


def inline(title):
    return f"data:text/html;charset=utf-8,<html><head><title>{title}</title></head><body></body></html>"


class TestSessionStoreEnabledAllWindows(SessionStoreTestCase):
    def setUp(self, include_private=True):
        """Setup for the test, enabling session restore.

        :param include_private: Whether to open private windows.
        """
        super(TestSessionStoreEnabledAllWindows, self).setUp(
            include_private=include_private, startup_page=3
        )

    def test_with_variety(self):
        """Test opening and restoring both standard and private windows.

        Opens a set of windows, both standard and private, with
        some number of tabs in them. Once the tabs have loaded, restarts
        the browser, and then ensures that the standard tabs have been
        restored, and that the private ones have not.
        """
        self.wait_for_windows(
            self.all_windows, "Not all requested windows have been opened"
        )

        self.marionette.quit()
        self.marionette.start_session()
        self.marionette.set_context("chrome")

        self.wait_for_windows(
            self.test_windows, "Non private browsing windows should have been restored"
        )


class TestSessionStoreEnabledNoPrivateWindows(TestSessionStoreEnabledAllWindows):
    def setUp(self):
        super(TestSessionStoreEnabledNoPrivateWindows, self).setUp(
            include_private=False
        )


class TestSessionStoreDisabled(SessionStoreTestCase):
    def test_no_restore_with_quit(self):
        self.wait_for_windows(
            self.all_windows, "Not all requested windows have been opened"
        )

        self.marionette.quit()
        self.marionette.start_session()
        self.marionette.set_context("chrome")

        self.assertEqual(
            len(self.marionette.chrome_window_handles),
            1,
            msg="Windows from last session shouldn`t have been restored.",
        )
        self.assertEqual(
            len(self.marionette.window_handles),
            1,
            msg="Tabs from last session shouldn`t have been restored.",
        )

    def test_restore_with_restart(self):
        self.wait_for_windows(
            self.all_windows, "Not all requested windows have been opened"
        )

        self.marionette.restart(in_app=True)

        self.wait_for_windows(
            self.test_windows, "Non private browsing windows should have been restored"
        )
