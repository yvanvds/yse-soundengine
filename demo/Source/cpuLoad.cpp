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

//[Headers] You can add your own extra header files here...
#include "YSEObjects.h"
#include "parts/yseTimerThread.h"
//[/Headers]

#include "cpuLoad.h"


//[MiscUserDefs] You can add your own user definitions and misc code here...
//[/MiscUserDefs]

//==============================================================================
cpuLoad::cpuLoad ()
{
    addAndMakeVisible (labelTitle = new Label ("labelTitle",
                                               TRANS("CPU load")));
    labelTitle->setFont (Font (40.00f, Font::bold));
    labelTitle->setJustificationType (Justification::centred);
    labelTitle->setEditable (false, false, false);
    labelTitle->setColour (Label::backgroundColourId, Colours::cornflowerblue);
    labelTitle->setColour (TextEditor::textColourId, Colours::black);
    labelTitle->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (label = new Label ("new label",
                                          TRANS("This example gives you an idea of YSE\'s processing speed. Sounds are placed at a random position.")));
    label->setFont (Font (15.00f, Font::plain));
    label->setJustificationType (Justification::centred);
    label->setEditable (false, false, false);
    label->setColour (TextEditor::textColourId, Colours::black);
    label->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (label2 = new Label ("new label",
                                           TRANS("Current load of audio thread: ")));
    label2->setFont (Font (15.00f, Font::bold));
    label2->setJustificationType (Justification::centredLeft);
    label2->setEditable (false, false, false);
    label2->setColour (Label::textColourId, Colours::white);
    label2->setColour (TextEditor::textColourId, Colours::black);
    label2->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (label3 = new Label ("new label",
                                           TRANS("Sounds in memory:")));
    label3->setFont (Font (15.00f, Font::bold));
    label3->setJustificationType (Justification::centredLeft);
    label3->setEditable (false, false, false);
    label3->setColour (Label::textColourId, Colours::white);
    label3->setColour (TextEditor::textColourId, Colours::black);
    label3->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (label4 = new Label ("new label",
                                           TRANS("Non virtual sounds:")));
    label4->setFont (Font (15.00f, Font::bold));
    label4->setJustificationType (Justification::centredLeft);
    label4->setEditable (false, false, false);
    label4->setColour (Label::textColourId, Colours::white);
    label4->setColour (TextEditor::textColourId, Colours::black);
    label4->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (label5 = new Label ("new label",
                                           TRANS("Virtual sounds:")));
    label5->setFont (Font (15.00f, Font::bold));
    label5->setJustificationType (Justification::centredLeft);
    label5->setEditable (false, false, false);
    label5->setColour (Label::textColourId, Colours::white);
    label5->setColour (TextEditor::textColourId, Colours::black);
    label5->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (buttonAdd = new TextButton ("new button"));
    buttonAdd->setButtonText (TRANS("add 10 sounds"));
    buttonAdd->addListener (this);

    addAndMakeVisible (buttonRemove = new TextButton ("new button"));
    buttonRemove->setButtonText (TRANS("remove 10 sounds"));
    buttonRemove->addListener (this);

    addAndMakeVisible (sliderVirtual = new Slider ("new slider"));
    sliderVirtual->setRange (50, 2000, 0);
    sliderVirtual->setSliderStyle (Slider::LinearHorizontal);
    sliderVirtual->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
    sliderVirtual->addListener (this);

    addAndMakeVisible (label6 = new Label ("new label",
                                           TRANS("Adjust non virtual sounds")));
    label6->setFont (Font (15.00f, Font::bold));
    label6->setJustificationType (Justification::centredLeft);
    label6->setEditable (false, false, false);
    label6->setColour (Label::textColourId, Colours::black);
    label6->setColour (TextEditor::textColourId, Colours::black);
    label6->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (label7 = new Label ("new label",
                                           TRANS("By default, YSE will play only 50 sounds at a time. Others are virtualized. This means that the engine will (at each update) evaluate which sounds are the least audible. Those sounds will be virtual until they become more important or until other sounds dissapear. Virtual sounds still accept changes and have their volume and velocity updated, but they will not be added to the main mix.")));
    label7->setFont (Font (15.00f, Font::plain));
    label7->setJustificationType (Justification::topLeft);
    label7->setEditable (false, false, false);
    label7->setColour (TextEditor::textColourId, Colours::black);
    label7->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (labelLoad = new Label ("new label",
                                              TRANS("0")));
    labelLoad->setFont (Font (15.00f, Font::bold));
    labelLoad->setJustificationType (Justification::centredRight);
    labelLoad->setEditable (false, false, false);
    labelLoad->setColour (Label::textColourId, Colours::white);
    labelLoad->setColour (TextEditor::textColourId, Colours::black);
    labelLoad->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (labelAllSounds = new Label ("new label",
                                                   TRANS("0")));
    labelAllSounds->setFont (Font (15.00f, Font::bold));
    labelAllSounds->setJustificationType (Justification::centredRight);
    labelAllSounds->setEditable (false, false, false);
    labelAllSounds->setColour (Label::textColourId, Colours::white);
    labelAllSounds->setColour (TextEditor::textColourId, Colours::black);
    labelAllSounds->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (labelPlayingSounds = new Label ("new label",
                                                       TRANS("0")));
    labelPlayingSounds->setFont (Font (15.00f, Font::bold));
    labelPlayingSounds->setJustificationType (Justification::centredRight);
    labelPlayingSounds->setEditable (false, false, false);
    labelPlayingSounds->setColour (Label::textColourId, Colours::white);
    labelPlayingSounds->setColour (TextEditor::textColourId, Colours::black);
    labelPlayingSounds->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (labelVirtualSounds = new Label ("new label",
                                                       TRANS("0")));
    labelVirtualSounds->setFont (Font (15.00f, Font::bold));
    labelVirtualSounds->setJustificationType (Justification::centredRight);
    labelVirtualSounds->setEditable (false, false, false);
    labelVirtualSounds->setColour (Label::textColourId, Colours::white);
    labelVirtualSounds->setColour (TextEditor::textColourId, Colours::black);
    labelVirtualSounds->setColour (TextEditor::backgroundColourId, Colour (0x00000000));


    //[UserPreSize]
    //[/UserPreSize]

    setSize (600, 400);


    //[Constructor] You can add your own custom stuff here..
    soundCounter = 0;
    sliderVirtual->setValue(YSE::System().maxSounds(), NotificationType::sendNotification);
    YseTimer().cpuLoad = labelLoad.get();
    //[/Constructor]
}

