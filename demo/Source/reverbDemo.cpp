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
//[/Headers]

#include "reverbDemo.h"


//[MiscUserDefs] You can add your own user definitions and misc code here...
//[/MiscUserDefs]

//==============================================================================
reverbDemo::reverbDemo ()
{
    addAndMakeVisible (labelTitle = new Label ("labelTitle",
                                               TRANS("Reverb Example")));
    labelTitle->setFont (Font (40.00f, Font::bold));
    labelTitle->setJustificationType (Justification::centred);
    labelTitle->setEditable (false, false, false);
    labelTitle->setColour (Label::backgroundColourId, Colours::cornflowerblue);
    labelTitle->setColour (TextEditor::textColourId, Colours::black);
    labelTitle->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (label = new Label ("new label",
                                          TRANS("This example has a global reverb applied, and has 4 local reverbs. When the listener is moved, the resulting reverb is calculated accordingly.")));
    label->setFont (Font (15.00f, Font::plain));
    label->setJustificationType (Justification::centred);
    label->setEditable (false, false, false);
    label->setColour (TextEditor::textColourId, Colours::black);
    label->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (playButton = new ToggleButton ("new toggle button"));
    playButton->setTooltip (TRANS("Toggles both sounds on or off."));
    playButton->setButtonText (TRANS("Play / Pause"));
    playButton->addListener (this);

    addAndMakeVisible (volumeSlider = new Slider ("new slider"));
    volumeSlider->setTooltip (TRANS("The volume of the main mix."));
    volumeSlider->setRange (0, 1, 0);
    volumeSlider->setSliderStyle (Slider::LinearHorizontal);
    volumeSlider->setTextBoxStyle (Slider::NoTextBox, false, 80, 20);
    volumeSlider->addListener (this);

    addAndMakeVisible (label2 = new Label ("new label",
                                           TRANS("Volume")));
    label2->setFont (Font (15.00f, Font::bold));
    label2->setJustificationType (Justification::centredLeft);
    label2->setEditable (false, false, false);
    label2->setColour (TextEditor::textColourId, Colours::black);
    label2->setColour (TextEditor::backgroundColourId, Colour (0x00000000));


    //[UserPreSize]
    //[/UserPreSize]

    setSize (600, 400);


    //[Constructor] You can add your own custom stuff here..
    //[/Constructor]
}

reverbDemo::~reverbDemo()
{
    //[Destructor_pre]. You can add your own custom destruction code here..
    //[/Destructor_pre]

    labelTitle = nullptr;
    label = nullptr;
    playButton = nullptr;
    volumeSlider = nullptr;
    label2 = nullptr;


    //[Destructor]. You can add your own custom destruction code here..
    //[/Destructor]
}

//==============================================================================
void reverbDemo::paint (Graphics& g)
{
    //[UserPrePaint] Add your own custom painting code here..
    //[/UserPrePaint]

    g.fillAll (Colours::white);

    g.setColour (Colours::grey);
    g.fillRect (proportionOfWidth (0.0225f), proportionOfHeight (0.0204f), proportionOfWidth (0.9531f), proportionOfHeight (0.2245f));

    g.setColour (Colour (0xffcccccc));
    g.fillRect (proportionOfWidth (0.0225f), proportionOfHeight (0.2721f), proportionOfWidth (0.9531f), proportionOfHeight (0.7075f));

    g.setColour (Colour (0xff404040));
    g.fillRoundedRectangle (static_cast<float> (proportionOfWidth (0.0525f)), static_cast<float> (proportionOfHeight (0.3469f)), static_cast<float> (proportionOfWidth (0.8931f)), static_cast<float> (proportionOfHeight (0.6054f)), 10.000f);

    g.setColour (Colour (0xff2a68a5));
    g.drawRoundedRectangle (static_cast<float> (proportionOfWidth (0.0525f)), static_cast<float> (proportionOfHeight (0.3469f)), static_cast<float> (proportionOfWidth (0.8931f)), static_cast<float> (proportionOfHeight (0.6054f)), 10.000f, 5.000f);

    //[UserPaint] Add your own custom painting code here..
    //[/UserPaint]
}

void reverbDemo::resized()
{
    labelTitle->setBounds (proportionOfWidth (0.1501f), proportionOfHeight (0.0408f), proportionOfWidth (0.6904f), proportionOfHeight (0.0952f));
    label->setBounds (proportionOfWidth (0.0450f), proportionOfHeight (0.1225f), proportionOfWidth (0.9156f), proportionOfHeight (0.1225f));
    playButton->setBounds (proportionOfWidth (0.0450f), proportionOfHeight (0.2874f), proportionOfWidth (0.1951f), proportionOfHeight (0.0408f));
    volumeSlider->setBounds (proportionOfWidth (0.5554f), proportionOfHeight (0.2874f), proportionOfWidth (0.4053f), proportionOfHeight (0.0408f));
    label2->setBounds (proportionOfWidth (0.4203f), proportionOfHeight (0.2874f), proportionOfWidth (0.1051f), proportionOfHeight (0.0408f));
    //[UserResized] Add your own custom resize handling here..
    //[/UserResized]
}

