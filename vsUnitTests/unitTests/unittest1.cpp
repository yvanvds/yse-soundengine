#include "stdafx.h"
#include "CppUnitTest.h"
#include "yse.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace unitTests
{		
	TEST_CLASS(Misc_Clamp)
	{
	public:
		
		TEST_METHOD(ClampIntegerTest)
		{
      Int i = 10;
      YSE::Clamp(i, 1, 5);
      Assert::AreEqual(i, 5);
      i = 0;
      YSE::Clamp(i, 1, 5);
      Assert::AreEqual(i, 1);
      i = 3;
      YSE::Clamp(i, 1, 5);
      Assert::AreEqual(i, 3);
		}

    TEST_METHOD(ClampFloatTest) {
      Flt i = 4.6f;
      YSE::Clamp(i, 3.5f, 4.2f);
      Assert::AreEqual(i, 4.2f);
      i = 1.345f;
      YSE::Clamp(i, 3.5f, 4.2f);
      Assert::AreEqual(i, 3.5f);
      i = 4.0567f;
      YSE::Clamp(i, 3.5f, 4.2f);
      Assert::AreEqual(i, 4.0567f);
    }

	};
}