cpuLoad::~cpuLoad()
{
    //[Destructor_pre]. You can add your own custom destruction code here..
  YseTimer().cpuLoad = NULL;
    //[/Destructor_pre]

    labelTitle = nullptr;
    label = nullptr;
    label2 = nullptr;
    label3 = nullptr;
    label4 = nullptr;
    label5 = nullptr;
    buttonAdd = nullptr;
    buttonRemove = nullptr;
    sliderVirtual = nullptr;
    label6 = nullptr;
    label7 = nullptr;
    labelLoad = nullptr;
    labelAllSounds = nullptr;
    labelPlayingSounds = nullptr;
    labelVirtualSounds = nullptr;


    //[Destructor]. You can add your own custom destruction code here..
    //[/Destructor]
}

//==============================================================================
void cpuLoad::paint (Graphics& g)
{
    //[UserPrePaint] Add your own custom painting code here..
    //[/UserPrePaint]

    g.fillAll (Colours::black);

    g.setColour (Colours::grey);
    g.fillRect (proportionOfWidth (0.0225f), proportionOfHeight (0.0204f), proportionOfWidth (0.9531f), proportionOfHeight (0.2245f));

    g.setColour (Colour (0xffcccccc));
    g.fillRect (proportionOfWidth (0.0225f), proportionOfHeight (0.2721f), proportionOfWidth (0.9531f), proportionOfHeight (0.7075f));

    g.setColour (Colour (0xff404040));
    g.fillRoundedRectangle (static_cast<float> (proportionOfWidth (0.0507f)), static_cast<float> (proportionOfHeight (0.2925f)), static_cast<float> (proportionOfWidth (0.9043f)), static_cast<float> (proportionOfHeight (0.1786f)), 10.000f);

    g.setColour (Colour (0xff2a68a5));
    g.drawRoundedRectangle (static_cast<float> (proportionOfWidth (0.0507f)), static_cast<float> (proportionOfHeight (0.2925f)), static_cast<float> (proportionOfWidth (0.9043f)), static_cast<float> (proportionOfHeight (0.1786f)), 10.000f, 5.000f);

    //[UserPaint] Add your own custom painting code here..
    //[/UserPaint]
}

