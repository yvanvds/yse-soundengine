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

#ifndef __JUCE_HEADER_65EA9514504C07E__
#define __JUCE_HEADER_65EA9514504C07E__

//[Headers]     -- You can add your own extra header files here --
#include "JuceHeader.h"
#include "parts/draggedComponent.h"
//[/Headers]



//==============================================================================
/**
                                                                    //[Comments]
    An auto-generated component, created by the Introjucer.

    Describe your class and how it works here!
                                                                    //[/Comments]
*/
class basic3D  : public Component,
                 public DragAndDropContainer,
                 public ButtonListener,
                 public SliderListener
{
public:
    //==============================================================================
    basic3D ();
    ~basic3D();

    //==============================================================================
    //[UserMethods]     -- You can add your own custom methods in this section.
    //[/UserMethods]

    void paint (Graphics& g);
    void resized();
    void buttonClicked (Button* buttonThatWasClicked);
    void sliderValueChanged (Slider* sliderThatWasMoved);

    // Binary resources:
    static const char* walk_png;
    static const int walk_pngSize;
    static const char* sound_png;
    static const int sound_pngSize;


private:
    //[UserVariables]   -- You can add your own custom variables in this section.
    //[/UserVariables]

    //==============================================================================
    ScopedPointer<Label> labelTitle;
    ScopedPointer<Label> label;
    ScopedPointer<ToggleButton> playButton;
    ScopedPointer<Slider> volumeSlider;
    ScopedPointer<Label> label2;
    ScopedPointer<draggedComponent> listenerButton;
    ScopedPointer<Label> coordLabel;
    ScopedPointer<draggedComponent> soundButton1;
    ScopedPointer<draggedComponent> soundButton2;


    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (basic3D)
};

//[EndFile] You can add extra defines here...
//[/EndFile]

#endif   // __JUCE_HEADER_65EA9514504C07E__
