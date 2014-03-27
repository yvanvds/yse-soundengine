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

#ifndef __JUCE_HEADER_478A4D14C850C018__
#define __JUCE_HEADER_478A4D14C850C018__

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
class cpuLoad  : public Component,
                 public ButtonListener,
                 public SliderListener
{
public:
    //==============================================================================
    cpuLoad ();
    ~cpuLoad();

    //==============================================================================
    //[UserMethods]     -- You can add your own custom methods in this section.
    void updateCounter();
    //[/UserMethods]

    void paint (Graphics& g);
    void resized();
    void buttonClicked (Button* buttonThatWasClicked);
    void sliderValueChanged (Slider* sliderThatWasMoved);



private:
    //[UserVariables]   -- You can add your own custom variables in this section.
    int soundCounter;
    //[/UserVariables]

    //==============================================================================
    ScopedPointer<Label> labelTitle;
    ScopedPointer<Label> label;
    ScopedPointer<Label> label2;
    ScopedPointer<Label> label3;
    ScopedPointer<Label> label4;
    ScopedPointer<Label> label5;
    ScopedPointer<TextButton> buttonAdd;
    ScopedPointer<TextButton> buttonRemove;
    ScopedPointer<Slider> sliderVirtual;
    ScopedPointer<Label> label6;
    ScopedPointer<Label> label7;
    ScopedPointer<Label> labelLoad;
    ScopedPointer<Label> labelAllSounds;
    ScopedPointer<Label> labelPlayingSounds;
    ScopedPointer<Label> labelVirtualSounds;


    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (cpuLoad)
};

//[EndFile] You can add extra defines here...
//[/EndFile]

#endif   // __JUCE_HEADER_478A4D14C850C018__