void cpuLoad::resized()
{
    labelTitle->setBounds (proportionOfWidth (0.1463f), proportionOfHeight (0.0408f), proportionOfWidth (0.6904f), proportionOfHeight (0.0952f));
    label->setBounds (proportionOfWidth (0.0413f), proportionOfHeight (0.1361f), proportionOfWidth (0.9156f), proportionOfHeight (0.1225f));
    label2->setBounds (proportionOfWidth (0.0901f), proportionOfHeight (0.2993f), proportionOfWidth (0.3602f), proportionOfHeight (0.0408f));
    label3->setBounds (proportionOfWidth (0.0901f), proportionOfHeight (0.3401f), proportionOfWidth (0.3602f), proportionOfHeight (0.0408f));
    label4->setBounds (proportionOfWidth (0.0901f), proportionOfHeight (0.3810f), proportionOfWidth (0.3602f), proportionOfHeight (0.0408f));
    label5->setBounds (proportionOfWidth (0.0901f), proportionOfHeight (0.4218f), proportionOfWidth (0.3602f), proportionOfHeight (0.0408f));
    buttonAdd->setBounds (proportionOfWidth (0.0600f), proportionOfHeight (0.5170f), proportionOfWidth (0.2814f), proportionOfHeight (0.0408f));
    buttonRemove->setBounds (proportionOfWidth (0.6604f), proportionOfHeight (0.5170f), proportionOfWidth (0.2814f), proportionOfHeight (0.0408f));
    sliderVirtual->setBounds (proportionOfWidth (0.4503f), proportionOfHeight (0.5714f), proportionOfWidth (0.5103f), proportionOfHeight (0.0408f));
    label6->setBounds (proportionOfWidth (0.0450f), proportionOfHeight (0.5714f), proportionOfWidth (0.3602f), proportionOfHeight (0.0408f));
    label7->setBounds (proportionOfWidth (0.0450f), proportionOfHeight (0.6803f), proportionOfWidth (0.9006f), proportionOfHeight (0.2721f));
    labelLoad->setBounds (proportionOfWidth (0.4953f), proportionOfHeight (0.2993f), proportionOfWidth (0.1801f), proportionOfHeight (0.0408f));
    labelAllSounds->setBounds (proportionOfWidth (0.4953f), proportionOfHeight (0.3401f), proportionOfWidth (0.1801f), proportionOfHeight (0.0408f));
    labelPlayingSounds->setBounds (proportionOfWidth (0.4953f), proportionOfHeight (0.3810f), proportionOfWidth (0.1801f), proportionOfHeight (0.0408f));
    labelVirtualSounds->setBounds (proportionOfWidth (0.4953f), proportionOfHeight (0.4218f), proportionOfWidth (0.1801f), proportionOfHeight (0.0408f));
    //[UserResized] Add your own custom resize handling here..
    //[/UserResized]
}

void cpuLoad::buttonClicked (Button* buttonThatWasClicked)
{
    //[UserbuttonClicked_Pre]
    //[/UserbuttonClicked_Pre]

    if (buttonThatWasClicked == buttonAdd)
    {
        //[UserButtonCode_buttonAdd] -- add your button handler code here..
      for (int x = 0; x < 10; x++) {
        Random & rand = Random::getSystemRandom();
        Sound().cpuTab.emplace_front();
        switch (rand.nextInt(4)) {
        case 0: Sound().cpuTab.front().create(new MemoryInputStream(BinaryData::g_ogg, BinaryData::g_oggSize, false), &YSE::ChannelAmbient(), true, 0.1f); break;
        case 1: Sound().cpuTab.front().create(new MemoryInputStream(BinaryData::kick_ogg, BinaryData::kick_oggSize, false), &YSE::ChannelFX(), true, 0.1f); break;
        case 2: Sound().cpuTab.front().create(new MemoryInputStream(BinaryData::drone_ogg, BinaryData::drone_oggSize, false), &YSE::ChannelMusic(), true, 0.1f); break;
        case 3: Sound().cpuTab.front().create(new MemoryInputStream(BinaryData::snare_ogg, BinaryData::snare_oggSize, false), &YSE::ChannelVoice(), true, 0.1f); break;
        }
        Sound().cpuTab.front().setPosition(YSE::Vec((rand.nextFloat() - 0.5f) * 10, (rand.nextFloat() - 0.5f) * 10, (rand.nextFloat() - 0.5f) * 10));
        Sound().cpuTab.front().setSpeed(rand.nextFloat() + 0.5f);
        Sound().cpuTab.front().play();
        soundCounter++;
        updateCounter();
      }
        //[/UserButtonCode_buttonAdd]
    }
    else if (buttonThatWasClicked == buttonRemove)
    {
        //[UserButtonCode_buttonRemove] -- add your button handler code here..
      for (int x = 0; x < 10; x++) {
        if (!Sound().cpuTab.empty()) {
          Sound().cpuTab.pop_front();
          soundCounter--;
          updateCounter();
        }
      }
        //[/UserButtonCode_buttonRemove]
    }

    //[UserbuttonClicked_Post]
    //[/UserbuttonClicked_Post]
}

