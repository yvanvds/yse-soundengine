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
//[/Headers]

#include "basic.h"


//[MiscUserDefs] You can add your own user definitions and misc code here...
//[/MiscUserDefs]

//==============================================================================
basicTab::basicTab ()
{
    setName ("basicTab");
    addAndMakeVisible (onOffButton2 = new ToggleButton ("onOffButton2"));
    onOffButton2->setButtonText (TRANS("play/pause second sound instance"));
    onOffButton2->addListener (this);

    addAndMakeVisible (labelTitle = new Label ("labelTitle",
                                               TRANS("Basic 2D example")));
    labelTitle->setFont (Font (40.00f, Font::bold));
    labelTitle->setJustificationType (Justification::centred);
    labelTitle->setEditable (false, false, false);
    labelTitle->setColour (Label::backgroundColourId, Colours::cornflowerblue);
    labelTitle->setColour (TextEditor::textColourId, Colours::black);
    labelTitle->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (label = new Label ("new label",
                                          TRANS("This example uses one soundfile, loaded into memory. Three objects are used to read from the same file. It demonstrates all sound objects are independent of each other.")));
    label->setFont (Font (15.00f, Font::plain));
    label->setJustificationType (Justification::centred);
    label->setEditable (false, false, false);
    label->setColour (TextEditor::textColourId, Colours::black);
    label->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (volumeSlider2 = new Slider ("volumeSlider2"));
    volumeSlider2->setRange (0, 1, 0);
    volumeSlider2->setSliderStyle (Slider::LinearHorizontal);
    volumeSlider2->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
    volumeSlider2->addListener (this);

    addAndMakeVisible (panSlider2 = new Slider ("panSlider2"));
    panSlider2->setRange (-1, 1, 0);
    panSlider2->setSliderStyle (Slider::LinearHorizontal);
    panSlider2->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
    panSlider2->addListener (this);

    addAndMakeVisible (speedSlider2 = new Slider ("speedSlider2"));
    speedSlider2->setRange (-10, 10, 0);
    speedSlider2->setSliderStyle (Slider::LinearHorizontal);
    speedSlider2->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
    speedSlider2->addListener (this);

    addAndMakeVisible (onOffButton1 = new ToggleButton ("onOffButton1"));
    onOffButton1->setButtonText (TRANS("play/pause first sound instance"));
    onOffButton1->addListener (this);

    addAndMakeVisible (volumeSlider1 = new Slider ("volumeSlider1"));
    volumeSlider1->setRange (0, 1, 0);
    volumeSlider1->setSliderStyle (Slider::LinearHorizontal);
    volumeSlider1->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
    volumeSlider1->addListener (this);

    addAndMakeVisible (panSlider1 = new Slider ("panSlider1"));
    panSlider1->setRange (-1, 1, 0);
    panSlider1->setSliderStyle (Slider::LinearHorizontal);
    panSlider1->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
    panSlider1->addListener (this);

    addAndMakeVisible (speedSlider1 = new Slider ("speedSlider1"));
    speedSlider1->setRange (-10, 10, 0);
    speedSlider1->setSliderStyle (Slider::LinearHorizontal);
    speedSlider1->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
    speedSlider1->addListener (this);

    addAndMakeVisible (onOffButton3 = new ToggleButton ("onOffButton3"));
    onOffButton3->setButtonText (TRANS("play/pause first sound instance"));
    onOffButton3->addListener (this);

    addAndMakeVisible (volumeSlider3 = new Slider ("volumeSlider3"));
    volumeSlider3->setRange (0, 1, 0);
    volumeSlider3->setSliderStyle (Slider::LinearHorizontal);
    volumeSlider3->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
    volumeSlider3->addListener (this);

    addAndMakeVisible (panSlider3 = new Slider ("panSlider3"));
    panSlider3->setRange (-1, 1, 0);
    panSlider3->setSliderStyle (Slider::LinearHorizontal);
    panSlider3->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
    panSlider3->addListener (this);

    addAndMakeVisible (speedSlider3 = new Slider ("speedSlider3"));
    speedSlider3->setRange (-10, 10, 0);
    speedSlider3->setSliderStyle (Slider::LinearHorizontal);
    speedSlider3->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
    speedSlider3->addListener (this);


    //[UserPreSize]

    //[/UserPreSize]

    setSize (600, 400);


    //[Constructor] You can add your own custom stuff here..
    volumeSlider1->setValue(0.5f);
    volumeSlider2->setValue(0.5f);
    volumeSlider3->setValue(0.5f);
    speedSlider1->setValue(1.f);
    speedSlider2->setValue(1.f);
    speedSlider3->setValue(1.f);
    //[/Constructor]
}

