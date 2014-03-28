/*
  ==============================================================================

    draggedComponent.h
    Created: 4 Feb 2014 6:16:45pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DRAGGEDCOMPONENT_H_INCLUDED
#define DRAGGEDCOMPONENT_H_INCLUDED

#include "../../JuceLibraryCode/JuceHeader.h"
#include "../../yse/yse.hpp"

//==============================================================================
/*
*/
class draggedComponent : public ImageButton
{
public:
    draggedComponent(const String & name = String::empty);
    ~draggedComponent();

    void paint (Graphics&);
    void resized();

    void mouseDown(const MouseEvent & e) override;
    void mouseDrag(const MouseEvent & e) override;
    void setCoordLabel(Label * ptr);
    void setDragBounds(Component * ptr);
    void setListener(bool value);
    void setSound(juce::InputStream * stream);
    void play(bool value);
private:
    ComponentBoundsConstrainer constrainer;
    ComponentDragger dragger;
    Label * coordLabel;
    Component * parent;
    Rectangle<int> bounds;
    bool listener;
    YSE::sound * sound;
    Point<float> calculateYSEPos(const Point<int> & pos);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (draggedComponent)
};


#endif  // DRAGGEDCOMPONENT_H_INCLUDED
