/*
  ==============================================================================

  This is an automatically generated GUI class created by the Introjucer!

  Be careful when adding custom code to these files, as only the code within
  the "//[xyz]" and "//[/xyz]" sections will be retained when the file is loaded
  and re-saved.

  Created with Introjucer version: 3.1.0

  ------------------------------------------------------------------------------

  The Introjucer is part of the JUCE library - "Jules' Utility Class Extensions"
  Copyright 2004-13 by Raw Material Software Ltd.

  ==============================================================================
*/

#ifndef __JUCE_HEADER_66400E31BB10D9B3__
#define __JUCE_HEADER_66400E31BB10D9B3__

//[Headers]     -- You can add your own extra header files here --
#include "JuceHeader.h"
#include "../../yse/yse.hpp"
#include <forward_list>

//[/Headers]



//==============================================================================
/**
                                                                    //[Comments]
    An auto-generated component, created by the Introjucer.

    Describe your class and how it works here!
                                                                    //[/Comments]
*/
class basicTab  : public Component,
                  public ButtonListener,
                  public SliderListener
{
public:
    //==============================================================================
    basicTab ();
    ~basicTab();

    //==============================================================================
    //[UserMethods]     -- You can add your own custom methods in this section.
    //[/UserMethods]

    void paint (Graphics& g);
    void resized();
    void buttonClicked (Button* buttonThatWasClicked);
    void sliderValueChanged (Slider* sliderThatWasMoved);



private:
    //[UserVariables]   -- You can add your own custom variables in this section.
    //[/UserVariables]

    //==============================================================================
    ScopedPointer<ToggleButton> onOffButton2;
    ScopedPointer<Label> labelTitle;
    ScopedPointer<Label> label;
    ScopedPointer<Slider> volumeSlider2;
    ScopedPointer<Slider> panSlider2;
    ScopedPointer<Slider> speedSlider2;
    ScopedPointer<ToggleButton> onOffButton1;
    ScopedPointer<Slider> volumeSlider1;
    ScopedPointer<Slider> panSlider1;
    ScopedPointer<Slider> speedSlider1;
    ScopedPointer<ToggleButton> onOffButton3;
    ScopedPointer<Slider> volumeSlider3;
    ScopedPointer<Slider> panSlider3;
    ScopedPointer<Slider> speedSlider3;


    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (basicTab)
};

//[EndFile] You can add extra defines here...
//[/EndFile]

#endif   // __JUCE_HEADER_66400E31BB10D9B3__