basicTab::~basicTab()
{
    //[Destructor_pre]. You can add your own custom destruction code here..
    //[/Destructor_pre]

    onOffButton2 = nullptr;
    labelTitle = nullptr;
    label = nullptr;
    volumeSlider2 = nullptr;
    panSlider2 = nullptr;
    speedSlider2 = nullptr;
    onOffButton1 = nullptr;
    volumeSlider1 = nullptr;
    panSlider1 = nullptr;
    speedSlider1 = nullptr;
    onOffButton3 = nullptr;
    volumeSlider3 = nullptr;
    panSlider3 = nullptr;
    speedSlider3 = nullptr;


    //[Destructor]. You can add your own custom destruction code here..
    //[/Destructor]
}

//==============================================================================
void basicTab::paint (Graphics& g)
{
    //[UserPrePaint] Add your own custom painting code here..
    //[/UserPrePaint]

    g.fillAll (Colours::black);

    g.setColour (Colours::grey);
    g.fillRect (proportionOfWidth (0.0225f), proportionOfHeight (0.0204f), proportionOfWidth (0.9531f), proportionOfHeight (0.2245f));

    g.setColour (Colour (0xffcccccc));
    g.fillRect (proportionOfWidth (0.0225f), proportionOfHeight (0.2653f), proportionOfWidth (0.9531f), proportionOfHeight (0.2245f));

    g.setColour (Colour (0xffcccccc));
    g.fillRect (proportionOfWidth (0.0225f), proportionOfHeight (0.5102f), proportionOfWidth (0.9531f), proportionOfHeight (0.2245f));

    g.setColour (Colour (0xffcccccc));
    g.fillRect (proportionOfWidth (0.0225f), proportionOfHeight (0.7551f), proportionOfWidth (0.9531f), proportionOfHeight (0.2245f));

    g.setColour (Colours::black);
    g.setFont (Font (15.00f, Font::bold));
    g.drawText (TRANS("Volume"),
                proportionOfWidth (0.0525f), proportionOfHeight (0.3197f), proportionOfWidth (0.1126f), proportionOfHeight (0.0510f),
                Justification::centred, true);

    g.setColour (Colours::black);
    g.setFont (Font (15.00f, Font::bold));
    g.drawText (TRANS("Pan"),
                proportionOfWidth (0.0525f), proportionOfHeight (0.3742f), proportionOfWidth (0.1126f), proportionOfHeight (0.0510f),
                Justification::centred, true);

    g.setColour (Colours::black);
    g.setFont (Font (15.00f, Font::bold));
    g.drawText (TRANS("Speed"),
                proportionOfWidth (0.0525f), proportionOfHeight (0.4286f), proportionOfWidth (0.1126f), proportionOfHeight (0.0510f),
                Justification::centred, true);

    g.setColour (Colours::black);
    g.setFont (Font (15.00f, Font::bold));
    g.drawText (TRANS("Volume"),
                proportionOfWidth (0.0525f), proportionOfHeight (0.5646f), proportionOfWidth (0.1126f), proportionOfHeight (0.0510f),
                Justification::centred, true);

    g.setColour (Colours::black);
    g.setFont (Font (15.00f, Font::bold));
    g.drawText (TRANS("Pan"),
                proportionOfWidth (0.0525f), proportionOfHeight (0.6191f), proportionOfWidth (0.1126f), proportionOfHeight (0.0510f),
                Justification::centred, true);

    g.setColour (Colours::black);
    g.setFont (Font (15.00f, Font::bold));
    g.drawText (TRANS("Speed"),
                proportionOfWidth (0.0525f), proportionOfHeight (0.6735f), proportionOfWidth (0.1126f), proportionOfHeight (0.0510f),
                Justification::centred, true);

    g.setColour (Colours::black);
    g.setFont (Font (15.00f, Font::bold));
    g.drawText (TRANS("Volume"),
                proportionOfWidth (0.0525f), proportionOfHeight (0.8095f), proportionOfWidth (0.1126f), proportionOfHeight (0.0510f),
                Justification::centred, true);

    g.setColour (Colours::black);
    g.setFont (Font (15.00f, Font::bold));
    g.drawText (TRANS("Pan"),
                proportionOfWidth (0.0525f), proportionOfHeight (0.8640f), proportionOfWidth (0.1126f), proportionOfHeight (0.0510f),
                Justification::centred, true);

    g.setColour (Colours::black);
    g.setFont (Font (15.00f, Font::bold));
    g.drawText (TRANS("Speed"),
                proportionOfWidth (0.0525f), proportionOfHeight (0.9184f), proportionOfWidth (0.1126f), proportionOfHeight (0.0510f),
                Justification::centred, true);

    //[UserPaint] Add your own custom painting code here..
    //[/UserPaint]
}

