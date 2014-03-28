/*
  ==============================================================================

    draggedComponent.cpp
    Created: 4 Feb 2014 6:16:45pm
    Author:  yvan

  ==============================================================================
*/

#include "../../JuceLibraryCode/JuceHeader.h"
#include "draggedComponent.h"
#include "yseTimerThread.h"
#include "../YSEObjects.h"

//==============================================================================
draggedComponent::draggedComponent(const String & name) : listener(false)
{
    // In your constructor, you should add any child components, and
    // initialise any special settings that your component needs.
  sound = Sound().basic3DTab.add(new YSE::sound);
}

draggedComponent::~draggedComponent()
{
}

void draggedComponent::paint (Graphics& g)
{
    /* This demo code just fills the component's background and
       draws some placeholder text to get you started.

       You should replace everything in this method with your own
       drawing code..
    */

    ImageButton::paint(g);
}

void draggedComponent::resized()
{
    // This method is where you should set the bounds of any child
    // components that your component contains..
    constrainer.setMinimumOnscreenAmounts(getHeight(), getWidth(), getHeight(), getWidth());
    bounds.setBounds(parent->proportionOfWidth(0.0525f),
      parent->proportionOfHeight(0.3469f),
      parent->proportionOfWidth(0.8931f),
      parent->proportionOfHeight(0.6054f));
}

void draggedComponent::mouseDown(const MouseEvent & e) {
  dragger.startDraggingComponent(this, e);
}

void draggedComponent::mouseDrag(const MouseEvent & e) {
  MouseEvent relative = e.getEventRelativeTo(parent);
  Point<int> p;
  p = relative.getPosition();
  if (bounds.contains(p)) {
    Point<float> ysePos = calculateYSEPos(p);
    coordLabel->setText(String::empty 
      + "x: " + String(ysePos.x) 
      + " z: " + String(ysePos.y)
      , NotificationType::sendNotification);
    dragger.dragComponent(this, e, &constrainer);
    if (listener) {
      YSE::Listener().setPosition(YSE::Vec(ysePos.x, 0, ysePos.y));
    }
    else {
      sound->setPosition(YSE::Vec(ysePos.x, 0, ysePos.y));
    } 
  } 
}

void draggedComponent::setCoordLabel(Label * ptr) {
  coordLabel = ptr;
}

void draggedComponent::setDragBounds(Component * ptr) {
  parent = ptr;
  bounds.setBounds(parent->proportionOfWidth(0.0525f), 
    parent->proportionOfHeight(0.3469f), 
    parent->proportionOfWidth(0.8931f), 
    parent->proportionOfHeight(0.6054f));
}

Point<float> draggedComponent::calculateYSEPos(const Point<int> & pos) {
  Point<float> result;
  Point<float> center;
  Point<float> size;
  result.x = static_cast<float>(pos.x);
  result.y = static_cast<float>(pos.y);
  result.x -= bounds.getPosition().x;
  result.y -= bounds.getPosition().y;
  center.x = static_cast<float>(bounds.getCentreX() - bounds.getPosition().x);
  center.y = static_cast<float>(bounds.getCentreY() - bounds.getPosition().y);
  result -= center;
  size.x = static_cast<float>(bounds.getWidth());
  size.y = static_cast<float>(bounds.getHeight());
  result /= size;
  result *= 20;
  return result;
}

void draggedComponent::setListener(bool value) {
  listener = value;
  Point<float> ysePos = calculateYSEPos(getPosition());
  YSE::Listener().setPosition(YSE::Vec(ysePos.x, 0, ysePos.y));
}

void draggedComponent::setSound(juce::InputStream * stream) {
  sound->create(stream, &YSE::ChannelMaster(), true);
  Point<float> ysePos = calculateYSEPos(getPosition());
  sound->setPosition(YSE::Vec(ysePos.x, 0, ysePos.y));
}

void draggedComponent::play(bool value) {
  if (value) {
    sound->play();
  }
  else {
    sound->pause();
  }
}