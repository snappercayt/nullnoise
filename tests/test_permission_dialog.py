import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "scripts"))

from permission_dialog import button_center, permission_button_center


class PermissionDialogTests(unittest.TestCase):
    def test_selects_allowed_button_and_returns_its_center(self):
        hierarchy = """
        <hierarchy>
          <node text="Cancel" bounds="[0,10][20,30]" />
          <node text="While using the app" bounds="[30,100][230,160]" />
        </hierarchy>
        """
        self.assertEqual(permission_button_center(hierarchy), (130, 130))

    def test_matches_allowed_labels_without_case_sensitivity(self):
        hierarchy = (
            '<hierarchy><node text="ONLY THIS TIME" '
            'bounds="[11,21][32,42]" /></hierarchy>'
        )
        self.assertEqual(permission_button_center(hierarchy), (21, 31))

    def test_finds_a_requested_system_dialog_button(self):
        hierarchy = (
            '<hierarchy><node text="Got it" bounds="[835,436][1017,562]" />'
            "</hierarchy>"
        )
        self.assertEqual(button_center(hierarchy, {"got it"}), (926, 499))

    def test_ignores_unrecognized_or_malformed_bounds(self):
        hierarchy = """
        <hierarchy>
          <node text="Allow notifications" bounds="[0,0][10,10]" />
          <node text="Allow" bounds="invalid" />
        </hierarchy>
        """
        self.assertIsNone(permission_button_center(hierarchy))

    def test_returns_none_for_invalid_xml(self):
        self.assertIsNone(permission_button_center("<hierarchy>"))


if __name__ == "__main__":
    unittest.main()
