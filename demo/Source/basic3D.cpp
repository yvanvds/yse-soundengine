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
#include "parts/yseTimerThread.h"
//[/Headers]

#include "basic3D.h"


//[MiscUserDefs] You can add your own user definitions and misc code here...
//[/MiscUserDefs]

//==============================================================================
basic3D::basic3D ()
{
    setName ("basic3D");
    addAndMakeVisible (labelTitle = new Label ("labelTitle",
                                               TRANS("Basic 3D example")));
    labelTitle->setFont (Font (40.00f, Font::bold));
    labelTitle->setJustificationType (Justification::centred);
    labelTitle->setEditable (false, false, false);
    labelTitle->setColour (Label::backgroundColourId, Colours::cornflowerblue);
    labelTitle->setColour (TextEditor::textColourId, Colours::black);
    labelTitle->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (label = new Label ("new label",
                                          TRANS("This example has 2 sound sources and a listener. These can be moved around to simulate a 3D experience. A doppler effect is active by default.\n")));
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

    addAndMakeVisible (listenerButton = new draggedComponent ("new button"));
    listenerButton->setTooltip (TRANS("the listener"));

    listenerButton->setImages (false, true, true,
                               ImageCache::getFromMemory (walk_png, walk_pngSize), 1.000f, Colour (0x00000000),
                               Image(), 1.000f, Colour (0x00000000),
                               Image(), 1.000f, Colour (0x00000000));
    addAndMakeVisible (coordLabel = new Label ("new label",
                                               TRANS("x: 0 z: 0")));
    coordLabel->setTooltip (TRANS("virtual position of the last moved item."));
    coordLabel->setFont (Font (15.00f, Font::plain));
    coordLabel->setJustificationType (Justification::centredLeft);
    coordLabel->setEditable (false, false, false);
    coordLabel->setColour (Label::textColourId, Colours::white);
    coordLabel->setColour (TextEditor::textColourId, Colours::black);
    coordLabel->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (soundButton1 = new draggedComponent ("new button"));
    soundButton1->setTooltip (TRANS("first sound"));

    soundButton1->setImages (false, true, true,
                             ImageCache::getFromMemory (sound_png, sound_pngSize), 1.000f, Colour (0x00000000),
                             Image(), 1.000f, Colour (0x00000000),
                             Image(), 1.000f, Colour (0x00000000));
    addAndMakeVisible (soundButton2 = new draggedComponent ("new button"));
    soundButton2->setTooltip (TRANS("second sound"));

    soundButton2->setImages (false, true, true,
                             ImageCache::getFromMemory (sound_png, sound_pngSize), 1.000f, Colour (0x00000000),
                             Image(), 1.000f, Colour (0x00000000),
                             Image(), 1.000f, Colour (0x00000000));

    //[UserPreSize]
    soundButton1->setDragBounds(this);
    soundButton2->setDragBounds(this);
    listenerButton->setDragBounds(this);
    //[/UserPreSize]

    setSize (600, 400);


    //[Constructor] You can add your own custom stuff here..
    soundButton1->setCoordLabel(coordLabel.get());
    soundButton2->setCoordLabel(coordLabel.get());
    listenerButton->setCoordLabel(coordLabel.get());
    soundButton1->setSound(new MemoryInputStream(BinaryData::kick_ogg, BinaryData::kick_oggSize, false));
    soundButton2->setSound(new MemoryInputStream(BinaryData::g_ogg, BinaryData::g_oggSize, false));
    listenerButton->setListener(true);
    volumeSlider->setValue(0.5f);
    //[/Constructor]
}

basic3D::~basic3D()
{
    //[Destructor_pre]. You can add your own custom destruction code here..
    //[/Destructor_pre]

    labelTitle = nullptr;
    label = nullptr;
    playButton = nullptr;
    volumeSlider = nullptr;
    label2 = nullptr;
    listenerButton = nullptr;
    coordLabel = nullptr;
    soundButton1 = nullptr;
    soundButton2 = nullptr;


    //[Destructor]. You can add your own custom destruction code here..
    //[/Destructor]
}

