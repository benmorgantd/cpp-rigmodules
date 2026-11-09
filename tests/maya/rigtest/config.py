"""Paths and run settings shared by every test. run_tests.py sets WORK_DIR and UPDATE_GOLDEN."""
import os

MAYA_TESTS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_DIR = os.path.dirname(os.path.dirname(MAYA_TESTS_DIR))
FIXTURES_DIR = os.path.join(MAYA_TESTS_DIR, "fixtures")
GOLDEN_DIR = os.path.join(MAYA_TESTS_DIR, "golden")

# Temporary folder for written templates. Set by the runner.
WORK_DIR = None
# When True, golden snapshot tests rewrite their file instead of comparing.
UPDATE_GOLDEN = False

# Namespaces every test uses unless it is testing namespaces.
SKELETON_NAMESPACE = "skel"
RIG_NAMESPACE = "rig"
# Asset every build uses unless it is testing assets. Its last token is the default namespace, RIG_NAMESPACE.
ASSET_ID = "tests|rig"

# Every node type the plugin registers that a build creates.
RIG_NODE_TYPES = ["assetRootModule", "rigRootModule", "layoutModule", "fkChainModule", "aimModule",
                  "rigConstraint", "rigControlNode"]
