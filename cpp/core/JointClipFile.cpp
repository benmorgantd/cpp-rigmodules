#include "JointClipFile.h"
#include "FormatUpdate.h"

const UpdateTable& jointClipUpdateTable()
{
	// Add a step here, and nowhere else, when the JointClip file changes shape or meaning.
	static const UpdateTable table{ 1, {} };
	return table;
}

void deserializeJointClip(const std::string&, JointClip&, ValidationReport& report)
{
	report.addError("JointClip file", "reading JointClip files is not built yet (D41 skeleton)");
}
