#pragma once
#include "basePage.h"
#include "yse.hpp"

class Demo3D :
	public basePage
{
public:
	Demo3D();
	virtual void ExplainDemo();

	void SelectSound1();
	void SelectSound2();
	void SelectListener();
	void MoveObjectLeft();
	void MoveObjectRight();
	void MoveObjectForward();
	void MoveObjectBack();
	void Reset();

private:
	enum direction {
		FORWARD,
		BACKWARD,
		LEFT,
		RIGHT,
	};

	void moveObject(direction d);

	YSE::sound sound1, sound2;
	int selectedObject;
};

