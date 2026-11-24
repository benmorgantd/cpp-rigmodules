#pragma once

// A tiny test harness for core/ (Maya-free). One executable holds every test; each test is a function registered with
// CORE_TEST, run in registration order. A failed CHECK stops that test and the run goes on. The exit code is the number
// of failed tests, and each outcome is one line, "PASS <name>" or "FAIL <name>: <message>", which
// tests/maya/concepts/test_core_cpp.py reads.

#include <cmath>
#include <string>
#include <vector>

struct CoreTestFailure
{
	std::string message;
};

struct CoreTestCase
{
	const char* name;
	void (*run)();
};

std::vector<CoreTestCase>& coreTests();

struct CoreTestRegistrar
{
	CoreTestRegistrar(const char* name, void (*run)())
	{
		coreTests().push_back({ name, run });
	}
};

#define CORE_TEST(name) \
	static void name(); \
	static CoreTestRegistrar registrar_##name(#name, name); \
	static void name()

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			throw CoreTestFailure{ std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #condition }; \
		} \
	} while (false)

#define CHECK_CLOSE(actual, expected, tolerance) \
	do \
	{ \
		const double checkActual = (actual); \
		const double checkExpected = (expected); \
		if (!(std::fabs(checkActual - checkExpected) <= (tolerance))) \
		{ \
			throw CoreTestFailure{ std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #actual + " is " \
				+ std::to_string(checkActual) + ", expected " + std::to_string(checkExpected) }; \
		} \
	} while (false)
