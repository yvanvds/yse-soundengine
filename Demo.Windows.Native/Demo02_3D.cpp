#include "stdafx.h"
#include "Demo02_3D.h"
#include <iostream>

/* 3D:

This demo is about 3D positioning. It's a bit clumsy to do in a
little console demo. Most important is the member function setPosition,
and the helper object YSE::Pos which holds xyz coordinates.

*/


Demo3D::Demo3D()
{
	// load a sound in memory and get a pointer to it
	sound1.create("..\\TestResources\\drone.ogg", nullptr, true).play();
	sound2.create("..\\TestResources\\kick.ogg", nullptr, true).play();
	selectedObject = 1;
	Reset();

	SetTitle("3D Sound Movement");

	AddAction('1', "Select Drone Sound", std::bind(&Demo3D::SelectSound1, this));
	AddAction('2', "Select Kick Sound", std::bind(&Demo3D::SelectSound2, this));
	AddAction('3', "Select Listener", std::bind(&Demo3D::SelectListener, this));
	AddAction('a', "Move Selected Left", std::bind(&Demo3D::MoveObjectLeft, this));
	AddAction('s', "Move Selected Back", std::bind(&Demo3D::MoveObjectBack, this));
	AddAction('d', "Move Selected Right", std::bind(&Demo3D::MoveObjectRight, this));
	AddAction('w', "Move Selected Forward", std::bind(&Demo3D::MoveObjectForward, this));
	AddAction('r', "Reset All Positions", std::bind(&Demo3D::Reset, this));
}

void Demo3D::ExplainDemo()
{
	std::cout << "This demonstrates how sounds and the listener can be moved in 3D." << std::endl;
	std::cout << "Initial positions (xyz) are:" << std::endl;
	std::cout << "Listener:  0 / 0 / 0" << std::endl;
	std::cout << "Sound 1 : -3 / 0 / 3" << std::endl;
	std::cout << "Sound 2 :  3 / 0 / 3" << std::endl;
	std::cout << std::endl;
}

void Demo3D::SelectSound1()
{
	selectedObject = 1;
}

void Demo3D::SelectSound2()
{
	selectedObject = 2;
}

void Demo3D::SelectListener()
{
	selectedObject = 3;
}

void Demo3D::MoveObjectLeft()
{
	moveObject(LEFT);
}

void Demo3D::MoveObjectRight()
{
	moveObject(RIGHT);
}

void Demo3D::MoveObjectForward()
{
	moveObject(FORWARD);
}

void Demo3D::MoveObjectBack()
{
	moveObject(BACKWARD);
}

void Demo3D::Reset()
{
	// YSE has a very flexible vector class built in
	YSE::Pos pos;
	pos.zero();   YSE::Listener().pos(pos);
	
	sound1.pos(YSE::Pos(-3,0,3));
	sound2.pos(YSE::Pos(3,0,3));
}

void Demo3D::moveObject(direction d) {
	if (selectedObject < 3) {
		YSE::sound * s;
		if (selectedObject == 1) s = &sound1;
		else s = &sound2;
		YSE::Pos pos = s->pos();
		switch (d) {
		case FORWARD: pos.z += 0.5f; s->pos(pos); break;
		case BACKWARD: pos.z -= 0.5f; s->pos(pos); break;
		case LEFT: pos.x -= 0.5f; s->pos(pos); break;
		case RIGHT: pos.x += 0.5f; s->pos(pos); break;
		}
	}
	else {
		// you do not have to create the listener object, it's already there
		YSE::Pos pos = YSE::Listener().pos();
		switch (d) {
		case FORWARD: pos.z += 0.5f; YSE::Listener().pos(pos); break;
		case BACKWARD: pos.z -= 0.5f; YSE::Listener().pos(pos); break;
		case LEFT: pos.x -= 0.5f; YSE::Listener().pos(pos); break;
		case RIGHT: pos.x += 0.5f; YSE::Listener().pos(pos); break;
		}
	}
}
