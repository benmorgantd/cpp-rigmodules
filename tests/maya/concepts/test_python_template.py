"""rigmodules.template: reading a template file without building it (D30)."""
import json
import os
import unittest

import rigmodules.template
from rigtest import config, fixtures


class GetRigName(unittest.TestCase):

    def write(self, name, text):
        path = os.path.join(config.WORK_DIR, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        return path

    def test_reads_the_rig_name(self):
        self.assertEqual(rigmodules.template.get_rig_name(fixtures.template_path("layout_rig")), "fktest")

    def test_missing_rig_name(self):
        path = self.write("no_rig_name.json", json.dumps({"rigVersion": 1}))
        with self.assertRaisesRegex(ValueError, "has no rigName"):
            rigmodules.template.get_rig_name(path)

    def test_empty_rig_name(self):
        path = self.write("empty_rig_name.json", json.dumps({"rigName": ""}))
        with self.assertRaisesRegex(ValueError, "has no rigName"):
            rigmodules.template.get_rig_name(path)

    def test_not_json(self):
        path = self.write("not_json.json", "{ rigName")
        with self.assertRaisesRegex(ValueError, "is not a JSON template"):
            rigmodules.template.get_rig_name(path)