void basicTab::resized()
{
    onOffButton2->setBounds (proportionOfWidth (0.0600f), proportionOfHeight (0.5170f), proportionOfWidth (0.3602f), proportionOfHeight (0.0408f));
    labelTitle->setBounds (proportionOfWidth (0.1501f), proportionOfHeight (0.0272f), proportionOfWidth (0.6904f), proportionOfHeight (0.0952f));
    label->setBounds (proportionOfWidth (0.0450f), proportionOfHeight (0.1225f), proportionOfWidth (0.9156f), proportionOfHeight (0.1225f));
    volumeSlider2->setBounds (proportionOfWidth (0.1951f), proportionOfHeight (0.5714f), proportionOfWidth (0.7355f), proportionOfHeight (0.0408f));
    panSlider2->setBounds (proportionOfWidth (0.1951f), proportionOfHeight (0.6259f), proportionOfWidth (0.7355f), proportionOfHeight (0.0408f));
    speedSlider2->setBounds (proportionOfWidth (0.1951f), proportionOfHeight (0.6803f), proportionOfWidth (0.7355f), proportionOfHeight (0.0408f));
    onOffButton1->setBounds (proportionOfWidth (0.0563f), proportionOfHeight (0.2704f), proportionOfWidth (0.3602f), proportionOfHeight (0.0408f));
    volumeSlider1->setBounds (proportionOfWidth (0.1914f), proportionOfHeight (0.3248f), proportionOfWidth (0.7355f), proportionOfHeight (0.0408f));
    panSlider1->setBounds (proportionOfWidth (0.1914f), proportionOfHeight (0.3792f), proportionOfWidth (0.7355f), proportionOfHeight (0.0408f));
    speedSlider1->setBounds (proportionOfWidth (0.1914f), proportionOfHeight (0.4337f), proportionOfWidth (0.7355f), proportionOfHeight (0.0408f));
    onOffButton3->setBounds (proportionOfWidth (0.0563f), proportionOfHeight (0.7602f), proportionOfWidth (0.3602f), proportionOfHeight (0.0408f));
    volumeSlider3->setBounds (proportionOfWidth (0.1914f), proportionOfHeight (0.8146f), proportionOfWidth (0.7355f), proportionOfHeight (0.0408f));
    panSlider3->setBounds (proportionOfWidth (0.1914f), proportionOfHeight (0.8691f), proportionOfWidth (0.7355f), proportionOfHeight (0.0408f));
    speedSlider3->setBounds (proportionOfWidth (0.1914f), proportionOfHeight (0.9235f), proportionOfWidth (0.7355f), proportionOfHeight (0.0408f));
    //[UserResized] Add your own custom resize handling here..
    //[/UserResized]
}

