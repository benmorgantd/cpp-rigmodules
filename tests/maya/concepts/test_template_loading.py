"""Reading the template file itself, before any key or content checks."""
import os

from rigtest import config
from rigtest.case import RigTestCase


class FileProblems(RigTestCase):

    def write_raw(self, name, text):
        path = os.path.join(config.WORK_DIR, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        return path.replace("\\", "/")

    def build_path(self, path):
        from maya import cmds
        joints = self.make_skeleton("layout_rig")
        return lambda: cmds.createRigFromTemplate(*joints, template=path, asset=config.ASSET_ID)

    def test_malformed_json_reports_the_file(self):
        path = self.write_raw("malformed.json", '{"assetId": "x", ')
        self.assertMayaError(self.build_path(path), "malformed.json")
        self.assertNothingBuilt()

    def test_empty_file_fails(self):
        path = self.write_raw("empty.json", "")
        self.assertMayaError(self.build_path(path))
        self.assertNothingBuilt()

    def test_root_must_be_an_object(self):
        path = self.write_raw("list.json", "[]")
        self.assertMayaError(self.build_path(path))
        self.assertNothingBuilt()

    def test_wrong_value_type_fails(self):
        path = self.write_raw("wrongtype.json", '{"rigVersion": "one"}')
        self.assertMayaError(self.build_path(path))
        self.assertNothingBuilt()
