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
#include "../../YSE/yse.hpp"
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
    class cpuTimer : public Timer {
    public:
      int soundsToAdd;
      int soundsToDelete;
      cpuTimer() : soundsToAdd(0), soundsToDelete(0) {}

      void setLabels(Label * cpuLoad, Label * allSounds) {
        label = cpuLoad;
        labelAllSounds = allSounds;
        soundCounter = 0;
      }

      void timerCallback() {
        if (soundsToAdd > 0) {
          Random & rand = Random::getSystemRandom();
          sounds.emplace_front();
          //switch (rand.nextInt(4)) {
            //case 0: sounds.front().create("g.ogg", &YSE::ChannelAmbient(), true, 0.1f); break;
            //case 1: sounds.front().create("g.ogg", &YSE::ChannelFX(), true, 0.1f); break;
            //case 2: 
          sounds.front().create("g.ogg", &YSE::ChannelMusic(), true, 0.5f); //break;
            //case 3: sounds.front().create("g.ogg", &YSE::ChannelVoice(), true, 0.1f); break;
          //}
          sounds.front().setPosition(YSE::Vec(rand.nextFloat() - 0.5f * 20, rand.nextFloat() - 0.5f * 20, rand.nextFloat() - 0.5f * 20));
          sounds.front().setSpeed(rand.nextFloat() + 0.5f);
          sounds.front().play();
          soundCounter++;
          soundsToAdd--;
        }
        if (soundsToDelete > 0) {
          sounds.pop_front();
          soundCounter--;
          soundsToDelete--;
        }
        labelAllSounds->setText(String(soundCounter), NotificationType::sendNotification);
        label->setText(String(YSE::System().cpuLoad()), NotificationType::sendNotification);
      }
    private:
      Label * label;
      Label * labelAllSounds;
      int soundCounter;
      std::forward_list<YSE::sound> sounds;

    };
    //[/UserMethods]

    void paint (Graphics& g);
    void resized();
    void buttonClicked (Button* buttonThatWasClicked);
    void sliderValueChanged (Slider* sliderThatWasMoved);

    

private:
    //[UserVariables]   -- You can add your own custom variables in this section.
    cpuTimer timer;
    std::forward_list<YSE::sound> sounds;
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
