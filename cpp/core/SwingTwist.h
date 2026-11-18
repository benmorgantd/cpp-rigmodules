#pragma once

// The swing-twist split about X, with no Maya types (design/ribbon-twist-follow.md). A rotation is a twist about X and a swing
// that turns X to where the rotation takes it: row-vector matrices read `rotation = twist * swing`, so the twist is applied first
// and the swing about axes perpendicular to X after it. Used by the Ribbon (its ends' twist) and the Swing constraint.

namespace SwingTwist
{
	// The twist of a rotation about X, in (-pi, pi]. The rotation is given by the rows of its matrix (Maya's convention: row
	// vectors, rows 0 to 2 are the X, Y and Z axes). Scale and shear are ignored. A swing of exactly half a turn has no twist
	// (the answer is arbitrary near it and 0 at it), and the result is stateless.
	double twistAboutX(const double (&rows)[3][3]);

	// The rotation about X by `angle`, as the rows of a row-vector matrix: twistAboutX of it gives `angle` back.
	void rotationAboutX(double angle, double (&outRows)[3][3]);
}