void reverbDemo::buttonClicked (Button* buttonThatWasClicked)
{
    //[UserbuttonClicked_Pre]
    //[/UserbuttonClicked_Pre]

    if (buttonThatWasClicked == playButton)
    {
        //[UserButtonCode_playButton] -- add your button handler code here..
        //[/UserButtonCode_playButton]
    }

    //[UserbuttonClicked_Post]
    //[/UserbuttonClicked_Post]
}

void reverbDemo::sliderValueChanged (Slider* sliderThatWasMoved)
{
    //[UsersliderValueChanged_Pre]
    //[/UsersliderValueChanged_Pre]

    if (sliderThatWasMoved == volumeSlider)
    {
        //[UserSliderCode_volumeSlider] -- add your slider handling code here..
        //[/UserSliderCode_volumeSlider]
    }

    //[UsersliderValueChanged_Post]
    //[/UsersliderValueChanged_Post]
}



//[MiscUserCode] You can add your own definitions of your custom methods or any other code here...
//[/MiscUserCode]


//==============================================================================
#if 0
/*  -- Introjucer information section --

    This is where the Introjucer stores the metadata that describe this GUI layout, so
    make changes in here at your peril!

BEGIN_JUCER_METADATA

<JUCER_COMPONENT documentType="Component" className="reverbDemo" componentName=""
                 parentClasses="public Component" constructorParams="" variableInitialisers=""
                 snapPixels="8" snapActive="1" snapShown="1" overlayOpacity="0.330"
                 fixedSize="0" initialWidth="600" initialHeight="400">
  <BACKGROUND backgroundColour="ffffffff">
    <RECT pos="2.251% 2.041% 95.31% 22.449%" fill="solid: ff808080" hasStroke="0"/>
    <RECT pos="2.251% 27.211% 95.31% 70.748%" fill="solid: ffcccccc" hasStroke="0"/>
    <ROUNDRECT pos="5.253% 34.694% 89.306% 60.544%" cornerSize="10" fill="solid: ff404040"
               hasStroke="1" stroke="5, mitered, butt" strokeColour="solid: ff2a68a5"/>
  </BACKGROUND>
  <LABEL name="labelTitle" id="e0b01ad84f543f79" memberName="labelTitle"
         virtualName="" explicitFocusOrder="0" pos="15.009% 4.082% 69.043% 9.524%"
         bkgCol="ff6495ed" edTextCol="ff000000" edBkgCol="0" labelText="Reverb Example"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="40" bold="1" italic="0" justification="36"/>
  <LABEL name="new label" id="2b173b509e785c8e" memberName="label" virtualName=""
         explicitFocusOrder="0" pos="4.503% 12.245% 91.557% 12.245%" edTextCol="ff000000"
         edBkgCol="0" labelText="This example has a global reverb applied, and has 4 local reverbs. When the listener is moved, the resulting reverb is calculated accordingly."
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="0" italic="0" justification="36"/>
  <TOGGLEBUTTON name="new toggle button" id="e2ff1e43222f9439" memberName="playButton"
                virtualName="" explicitFocusOrder="0" pos="4.503% 28.741% 19.512% 4.082%"
                tooltip="Toggles both sounds on or off." buttonText="Play / Pause"
                connectedEdges="0" needsCallback="1" radioGroupId="0" state="0"/>
  <SLIDER name="new slider" id="d7af7d638b2603ff" memberName="volumeSlider"
          virtualName="" explicitFocusOrder="0" pos="55.535% 28.741% 40.525% 4.082%"
          tooltip="The volume of the main mix." min="0" max="1" int="0"
          style="LinearHorizontal" textBoxPos="NoTextBox" textBoxEditable="1"
          textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
  <LABEL name="new label" id="6da4ee40c181dadf" memberName="label2" virtualName=""
         explicitFocusOrder="0" pos="42.026% 28.741% 10.507% 4.082%" edTextCol="ff000000"
         edBkgCol="0" labelText="Volume" editableSingleClick="0" editableDoubleClick="0"
         focusDiscardsChanges="0" fontname="Default font" fontsize="15"
         bold="1" italic="0" justification="33"/>
</JUCER_COMPONENT>

END_JUCER_METADATA
*/
#endif


//[EndFile] You can add extra defines here...
//[/EndFile]