void basicTab::buttonClicked (Button* buttonThatWasClicked)
{
    //[UserbuttonClicked_Pre]
    //[/UserbuttonClicked_Pre]

    if (buttonThatWasClicked == onOffButton2)
    {
        //[UserButtonCode_onOffButton2] -- add your button handler code here..
        Sound().basicTab[1]->toggle();
        //[/UserButtonCode_onOffButton2]
    }
    else if (buttonThatWasClicked == onOffButton1)
    {
        //[UserButtonCode_onOffButton1] -- add your button handler code here..
      Sound().basicTab[0]->toggle();
        //[/UserButtonCode_onOffButton1]
    }
    else if (buttonThatWasClicked == onOffButton3)
    {
        //[UserButtonCode_onOffButton3] -- add your button handler code here..
      Sound().basicTab[2]->toggle();
        //[/UserButtonCode_onOffButton3]
    }

    //[UserbuttonClicked_Post]
    //[/UserbuttonClicked_Post]
}

void basicTab::sliderValueChanged (Slider* sliderThatWasMoved)
{
    //[UsersliderValueChanged_Pre]
    //[/UsersliderValueChanged_Pre]

    if (sliderThatWasMoved == volumeSlider2)
    {
        //[UserSliderCode_volumeSlider2] -- add your slider handling code here..
      Sound().basicTab[1]->setVolume(static_cast<float>(volumeSlider2->getValue()));
        //[/UserSliderCode_volumeSlider2]
    }
    else if (sliderThatWasMoved == panSlider2)
    {
        //[UserSliderCode_panSlider2] -- add your slider handling code here..
      Sound().basicTab[1]->setPosition(YSE::Vec(static_cast<float>(panSlider2->getValue() * 10), 0.f, 1.f));
        //[/UserSliderCode_panSlider2]
    }
    else if (sliderThatWasMoved == speedSlider2)
    {
        //[UserSliderCode_speedSlider2] -- add your slider handling code here..
      Sound().basicTab[1]->setSpeed(static_cast<float>(speedSlider2->getValue()));
        //[/UserSliderCode_speedSlider2]
    }
    else if (sliderThatWasMoved == volumeSlider1)
    {
        //[UserSliderCode_volumeSlider1] -- add your slider handling code here..
      Sound().basicTab[0]->setVolume(static_cast<float>(volumeSlider1->getValue()));
        //[/UserSliderCode_volumeSlider1]
    }
    else if (sliderThatWasMoved == panSlider1)
    {
        //[UserSliderCode_panSlider1] -- add your slider handling code here..
      Sound().basicTab[0]->setPosition(YSE::Vec(static_cast<float>(panSlider1->getValue() * 10), 0.f, 1.f));
        //[/UserSliderCode_panSlider1]
    }
    else if (sliderThatWasMoved == speedSlider1)
    {
        //[UserSliderCode_speedSlider1] -- add your slider handling code here..
      Sound().basicTab[0]->setSpeed(static_cast<float>(speedSlider1->getValue()));
        //[/UserSliderCode_speedSlider1]
    }
    else if (sliderThatWasMoved == volumeSlider3)
    {
        //[UserSliderCode_volumeSlider3] -- add your slider handling code here..
      Sound().basicTab[2]->setVolume(static_cast<float>(volumeSlider3->getValue()));
        //[/UserSliderCode_volumeSlider3]
    }
    else if (sliderThatWasMoved == panSlider3)
    {
        //[UserSliderCode_panSlider3] -- add your slider handling code here..
      Sound().basicTab[2]->setPosition(YSE::Vec(static_cast<float>(panSlider3->getValue() * 10), 0, 1));
        //[/UserSliderCode_panSlider3]
    }
    else if (sliderThatWasMoved == speedSlider3)
    {
        //[UserSliderCode_speedSlider3] -- add your slider handling code here..
      Sound().basicTab[2]->setSpeed(static_cast<float>(speedSlider3->getValue()));
        //[/UserSliderCode_speedSlider3]
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

<JUCER_COMPONENT documentType="Component" className="basicTab" componentName="basicTab"
                 parentClasses="public Component" constructorParams="" variableInitialisers=""
                 snapPixels="8" snapActive="1" snapShown="1" overlayOpacity="0.330"
                 fixedSize="0" initialWidth="600" initialHeight="400">
  <BACKGROUND backgroundColour="ff000000">
    <RECT pos="2.251% 2.041% 95.31% 22.449%" fill="solid: ff808080" hasStroke="0"/>
    <RECT pos="2.251% 26.531% 95.31% 22.449%" fill="solid: ffcccccc" hasStroke="0"/>
    <RECT pos="2.251% 51.02% 95.31% 22.449%" fill="solid: ffcccccc" hasStroke="0"/>
    <RECT pos="2.251% 75.51% 95.31% 22.449%" fill="solid: ffcccccc" hasStroke="0"/>
    <TEXT pos="5.253% 31.973% 11.257% 5.102%" fill="solid: ff000000" hasStroke="0"
          text="Volume" fontname="Default font" fontsize="15" bold="1"
          italic="0" justification="36"/>
    <TEXT pos="5.253% 37.415% 11.257% 5.102%" fill="solid: ff000000" hasStroke="0"
          text="Pan" fontname="Default font" fontsize="15" bold="1" italic="0"
          justification="36"/>
    <TEXT pos="5.253% 42.857% 11.257% 5.102%" fill="solid: ff000000" hasStroke="0"
          text="Speed" fontname="Default font" fontsize="15" bold="1" italic="0"
          justification="36"/>
    <TEXT pos="5.253% 56.463% 11.257% 5.102%" fill="solid: ff000000" hasStroke="0"
          text="Volume" fontname="Default font" fontsize="15" bold="1"
          italic="0" justification="36"/>
    <TEXT pos="5.253% 61.905% 11.257% 5.102%" fill="solid: ff000000" hasStroke="0"
          text="Pan" fontname="Default font" fontsize="15" bold="1" italic="0"
          justification="36"/>
    <TEXT pos="5.253% 67.347% 11.257% 5.102%" fill="solid: ff000000" hasStroke="0"
          text="Speed" fontname="Default font" fontsize="15" bold="1" italic="0"
          justification="36"/>
    <TEXT pos="5.253% 80.952% 11.257% 5.102%" fill="solid: ff000000" hasStroke="0"
          text="Volume" fontname="Default font" fontsize="15" bold="1"
          italic="0" justification="36"/>
    <TEXT pos="5.253% 86.395% 11.257% 5.102%" fill="solid: ff000000" hasStroke="0"
          text="Pan" fontname="Default font" fontsize="15" bold="1" italic="0"
          justification="36"/>
    <TEXT pos="5.253% 91.837% 11.257% 5.102%" fill="solid: ff000000" hasStroke="0"
          text="Speed" fontname="Default font" fontsize="15" bold="1" italic="0"
          justification="36"/>
  </BACKGROUND>
  <TOGGLEBUTTON name="onOffButton2" id="c0df1f89f034b7af" memberName="onOffButton2"
                virtualName="" explicitFocusOrder="0" pos="6.004% 51.701% 36.023% 4.082%"
                buttonText="play/pause second sound instance" connectedEdges="0"
                needsCallback="1" radioGroupId="0" state="0"/>
  <LABEL name="labelTitle" id="e0b01ad84f543f79" memberName="labelTitle"
         virtualName="" explicitFocusOrder="0" pos="15.009% 2.721% 69.043% 9.524%"
         bkgCol="ff6495ed" edTextCol="ff000000" edBkgCol="0" labelText="Basic 2D example"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="40" bold="1" italic="0" justification="36"/>
  <LABEL name="new label" id="2b173b509e785c8e" memberName="label" virtualName=""
         explicitFocusOrder="0" pos="4.503% 12.245% 91.557% 12.245%" edTextCol="ff000000"
         edBkgCol="0" labelText="This example uses one soundfile, loaded into memory. Three objects are used to read from the same file. It demonstrates all sound objects are independent of each other."
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="0" italic="0" justification="36"/>
  <SLIDER name="volumeSlider2" id="6f1bf5e117b25116" memberName="volumeSlider2"
          virtualName="" explicitFocusOrder="0" pos="19.512% 57.143% 73.546% 4.082%"
          min="0" max="1" int="0" style="LinearHorizontal" textBoxPos="TextBoxLeft"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
  <SLIDER name="panSlider2" id="ed1125466d4aca2c" memberName="panSlider2"
          virtualName="" explicitFocusOrder="0" pos="19.512% 62.585% 73.546% 4.082%"
          min="-1" max="1" int="0" style="LinearHorizontal" textBoxPos="TextBoxLeft"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
  <SLIDER name="speedSlider2" id="ea275b5d32a9de15" memberName="speedSlider2"
          virtualName="" explicitFocusOrder="0" pos="19.512% 68.027% 73.546% 4.082%"
          min="-10" max="10" int="0" style="LinearHorizontal" textBoxPos="TextBoxLeft"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
  <TOGGLEBUTTON name="onOffButton1" id="d092298ce028045c" memberName="onOffButton1"
                virtualName="" explicitFocusOrder="0" pos="5.629% 27.041% 36.023% 4.082%"
                buttonText="play/pause first sound instance" connectedEdges="0"
                needsCallback="1" radioGroupId="0" state="0"/>
  <SLIDER name="volumeSlider1" id="89c82f1addd8dd91" memberName="volumeSlider1"
          virtualName="" explicitFocusOrder="0" pos="19.137% 32.483% 73.546% 4.082%"
          min="0" max="1" int="0" style="LinearHorizontal" textBoxPos="TextBoxLeft"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
  <SLIDER name="panSlider1" id="4c3a89ea969ec4c4" memberName="panSlider1"
          virtualName="" explicitFocusOrder="0" pos="19.137% 37.925% 73.546% 4.082%"
          min="-1" max="1" int="0" style="LinearHorizontal" textBoxPos="TextBoxLeft"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
  <SLIDER name="speedSlider1" id="549f81ec2b2046c7" memberName="speedSlider1"
          virtualName="" explicitFocusOrder="0" pos="19.137% 43.367% 73.546% 4.082%"
          min="-10" max="10" int="0" style="LinearHorizontal" textBoxPos="TextBoxLeft"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
  <TOGGLEBUTTON name="onOffButton3" id="172d86281dbfa0dc" memberName="onOffButton3"
                virtualName="" explicitFocusOrder="0" pos="5.629% 76.02% 36.023% 4.082%"
                buttonText="play/pause first sound instance" connectedEdges="0"
                needsCallback="1" radioGroupId="0" state="0"/>
  <SLIDER name="volumeSlider3" id="92ce00cc6452dfc" memberName="volumeSlider3"
          virtualName="" explicitFocusOrder="0" pos="19.137% 81.463% 73.546% 4.082%"
          min="0" max="1" int="0" style="LinearHorizontal" textBoxPos="TextBoxLeft"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
  <SLIDER name="panSlider3" id="28f151161d7b3708" memberName="panSlider3"
          virtualName="" explicitFocusOrder="0" pos="19.137% 86.905% 73.546% 4.082%"
          min="-1" max="1" int="0" style="LinearHorizontal" textBoxPos="TextBoxLeft"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
  <SLIDER name="speedSlider3" id="58963fba7ecef5d3" memberName="speedSlider3"
          virtualName="" explicitFocusOrder="0" pos="19.137% 92.347% 73.546% 4.082%"
          min="-10" max="10" int="0" style="LinearHorizontal" textBoxPos="TextBoxLeft"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
</JUCER_COMPONENT>

END_JUCER_METADATA
*/
#endif


//[EndFile] You can add extra defines here...
//[/EndFile]
