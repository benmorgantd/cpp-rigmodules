#include "AnimationSidecar.h"
#include "FormatUpdate.h"

const UpdateTable& animationSidecarUpdateTable()
{
	// Add a step here, and nowhere else, when the sidecar file changes shape or meaning.
	static const UpdateTable table{ 1, {} };
	return table;
}

void deserializeAnimationSidecar(const std::string&, AnimationSidecar&, ValidationReport& report)
{
	report.addError("animation sidecar", "reading animation sidecar files is not built yet (D41 skeleton)");
}