//==============================================================================
void basic3D::paint (Graphics& g)
{
    //[UserPrePaint] Add your own custom painting code here..
    //[/UserPrePaint]

    g.fillAll (Colours::black);

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

void basic3D::resized()
{
    labelTitle->setBounds (proportionOfWidth (0.1576f), proportionOfHeight (0.0323f), proportionOfWidth (0.6904f), proportionOfHeight (0.0952f));
    label->setBounds (proportionOfWidth (0.0525f), proportionOfHeight (0.1276f), proportionOfWidth (0.9156f), proportionOfHeight (0.1225f));
    playButton->setBounds (proportionOfWidth (0.0450f), proportionOfHeight (0.2857f), proportionOfWidth (0.1951f), proportionOfHeight (0.0408f));
    volumeSlider->setBounds (proportionOfWidth (0.5554f), proportionOfHeight (0.2857f), proportionOfWidth (0.4053f), proportionOfHeight (0.0408f));
    label2->setBounds (proportionOfWidth (0.4203f), proportionOfHeight (0.2857f), proportionOfWidth (0.1051f), proportionOfHeight (0.0408f));
    listenerButton->setBounds (proportionOfWidth (0.4653f), proportionOfHeight (0.6122f), proportionOfWidth (0.0450f), proportionOfHeight (0.0816f));
    coordLabel->setBounds (proportionOfWidth (0.0600f), proportionOfHeight (0.3674f), proportionOfWidth (0.2814f), proportionOfHeight (0.0408f));
    soundButton1->setBounds (proportionOfWidth (0.2402f), proportionOfHeight (0.5170f), proportionOfWidth (0.0882f), proportionOfHeight (0.0816f));
    soundButton2->setBounds (proportionOfWidth (0.6304f), proportionOfHeight (0.5170f), proportionOfWidth (0.0882f), proportionOfHeight (0.0816f));
    //[UserResized] Add your own custom resize handling here..
    //[/UserResized]
}

void basic3D::buttonClicked (Button* buttonThatWasClicked)
{
    //[UserbuttonClicked_Pre]
    //[/UserbuttonClicked_Pre]

    if (buttonThatWasClicked == playButton)
    {
        //[UserButtonCode_playButton] -- add your button handler code here..
      soundButton1->play(playButton->getToggleState());
      soundButton2->play(playButton->getToggleState());
        //[/UserButtonCode_playButton]
    }

    //[UserbuttonClicked_Post]
    //[/UserbuttonClicked_Post]
}

void basic3D::sliderValueChanged (Slider* sliderThatWasMoved)
{
    //[UsersliderValueChanged_Pre]
    //[/UsersliderValueChanged_Pre]

    if (sliderThatWasMoved == volumeSlider)
    {
        //[UserSliderCode_volumeSlider] -- add your slider handling code here..
        YSE::ChannelMaster().setVolume(static_cast<float>(volumeSlider->getValue()));
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

<JUCER_COMPONENT documentType="Component" className="basic3D" componentName="basic3D"
                 parentClasses="public Component, public DragAndDropContainer"
                 constructorParams="" variableInitialisers="" snapPixels="8" snapActive="1"
                 snapShown="1" overlayOpacity="0.330" fixedSize="0" initialWidth="600"
                 initialHeight="400">
  <BACKGROUND backgroundColour="ff000000">
    <RECT pos="2.251% 2.041% 95.31% 22.449%" fill="solid: ff808080" hasStroke="0"/>
    <RECT pos="2.251% 27.211% 95.31% 70.748%" fill="solid: ffcccccc" hasStroke="0"/>
    <ROUNDRECT pos="5.253% 34.694% 89.306% 60.544%" cornerSize="10" fill="solid: ff404040"
               hasStroke="1" stroke="5, mitered, butt" strokeColour="solid: ff2a68a5"/>
  </BACKGROUND>
  <LABEL name="labelTitle" id="e0b01ad84f543f79" memberName="labelTitle"
         virtualName="" explicitFocusOrder="0" pos="15.76% 3.231% 69.043% 9.524%"
         bkgCol="ff6495ed" edTextCol="ff000000" edBkgCol="0" labelText="Basic 3D example"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="40" bold="1" italic="0" justification="36"/>
  <LABEL name="new label" id="2b173b509e785c8e" memberName="label" virtualName=""
         explicitFocusOrder="0" pos="5.253% 12.755% 91.557% 12.245%" edTextCol="ff000000"
         edBkgCol="0" labelText="This example has 2 sound sources and a listener. These can be moved around to simulate a 3D experience. A doppler effect is active by default.&#10;"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="0" italic="0" justification="36"/>
  <TOGGLEBUTTON name="new toggle button" id="e2ff1e43222f9439" memberName="playButton"
                virtualName="" explicitFocusOrder="0" pos="4.503% 28.571% 19.512% 4.082%"
                tooltip="Toggles both sounds on or off." buttonText="Play / Pause"
                connectedEdges="0" needsCallback="1" radioGroupId="0" state="0"/>
  <SLIDER name="new slider" id="d7af7d638b2603ff" memberName="volumeSlider"
          virtualName="" explicitFocusOrder="0" pos="55.535% 28.571% 40.525% 4.082%"
          tooltip="The volume of the main mix." min="0" max="1" int="0"
          style="LinearHorizontal" textBoxPos="NoTextBox" textBoxEditable="1"
          textBoxWidth="80" textBoxHeight="20" skewFactor="1"/>
  <LABEL name="new label" id="6da4ee40c181dadf" memberName="label2" virtualName=""
         explicitFocusOrder="0" pos="42.026% 28.571% 10.507% 4.082%" edTextCol="ff000000"
         edBkgCol="0" labelText="Volume" editableSingleClick="0" editableDoubleClick="0"
         focusDiscardsChanges="0" fontname="Default font" fontsize="15"
         bold="1" italic="0" justification="33"/>
  <IMAGEBUTTON name="new button" id="4582e3741e7758e0" memberName="listenerButton"
               virtualName="draggedComponent" explicitFocusOrder="0" pos="46.529% 61.224% 4.503% 8.163%"
               tooltip="the listener" buttonText="new button" connectedEdges="0"
               needsCallback="0" radioGroupId="0" keepProportions="1" resourceNormal="walk_png"
               opacityNormal="1" colourNormal="0" resourceOver="" opacityOver="1"
               colourOver="0" resourceDown="" opacityDown="1" colourDown="0"/>
  <LABEL name="new label" id="9125a784bf60625" memberName="coordLabel"
         virtualName="" explicitFocusOrder="0" pos="6.004% 36.735% 28.143% 4.082%"
         tooltip="virtual position of the last moved item." textCol="ffffffff"
         edTextCol="ff000000" edBkgCol="0" labelText="x: 0 z: 0" editableSingleClick="0"
         editableDoubleClick="0" focusDiscardsChanges="0" fontname="Default font"
         fontsize="15" bold="0" italic="0" justification="33"/>
  <IMAGEBUTTON name="new button" id="d692b330a206a416" memberName="soundButton1"
               virtualName="draggedComponent" explicitFocusOrder="0" pos="24.015% 51.701% 8.818% 8.163%"
               tooltip="first sound" buttonText="new button" connectedEdges="0"
               needsCallback="0" radioGroupId="0" keepProportions="1" resourceNormal="sound_png"
               opacityNormal="1" colourNormal="0" resourceOver="" opacityOver="1"
               colourOver="0" resourceDown="" opacityDown="1" colourDown="0"/>
  <IMAGEBUTTON name="new button" id="93525454cd347f1d" memberName="soundButton2"
               virtualName="draggedComponent" explicitFocusOrder="0" pos="63.039% 51.701% 8.818% 8.163%"
               tooltip="second sound" buttonText="new button" connectedEdges="0"
               needsCallback="0" radioGroupId="0" keepProportions="1" resourceNormal="sound_png"
               opacityNormal="1" colourNormal="0" resourceOver="" opacityOver="1"
               colourOver="0" resourceDown="" opacityDown="1" colourDown="0"/>
</JUCER_COMPONENT>

END_JUCER_METADATA
*/
#endif

//==============================================================================
// Binary resources - be careful not to edit any of these sections!

// JUCER_RESOURCE: walk_png, 1789, "../resources/walk.png"
static const unsigned char resource_basic3D_walk_png[] = { 137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,50,0,0,0,79,8,6,0,0,0,169,131,150,174,0,0,0,6,98,75,71,68,0,255,0,255,0,255,160,189,167,147,
0,0,0,9,112,72,89,115,0,0,23,18,0,0,23,18,1,103,159,210,82,0,0,0,7,116,73,77,69,7,222,2,3,18,49,58,147,102,130,111,0,0,6,138,73,68,65,84,104,222,213,155,91,136,22,101,24,199,255,239,238,183,235,30,60,
133,133,26,158,74,215,218,96,179,172,76,219,36,52,146,50,35,237,36,94,72,173,93,36,68,33,84,68,23,17,20,89,148,116,101,70,96,40,86,150,93,44,149,129,21,149,150,129,152,145,164,157,176,50,59,80,230,110,
102,109,185,237,241,215,133,239,23,195,48,243,206,124,51,239,183,223,246,220,236,126,51,207,60,239,252,231,125,206,243,140,84,1,2,138,127,11,192,85,192,91,192,223,192,0,112,16,184,7,24,173,255,3,1,99,
129,77,196,211,231,192,5,65,224,195,13,128,128,90,224,9,146,233,32,48,106,88,2,177,96,166,1,189,164,163,53,105,100,86,85,8,203,149,146,106,82,242,174,26,206,64,154,125,243,86,10,200,95,190,121,43,5,100,
87,9,188,239,14,87,67,23,112,58,176,63,165,177,95,50,220,227,200,18,224,68,2,136,39,128,234,97,235,126,3,96,22,3,123,35,0,252,6,220,15,52,164,149,101,202,165,62,198,152,168,227,163,36,245,27,99,186,3,
199,78,147,212,36,169,85,82,189,164,207,36,125,36,169,211,24,211,31,39,171,108,121,83,200,6,10,192,72,96,25,240,50,240,75,232,137,15,0,31,0,203,129,250,36,153,67,13,160,6,104,2,174,3,54,3,93,41,141,249,
27,107,47,133,138,230,85,192,116,96,53,176,21,248,154,108,52,8,60,12,212,14,133,203,12,254,46,0,183,2,187,172,218,252,131,31,186,27,168,242,186,43,193,155,183,194,171,129,249,192,75,86,199,203,69,147,
189,170,88,0,196,20,224,65,224,24,67,67,91,178,0,49,46,247,9,204,146,180,69,210,249,67,108,122,13,65,23,157,57,215,178,32,26,37,61,95,1,16,146,180,184,212,11,92,73,227,109,146,206,169,96,189,82,146,122,
21,28,231,90,37,197,185,195,191,37,157,148,212,45,105,208,242,157,233,17,200,37,69,205,200,5,196,166,18,227,34,78,245,75,90,43,233,128,164,78,73,199,237,177,133,146,214,123,4,50,185,212,11,226,118,164,
94,82,99,196,241,103,140,49,15,69,0,191,199,179,106,213,2,83,141,49,223,231,181,145,56,32,91,35,64,20,36,173,240,12,164,166,104,159,105,237,196,5,100,100,232,88,143,164,159,35,4,47,148,212,224,25,72,157,
164,217,62,188,86,212,142,252,36,169,175,104,128,1,64,119,228,184,225,189,146,250,98,84,126,46,80,157,214,224,171,28,79,36,12,164,83,82,111,48,214,88,90,146,3,200,115,146,118,196,156,155,35,169,197,199,
142,140,140,0,210,23,178,143,107,28,46,58,137,6,173,247,123,49,230,252,68,73,183,218,242,32,23,144,170,56,32,1,193,75,115,236,198,33,73,29,146,62,112,240,172,145,116,93,132,58,39,3,177,204,99,35,120,255,
3,18,80,171,214,28,64,190,149,116,204,24,115,84,210,17,7,223,54,160,45,144,58,165,222,145,42,73,167,71,28,239,48,198,244,7,0,207,138,9,154,105,233,176,49,166,216,124,91,151,16,235,54,0,143,3,38,206,248,
171,98,50,226,40,32,191,135,182,247,2,73,99,50,130,232,149,244,85,224,247,43,146,190,75,112,199,247,75,218,8,140,8,23,122,165,236,72,159,164,63,66,106,213,98,109,41,11,117,75,58,28,240,128,29,146,30,176,
177,202,69,171,36,189,46,105,124,216,110,210,2,57,89,4,18,200,197,102,230,80,171,30,73,225,244,99,155,164,123,37,117,185,234,61,73,139,36,237,4,90,131,15,54,19,16,107,27,103,231,0,210,43,233,104,184,144,
51,198,172,151,116,133,164,237,9,133,96,179,164,118,224,230,88,111,6,52,0,7,66,229,231,183,192,121,1,158,217,246,157,95,86,58,236,106,47,217,216,177,28,56,158,66,214,234,184,29,233,151,180,219,214,28,
93,58,213,214,63,104,221,101,113,193,241,57,243,171,254,168,170,52,240,127,159,49,102,155,85,223,246,4,117,123,6,120,36,174,241,80,7,44,0,150,2,55,216,182,102,177,179,82,101,251,178,121,104,127,82,128,
11,237,208,181,192,59,142,158,24,137,29,196,136,174,74,53,176,37,39,144,143,75,237,102,2,163,129,219,108,131,59,76,237,89,218,68,5,224,163,156,64,62,205,210,242,177,26,209,8,108,12,200,250,29,168,203,
34,168,54,197,123,141,36,250,58,111,191,25,104,5,158,5,166,199,246,181,18,132,141,145,116,34,103,225,116,68,210,116,99,204,160,175,102,98,150,119,136,51,124,212,228,146,38,248,42,39,141,49,153,128,156,
235,169,148,157,234,179,54,206,2,100,166,135,117,235,139,15,196,87,179,186,146,64,90,74,109,194,249,6,50,195,147,54,180,20,3,237,144,2,9,168,192,76,79,107,95,40,105,218,144,3,9,168,128,175,129,176,113,
146,230,251,122,67,85,146,106,1,103,121,110,196,221,37,169,222,135,157,148,106,35,205,158,129,204,144,116,163,15,239,85,42,144,121,41,249,254,146,244,161,45,105,147,232,113,31,222,171,84,32,105,219,63,
7,37,45,144,180,47,5,239,68,96,145,207,152,146,38,235,61,146,34,33,28,0,46,183,215,92,4,156,76,113,205,107,26,42,2,102,1,191,166,184,169,13,161,250,229,190,20,215,252,226,51,166,36,21,84,43,83,212,233,
63,21,103,75,66,133,216,238,132,235,186,128,165,101,181,145,128,17,206,75,81,167,223,46,169,59,56,209,99,140,25,176,110,214,69,141,146,230,150,221,216,129,73,146,46,77,96,219,36,233,125,219,214,9,239,
232,33,219,183,146,163,205,211,12,212,148,85,181,236,196,143,139,126,4,154,18,106,254,21,9,51,43,159,0,83,202,9,162,0,236,72,0,210,150,162,121,49,9,248,210,33,227,87,224,226,114,26,249,101,9,32,182,167,
141,1,64,123,130,172,171,189,218,72,96,22,165,32,105,179,227,250,239,37,221,82,66,230,188,67,210,128,131,117,18,96,188,0,9,205,16,174,211,169,121,195,184,70,116,155,49,166,59,105,238,48,112,238,61,157,
122,229,22,71,103,75,170,246,173,82,55,37,140,243,173,205,146,130,199,52,216,138,244,2,48,194,39,136,38,224,11,199,130,251,128,177,25,101,191,225,144,251,94,212,176,102,73,170,21,176,139,58,73,143,58,
82,246,46,73,119,27,99,78,148,178,27,1,245,218,227,96,27,175,188,99,238,129,39,246,88,130,103,89,149,39,83,5,174,118,200,62,106,95,34,229,6,209,150,0,226,201,164,120,145,98,173,137,14,249,255,216,78,102,
238,236,246,15,199,34,239,219,239,162,124,216,162,139,178,175,97,191,30,216,227,16,254,3,48,219,163,51,249,206,177,214,105,121,22,120,218,33,184,31,184,222,115,214,240,182,99,189,113,89,133,174,76,216,
234,101,121,237,34,98,205,167,28,235,77,200,42,212,21,244,238,44,71,45,157,224,84,206,200,42,116,95,140,192,103,203,245,17,10,48,199,187,177,219,33,253,87,67,194,118,102,214,213,116,165,193,84,199,88,
250,216,60,194,27,128,53,64,135,173,175,39,151,67,165,2,64,38,216,207,45,194,212,147,57,142,68,13,168,148,187,199,4,140,1,222,140,0,210,153,43,178,87,130,128,69,246,123,147,63,109,239,171,7,88,151,181,
110,255,23,183,38,46,94,200,183,140,193,0,0,0,0,73,69,78,68,174,66,96,130,0,0};

const char* basic3D::walk_png = (const char*) resource_basic3D_walk_png;
const int basic3D::walk_pngSize = 1789;

// JUCER_RESOURCE: sound_png, 2840, "../resources/sound.png"
static const unsigned char resource_basic3D_sound_png[] = { 137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,100,0,0,0,100,8,6,0,0,0,112,226,149,84,0,0,0,6,98,75,71,68,0,255,0,255,0,255,160,189,167,
147,0,0,0,9,112,72,89,115,0,0,13,215,0,0,13,215,1,66,40,155,120,0,0,0,7,116,73,77,69,7,222,2,3,19,15,13,107,220,86,42,0,0,10,165,73,68,65,84,120,218,237,93,123,140,92,85,25,255,125,247,206,62,74,187,179,
247,49,179,172,150,150,82,33,136,1,84,90,145,242,48,109,65,144,52,128,162,104,1,13,104,40,149,6,5,26,77,164,4,91,94,162,38,133,128,136,161,24,164,80,173,4,65,4,202,203,4,33,216,66,43,32,8,136,212,148,
242,90,90,167,115,239,185,211,90,108,103,230,222,207,63,122,55,93,102,207,57,187,91,118,59,143,158,95,178,255,156,239,188,230,251,221,243,61,206,61,231,46,96,96,96,96,96,96,96,96,96,96,96,96,96,96,96,
96,96,96,96,96,96,96,96,48,202,160,86,249,33,97,24,158,0,224,26,0,71,2,120,157,136,174,112,93,247,169,177,30,55,138,162,169,73,146,92,4,96,130,101,89,247,58,142,243,228,62,79,136,16,226,82,102,94,10,192,
26,80,188,147,153,103,250,190,255,220,88,141,27,4,193,201,68,244,48,128,182,1,197,183,123,158,119,225,158,246,105,53,51,17,204,220,22,4,193,29,204,124,163,228,183,116,16,209,101,99,56,54,17,209,157,53,
100,0,192,188,48,12,23,236,115,132,108,222,188,121,188,16,226,62,34,250,182,166,218,225,99,184,42,39,1,248,152,66,188,52,12,195,35,246,25,66,194,48,156,220,222,222,254,28,128,211,180,246,152,40,51,86,
115,112,93,247,61,0,239,42,196,157,0,238,223,178,101,75,87,203,19,18,134,225,225,0,158,25,203,167,127,88,206,151,40,1,112,143,166,202,193,182,109,255,164,165,9,9,195,240,52,0,207,1,152,220,8,243,41,151,
203,75,0,252,75,83,101,65,16,4,199,182,36,33,97,24,94,12,224,143,0,198,55,202,156,122,123,123,183,39,73,114,30,128,138,70,191,55,51,179,221,50,132,48,179,21,4,193,77,0,126,1,192,110,180,249,229,114,185,
117,0,174,212,152,182,105,66,136,139,90,34,15,233,235,235,219,175,179,179,115,5,17,125,101,15,237,252,122,215,117,15,253,8,121,198,23,45,203,186,150,153,15,97,230,23,153,249,138,92,46,183,86,242,208,216,
81,20,61,203,204,159,83,45,112,0,83,61,207,43,53,237,10,41,149,74,222,184,113,227,30,221,83,50,70,33,233,155,65,68,171,152,249,104,0,46,17,157,104,89,214,83,197,98,241,104,9,241,49,128,249,0,170,138,238,
60,34,186,180,105,77,86,177,88,60,44,142,227,231,1,124,161,142,81,212,143,36,73,95,167,101,89,191,151,133,179,174,235,254,29,192,82,141,233,253,193,182,109,219,122,154,142,16,33,196,76,203,178,86,3,56,
168,206,83,81,69,114,7,217,182,125,157,76,16,199,241,117,0,182,40,218,77,168,84,42,223,111,42,66,132,16,223,98,230,199,1,184,13,48,157,71,52,178,139,163,40,154,86,91,152,207,231,183,49,243,207,53,237,
190,87,40,20,38,52,60,33,204,76,65,16,44,97,230,229,0,218,27,97,78,113,28,47,7,176,67,101,209,146,36,185,153,153,7,5,69,59,118,236,184,21,192,102,69,187,172,109,219,103,55,52,33,204,220,30,134,225,111,
136,104,113,35,69,125,249,124,126,61,128,197,154,42,199,10,33,206,173,45,156,56,113,226,7,68,244,83,77,187,11,27,150,16,33,132,19,134,225,163,68,116,94,35,6,23,174,235,46,37,162,117,154,42,87,49,243,160,
253,178,106,181,122,7,128,173,138,96,97,122,20,69,71,53,28,33,66,136,41,204,188,154,136,102,55,106,232,77,68,113,28,199,23,3,72,20,85,166,10,33,230,202,124,9,128,21,170,126,147,36,185,160,161,8,137,162,
104,58,51,63,11,224,83,117,220,138,153,44,132,88,28,4,193,53,197,98,241,48,77,38,254,55,0,183,107,186,90,172,216,26,185,77,67,244,153,204,108,141,56,83,15,130,224,0,219,182,247,31,101,103,121,48,17,45,
3,144,221,11,79,184,52,83,79,55,41,255,48,32,128,72,136,104,161,235,186,55,41,242,162,137,150,101,109,0,208,161,240,131,167,248,190,255,132,100,156,213,0,142,85,180,57,206,247,253,53,181,229,25,5,17,147,
136,232,118,0,167,36,73,50,218,74,170,123,16,33,132,184,187,38,154,179,152,121,105,24,134,79,122,158,247,138,100,149,244,133,97,184,92,229,144,137,232,59,0,158,144,136,86,170,8,177,44,235,76,0,107,134,
52,89,204,108,19,209,3,0,78,65,11,162,84,42,29,9,160,91,34,178,1,92,175,180,237,150,245,51,205,214,200,25,165,82,201,147,180,89,165,241,35,167,15,203,135,68,81,116,36,128,163,208,186,216,160,81,236,156,
98,177,120,146,76,224,56,206,155,0,30,83,180,235,140,227,248,44,73,155,141,0,254,161,88,85,135,8,33,14,28,146,144,36,73,62,209,194,100,192,113,28,129,93,47,185,84,43,225,106,141,185,91,174,233,122,142,
162,252,33,205,42,249,252,112,162,44,66,235,99,145,70,54,35,138,162,233,50,129,231,121,15,1,136,20,237,102,111,220,184,177,83,66,226,211,186,177,26,114,235,100,111,195,243,188,103,0,60,174,137,4,191,169,
48,51,59,53,237,198,103,179,217,153,146,54,235,84,121,12,17,29,99,8,217,109,154,22,105,34,193,115,152,185,77,33,123,84,211,237,113,18,242,75,0,214,43,234,127,166,54,211,223,103,9,113,28,231,69,102,126,
65,33,206,11,33,78,150,9,170,213,234,159,53,68,78,87,248,30,149,207,234,44,149,74,147,12,33,187,87,137,206,73,207,148,50,149,207,191,15,224,61,69,155,233,10,162,254,169,25,103,170,33,164,63,241,176,237,
149,80,239,83,29,163,137,182,84,27,142,185,66,161,208,43,169,191,81,227,175,14,50,132,164,200,102,179,69,0,175,169,158,118,102,86,189,155,121,69,67,242,20,73,217,70,141,153,51,132,72,162,32,149,125,63,
66,97,234,134,173,224,180,254,91,154,213,54,209,16,242,225,228,108,173,198,156,76,81,136,222,214,116,121,64,109,65,87,87,87,168,50,141,150,101,77,48,132,124,24,239,106,100,190,130,168,255,104,158,248,
172,100,213,48,128,237,138,250,134,144,26,133,4,26,243,227,43,158,234,237,154,200,77,117,212,85,213,102,188,33,100,0,218,218,218,132,46,169,151,21,102,50,153,237,26,130,13,33,31,5,113,28,151,53,202,109,
87,248,157,68,179,170,44,69,121,172,104,98,50,245,225,152,165,212,252,4,138,108,125,63,77,144,240,193,112,124,133,106,229,24,66,136,188,145,250,23,141,89,2,17,13,203,52,13,232,203,16,82,243,68,235,206,
219,134,10,165,235,78,86,142,136,144,90,2,205,10,33,154,166,145,109,82,148,79,209,180,25,180,207,213,215,215,183,31,20,231,23,140,201,26,108,50,102,168,252,125,181,90,125,65,161,116,221,149,186,119,106,
11,58,58,58,38,107,8,220,108,8,217,77,70,7,128,207,42,196,111,164,7,222,100,74,252,164,70,193,111,75,130,131,169,154,57,188,105,8,73,17,69,209,28,0,227,20,138,90,171,81,226,116,133,104,71,119,119,247,
59,146,114,221,213,138,183,12,33,187,21,171,59,83,188,90,86,152,250,3,213,137,203,151,136,168,42,89,53,83,13,33,67,32,189,205,116,170,66,92,110,107,107,123,64,38,232,236,236,60,94,227,160,159,87,16,175,
186,123,200,213,106,213,16,2,0,229,114,249,114,12,190,178,214,255,68,63,152,205,102,85,123,92,167,106,252,199,58,69,182,175,34,228,245,158,158,158,255,238,243,132,20,139,197,137,68,52,95,83,101,133,70,
233,42,66,184,82,169,12,122,223,30,4,193,167,177,235,83,27,178,190,6,189,107,183,36,17,65,165,213,9,177,44,235,10,149,51,7,208,231,56,206,35,138,32,96,26,128,67,21,102,233,133,158,158,158,65,55,167,108,
219,30,209,171,96,217,217,222,23,91,220,145,91,0,190,166,145,47,33,162,138,34,171,63,95,211,245,42,69,127,115,52,171,109,232,21,226,121,222,59,0,126,219,194,230,106,127,0,121,133,120,131,231,121,210,147,
40,233,101,205,115,52,68,222,35,49,87,89,0,179,84,219,50,142,227,188,90,91,152,81,56,188,249,29,29,29,235,153,249,108,140,254,37,204,3,81,199,79,100,228,243,249,77,97,24,190,15,224,227,18,241,98,213,234,
176,109,123,30,20,239,71,0,172,205,229,114,175,75,86,192,28,149,254,152,249,33,217,150,188,148,144,222,222,222,237,0,174,78,255,70,59,25,155,157,36,201,125,0,156,58,46,148,249,0,254,52,208,66,16,209,125,
142,227,172,84,40,175,93,8,177,80,211,223,157,138,242,51,52,109,164,126,106,175,71,89,142,227,60,201,204,199,215,38,68,123,19,158,231,61,140,93,199,56,111,6,176,140,153,231,58,142,243,245,244,27,88,131,
32,132,152,7,201,225,133,126,203,84,46,151,239,174,45,220,186,117,107,14,192,151,85,81,55,20,87,27,50,245,80,136,239,251,175,21,10,133,25,109,109,109,15,106,146,166,177,38,229,21,0,151,12,85,175,84,42,
121,113,28,95,165,241,29,183,166,22,229,67,168,84,42,231,19,81,135,162,217,95,124,223,223,218,16,43,164,31,61,61,61,155,119,238,220,57,43,53,29,13,139,56,142,175,134,226,244,9,128,255,181,183,183,223,
34,139,228,136,104,129,134,196,95,43,67,242,122,254,216,222,222,222,237,174,235,126,53,53,29,13,7,33,196,44,0,186,111,93,221,216,213,213,85,24,20,62,133,225,201,80,108,40,50,115,193,243,188,7,27,146,144,
212,153,198,190,239,95,194,204,186,207,27,237,117,20,10,133,9,204,124,135,70,71,239,87,171,213,235,21,191,233,199,154,223,123,39,17,149,27,150,144,1,126,101,25,51,159,6,96,91,35,204,39,147,201,156,14,
96,138,70,177,139,106,247,161,210,220,227,75,144,220,140,234,207,45,45,203,90,166,221,69,104,36,19,225,251,254,99,68,116,2,212,199,253,247,38,116,33,235,26,199,113,86,72,204,17,233,238,40,2,184,215,113,
156,13,77,67,8,0,184,174,251,114,146,36,199,0,120,169,206,83,9,21,229,219,136,232,92,89,82,23,134,225,60,77,212,24,219,182,125,229,80,131,54,228,110,111,46,151,235,203,100,50,39,1,248,107,189,230,192,
204,119,165,249,66,45,46,119,93,247,45,89,120,76,68,215,106,186,188,191,187,187,251,223,77,73,8,0,100,179,217,192,117,221,89,208,124,51,100,140,205,231,179,68,116,22,51,247,43,113,19,128,5,158,231,253,
82,17,30,95,7,245,30,89,130,93,255,185,97,232,32,7,77,0,33,196,37,204,124,195,72,31,160,143,250,85,210,97,219,182,48,60,30,192,211,154,249,221,230,121,222,119,135,211,87,83,188,160,114,93,247,38,102,158,
11,245,23,222,234,134,244,147,26,191,211,232,178,104,219,246,162,225,246,215,52,111,12,125,223,191,151,153,79,132,250,35,147,245,240,51,20,199,241,10,0,147,52,171,116,97,119,119,119,216,114,132,164,164,
172,1,48,27,146,195,104,117,50,165,223,128,230,29,187,42,60,110,25,66,0,192,243,188,87,51,153,204,81,216,245,31,18,234,13,221,49,162,144,153,231,166,183,167,90,151,144,254,8,172,92,46,159,10,224,225,33,
76,74,60,134,230,202,134,226,46,123,106,170,46,243,125,255,221,145,246,219,180,167,78,210,141,201,211,153,249,42,77,181,151,199,106,252,52,49,124,67,33,94,233,186,238,93,123,210,111,83,31,3,34,34,246,
125,127,9,51,47,196,224,91,174,21,0,183,140,177,83,255,97,237,184,68,180,30,250,29,226,230,207,67,134,153,11,156,192,204,139,211,123,226,155,0,92,239,121,222,170,177,30,55,253,135,45,23,16,209,4,0,47,
219,182,253,171,145,68,85,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,163,130,255,3,210,69,29,176,172,236,245,166,0,0,0,0,73,69,78,68,174,66,96,130,0,0};

const char* basic3D::sound_png = (const char*) resource_basic3D_sound_png;
const int basic3D::sound_pngSize = 2840;


//[EndFile] You can add extra defines here...
//[/EndFile]