void cpuLoad::sliderValueChanged (Slider* sliderThatWasMoved)
{
    //[UsersliderValueChanged_Pre]
    //[/UsersliderValueChanged_Pre]

    if (sliderThatWasMoved == sliderVirtual)
    {
        //[UserSliderCode_sliderVirtual] -- add your slider handling code here..
        Int v = static_cast<Int>(sliderVirtual->getValue());
        YSE::System().maxSounds(v);
        updateCounter();

        //[/UserSliderCode_sliderVirtual]
    }

    //[UsersliderValueChanged_Post]
    //[/UsersliderValueChanged_Post]
}



//[MiscUserCode] You can add your own definitions of your custom methods or any other code here...
void cpuLoad::updateCounter() {
  labelAllSounds->setText(String(soundCounter), NotificationType::sendNotification);
  if (soundCounter < YSE::System().maxSounds()) {
    labelPlayingSounds->setText(String(soundCounter), NotificationType::sendNotification);
  }
  if (soundCounter > YSE::System().maxSounds()) {
    labelVirtualSounds->setText(String(soundCounter - YSE::System().maxSounds()), NotificationType::sendNotification);
  }
  else {
    labelVirtualSounds->setText(String(0), NotificationType::sendNotification);
  }
}

//[/MiscUserCode]


