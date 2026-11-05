"""Build smoke tier: every fixture template builds without an error. Behaviour tiers only run
after this passes, so a broken build shows up here once instead of as a failure in every later test."""
from rigtest.case import RigTestCase
from rigtest.cases import add_smoke_cases, add_validation_cases


class FixtureBuilds(RigTestCase):
    """One build per template and skeleton pair named by a golden file."""


add_smoke_cases(FixtureBuilds)


class ValidTemplateVariations(RigTestCase):
    """Validation cases with "expectErrors": [] must build."""


add_validation_cases(ValidTemplateVariations, expect_build=True)