//==============================================================================
#if 0
/*  -- Introjucer information section --

    This is where the Introjucer stores the metadata that describe this GUI layout, so
    make changes in here at your peril!

BEGIN_JUCER_METADATA

<JUCER_COMPONENT documentType="Component" className="cpuLoad" componentName=""
                 parentClasses="public Component" constructorParams="" variableInitialisers=""
                 snapPixels="8" snapActive="1" snapShown="1" overlayOpacity="0.330"
                 fixedSize="0" initialWidth="600" initialHeight="400">
  <BACKGROUND backgroundColour="ff000000">
    <RECT pos="2.251% 2.041% 95.31% 22.449%" fill="solid: ff808080" hasStroke="0"/>
    <RECT pos="2.251% 27.211% 95.31% 70.748%" fill="solid: ffcccccc" hasStroke="0"/>
    <ROUNDRECT pos="5.066% 29.252% 90.432% 17.857%" cornerSize="10" fill="solid: ff404040"
               hasStroke="1" stroke="5, mitered, butt" strokeColour="solid: ff2a68a5"/>
  </BACKGROUND>
  <LABEL name="labelTitle" id="e0b01ad84f543f79" memberName="labelTitle"
         virtualName="" explicitFocusOrder="0" pos="14.634% 4.082% 69.043% 9.524%"
         bkgCol="ff6495ed" edTextCol="ff000000" edBkgCol="0" labelText="CPU load"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="40" bold="1" italic="0" justification="36"/>
  <LABEL name="new label" id="2b173b509e785c8e" memberName="label" virtualName=""
         explicitFocusOrder="0" pos="4.128% 13.605% 91.557% 12.245%" edTextCol="ff000000"
         edBkgCol="0" labelText="This example gives you an idea of YSE's processing speed. Sounds are placed at a random position."
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="0" italic="0" justification="36"/>
  <LABEL name="new label" id="1fc8d743ec471e25" memberName="label2" virtualName=""
         explicitFocusOrder="0" pos="9.006% 29.932% 36.023% 4.082%" textCol="ffffffff"
         edTextCol="ff000000" edBkgCol="0" labelText="Current load of audio thread: "
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="1" italic="0" justification="33"/>
  <LABEL name="new label" id="7205dad0be1f6e8d" memberName="label3" virtualName=""
         explicitFocusOrder="0" pos="9.006% 34.014% 36.023% 4.082%" textCol="ffffffff"
         edTextCol="ff000000" edBkgCol="0" labelText="Sounds in memory:"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="1" italic="0" justification="33"/>
  <LABEL name="new label" id="af464e43dcaa2897" memberName="label4" virtualName=""
         explicitFocusOrder="0" pos="9.006% 38.095% 36.023% 4.082%" textCol="ffffffff"
         edTextCol="ff000000" edBkgCol="0" labelText="Non virtual sounds:"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="1" italic="0" justification="33"/>
  <LABEL name="new label" id="46b9e9980b5595fc" memberName="label5" virtualName=""
         explicitFocusOrder="0" pos="9.006% 42.177% 36.023% 4.082%" textCol="ffffffff"
         edTextCol="ff000000" edBkgCol="0" labelText="Virtual sounds:"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="1" italic="0" justification="33"/>
  <TEXTBUTTON name="new button" id="e2c1d34e4e00b9dc" memberName="buttonAdd"
              virtualName="" explicitFocusOrder="0" pos="6.004% 51.701% 28.143% 4.082%"
              buttonText="add 10 sounds" connectedEdges="0" needsCallback="1"
              radioGroupId="0"/>
  <TEXTBUTTON name="new button" id="974a46eea8e8b603" memberName="buttonRemove"
              virtualName="" explicitFocusOrder="0" pos="66.041% 51.701% 28.143% 4.082%"
              buttonText="remove 10 sounds" connectedEdges="0" needsCallback="1"
              radioGroupId="0"/>
  <SLIDER name="new slider" id="3efc9738ef685ef2" memberName="sliderVirtual"
          virtualName="" explicitFocusOrder="0" pos="45.028% 57.143% 51.032% 4.082%"
          min="50" max="2000" int="0" style="LinearHorizontal" textBoxPos="TextBoxLeft"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
  <LABEL name="new label" id="e8a95fa639d4f9a2" memberName="label6" virtualName=""
         explicitFocusOrder="0" pos="4.503% 57.143% 36.023% 4.082%" textCol="ff000000"
         edTextCol="ff000000" edBkgCol="0" labelText="Adjust non virtual sounds"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="1" italic="0" justification="33"/>
  <LABEL name="new label" id="517c64872bd4475b" memberName="label7" virtualName=""
         explicitFocusOrder="0" pos="4.503% 68.027% 90.056% 27.211%" edTextCol="ff000000"
         edBkgCol="0" labelText="By default, YSE will play only 50 sounds at a time. Others are virtualized. This means that the engine will (at each update) evaluate which sounds are the least audible. Those sounds will be virtual until they become more important or until other sounds dissapear. Virtual sounds still accept changes and have their volume and velocity updated, but they will not be added to the main mix."
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="0" italic="0" justification="9"/>
  <LABEL name="new label" id="42ab0852a5f44bc6" memberName="labelLoad"
         virtualName="" explicitFocusOrder="0" pos="49.531% 29.932% 18.011% 4.082%"
         textCol="ffffffff" edTextCol="ff000000" edBkgCol="0" labelText="0"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="1" italic="0" justification="34"/>
  <LABEL name="new label" id="5b2dd28bf3a113a0" memberName="labelAllSounds"
         virtualName="" explicitFocusOrder="0" pos="49.531% 34.014% 18.011% 4.082%"
         textCol="ffffffff" edTextCol="ff000000" edBkgCol="0" labelText="0"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="1" italic="0" justification="34"/>
  <LABEL name="new label" id="6ab11a56678fee15" memberName="labelPlayingSounds"
         virtualName="" explicitFocusOrder="0" pos="49.531% 38.095% 18.011% 4.082%"
         textCol="ffffffff" edTextCol="ff000000" edBkgCol="0" labelText="0"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="1" italic="0" justification="34"/>
  <LABEL name="new label" id="2c099d2af86a1464" memberName="labelVirtualSounds"
         virtualName="" explicitFocusOrder="0" pos="49.531% 42.177% 18.011% 4.082%"
         textCol="ffffffff" edTextCol="ff000000" edBkgCol="0" labelText="0"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="1" italic="0" justification="34"/>
</JUCER_COMPONENT>

END_JUCER_METADATA
*/
#endif


//[EndFile] You can add extra defines here...
//[/EndFile]
