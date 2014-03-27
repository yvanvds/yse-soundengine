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

#include "movingChannels.h"


//[MiscUserDefs] You can add your own user definitions and misc code here...
//[/MiscUserDefs]

//==============================================================================
movingChannels::movingChannels ()
{
    addAndMakeVisible (labelTitle = new Label ("labelTitle",
                                               TRANS("Channels")));
    labelTitle->setFont (Font (40.00f, Font::bold));
    labelTitle->setJustificationType (Justification::centred);
    labelTitle->setEditable (false, false, false);
    labelTitle->setColour (Label::backgroundColourId, Colours::cornflowerblue);
    labelTitle->setColour (TextEditor::textColourId, Colours::black);
    labelTitle->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (label = new Label ("new label",
                                          TRANS("Channels can be added, moved and removed on the fly. Sounds can also be moved between channels. In fact almost nothing happens in this demo. It just shows you that you can move channels and sounds around without glitches.")));
    label->setFont (Font (15.00f, Font::plain));
    label->setJustificationType (Justification::centred);
    label->setEditable (false, false, false);
    label->setColour (TextEditor::textColourId, Colours::black);
    label->setColour (TextEditor::backgroundColourId, Colour (0x00000000));

    addAndMakeVisible (treeView = new TreeView ("new treeview"));
    treeView->setDefaultOpenness (true);
    treeView->setColour (TreeView::backgroundColourId, Colours::grey);
    treeView->setColour (TreeView::linesColourId, Colour (0xff404040));

    addAndMakeVisible (buttonAddSound = new TextButton ("new button"));
    buttonAddSound->setButtonText (TRANS("add sound"));
    buttonAddSound->addListener (this);

    addAndMakeVisible (buttonAddChannel = new TextButton ("new button"));
    buttonAddChannel->setButtonText (TRANS("add channel"));
    buttonAddChannel->addListener (this);


    //[UserPreSize]
    ValueTree temp = createRootValueTree();
    treeView->setRootItem(rootItem = new channelTreeItem(temp));
    //[/UserPreSize]

    setSize (600, 400);


    //[Constructor] You can add your own custom stuff here..
    //[/Constructor]
}

movingChannels::~movingChannels()
{
    //[Destructor_pre]. You can add your own custom destruction code here..
    //[/Destructor_pre]

    labelTitle = nullptr;
    label = nullptr;
    treeView = nullptr;
    buttonAddSound = nullptr;
    buttonAddChannel = nullptr;


    //[Destructor]. You can add your own custom destruction code here..
    //[/Destructor]
}

//==============================================================================
void movingChannels::paint (Graphics& g)
{
    //[UserPrePaint] Add your own custom painting code here..
    //[/UserPrePaint]

    g.fillAll (Colours::black);

    g.setColour (Colours::grey);
    g.fillRect (proportionOfWidth (0.0225f), proportionOfHeight (0.0204f), proportionOfWidth (0.9531f), proportionOfHeight (0.2245f));

    g.setColour (Colour (0xffcccccc));
    g.fillRect (proportionOfWidth (0.0225f), proportionOfHeight (0.2721f), proportionOfWidth (0.9531f), proportionOfHeight (0.7075f));

    //[UserPaint] Add your own custom painting code here..
    //[/UserPaint]
}

void movingChannels::resized()
{
    labelTitle->setBounds (proportionOfWidth (0.1576f), proportionOfHeight (0.0510f), proportionOfWidth (0.6904f), proportionOfHeight (0.0952f));
    label->setBounds (proportionOfWidth (0.0525f), proportionOfHeight (0.1480f), proportionOfWidth (0.9156f), proportionOfHeight (0.0969f));
    treeView->setBounds (proportionOfWidth (0.0450f), proportionOfHeight (0.2993f), proportionOfWidth (0.9006f), proportionOfHeight (0.5986f));
    buttonAddSound->setBounds (proportionOfWidth (0.0450f), proportionOfHeight (0.9116f), proportionOfWidth (0.2814f), proportionOfHeight (0.0408f));
    buttonAddChannel->setBounds (proportionOfWidth (0.6604f), proportionOfHeight (0.9116f), proportionOfWidth (0.2814f), proportionOfHeight (0.0408f));
    //[UserResized] Add your own custom resize handling here..
    //[/UserResized]
}

void movingChannels::buttonClicked (Button* buttonThatWasClicked)
{
    //[UserbuttonClicked_Pre]
    //[/UserbuttonClicked_Pre]

    if (buttonThatWasClicked == buttonAddSound)
    {
        //[UserButtonCode_buttonAddSound] -- add your button handler code here..
      Array<ValueTree> selectedItems(channelTreeItem::getSelectedTreeViewItems(*treeView));
      if (selectedItems.size() > 0) {
        if (static_cast<ITEM_TYPE>(selectedItems[0]["type"].operator int()) != SOUND) {
         //createTree("sound", SOUND, &selectedItems[0]);
        }
      }
        //[/UserButtonCode_buttonAddSound]
    }
    else if (buttonThatWasClicked == buttonAddChannel)
    {
        //[UserButtonCode_buttonAddChannel] -- add your button handler code here..
      Array<ValueTree> selectedItems(channelTreeItem::getSelectedTreeViewItems(*treeView));
      if (selectedItems.size() > 0) {
        if (static_cast<ITEM_TYPE>(selectedItems[0]["type"].operator int()) != SOUND) {
          //createTree("user channel", USERCHANNEL, &selectedItems[0]);
        }
      }
        //[/UserButtonCode_buttonAddChannel]
    }

    //[UserbuttonClicked_Post]
    //[/UserbuttonClicked_Post]
}

bool movingChannels::keyPressed (const KeyPress& key)
{
    //[UserCode_keyPressed] -- Add your code here...
  if (key == KeyPress::deleteKey) {
    deleteSelectedItems();
    return true;
  }
    return false;  // Return true if your handler uses this key event, or false to allow it to be passed-on.
    //[/UserCode_keyPressed]
}



//[MiscUserCode] You can add your own definitions of your custom methods or any other code here...
ValueTree movingChannels::createTree(const String& desc, ITEM_TYPE type, ValueTree * parent) {
  ValueTree t("item");
  t.setProperty("name", desc, nullptr);
  t.setProperty("type", type, nullptr);
  if (parent != NULL) {
    parent->addChild(t, -1, nullptr);
  }
  Sound().addTree(t);
  return t;
}

ValueTree movingChannels::createRootValueTree() {
  ValueTree vt = createTree("mainMix", MAINCHANNEL);
  createTree("Voice Channel", VOICECHANNEL, &vt);
  createTree("FX Channel", FXCHANNEL, &vt);
  createTree("Ambient Channel", AMBIENTCHANNEL, &vt);
  createTree("GUI Channel", GUICHANNEL, &vt);
  createTree("Music Channel", MUSICCHANNEL, &vt);
  return vt;
}

void movingChannels::deleteSelectedItems() {
  Array<ValueTree> selectedItems(channelTreeItem::getSelectedTreeViewItems(*treeView));
  for (int i = selectedItems.size(); --i >= 0;) {
    ValueTree& v = selectedItems.getReference(i);
    if (v.getParent().isValid()) {
      v.getParent().removeChild(v, nullptr);
    }
  }
}

//[/MiscUserCode]


//==============================================================================
#if 0
/*  -- Introjucer information section --

    This is where the Introjucer stores the metadata that describe this GUI layout, so
    make changes in here at your peril!

BEGIN_JUCER_METADATA

<JUCER_COMPONENT documentType="Component" className="movingChannels" componentName=""
                 parentClasses="public Component, public DragAndDropContainer"
                 constructorParams="" variableInitialisers="" snapPixels="8" snapActive="1"
                 snapShown="1" overlayOpacity="0.330" fixedSize="0" initialWidth="600"
                 initialHeight="400">
  <METHODS>
    <METHOD name="keyPressed (const KeyPress&amp; key)"/>
  </METHODS>
  <BACKGROUND backgroundColour="ff000000">
    <RECT pos="2.251% 2.041% 95.31% 22.449%" fill="solid: ff808080" hasStroke="0"/>
    <RECT pos="2.251% 27.211% 95.31% 70.748%" fill="solid: ffcccccc" hasStroke="0"/>
  </BACKGROUND>
  <LABEL name="labelTitle" id="e0b01ad84f543f79" memberName="labelTitle"
         virtualName="" explicitFocusOrder="0" pos="15.76% 5.102% 69.043% 9.524%"
         bkgCol="ff6495ed" edTextCol="ff000000" edBkgCol="0" labelText="Channels"
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="40" bold="1" italic="0" justification="36"/>
  <LABEL name="new label" id="2b173b509e785c8e" memberName="label" virtualName=""
         explicitFocusOrder="0" pos="5.253% 14.796% 91.557% 9.694%" edTextCol="ff000000"
         edBkgCol="0" labelText="Channels can be added, moved and removed on the fly. Sounds can also be moved between channels. In fact almost nothing happens in this demo. It just shows you that you can move channels and sounds around without glitches."
         editableSingleClick="0" editableDoubleClick="0" focusDiscardsChanges="0"
         fontname="Default font" fontsize="15" bold="0" italic="0" justification="36"/>
  <TREEVIEW name="new treeview" id="ba09230bfbf01819" memberName="treeView"
            virtualName="" explicitFocusOrder="0" pos="4.503% 29.932% 90.056% 59.864%"
            backgroundColour="ff808080" linecol="ff404040" rootVisible="1"
            openByDefault="1"/>
  <TEXTBUTTON name="new button" id="e2c1d34e4e00b9dc" memberName="buttonAddSound"
              virtualName="" explicitFocusOrder="0" pos="4.503% 91.156% 28.143% 4.082%"
              buttonText="add sound" connectedEdges="0" needsCallback="1" radioGroupId="0"/>
  <TEXTBUTTON name="new button" id="3d889da0d0955c73" memberName="buttonAddChannel"
              virtualName="" explicitFocusOrder="0" pos="66.041% 91.156% 28.143% 4.082%"
              buttonText="add channel" connectedEdges="0" needsCallback="1"
              radioGroupId="0"/>
</JUCER_COMPONENT>

END_JUCER_METADATA
*/
#endif

//==============================================================================
// Binary resources - be careful not to edit any of these sections!

// JUCER_RESOURCE: bluechannel_png, 273, "../resources/bluechannel.png"
static const unsigned char resource_movingChannels_bluechannel_png[] = { 137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,50,0,0,0,48,8,6,0,0,0,83,247,41,186,0,0,0,6,98,75,71,68,0,255,0,255,0,255,160,
189,167,147,0,0,0,9,112,72,89,115,0,0,11,19,0,0,11,19,1,0,154,156,24,0,0,0,7,116,73,77,69,7,222,2,26,10,25,52,22,167,235,31,0,0,0,158,73,68,65,84,104,222,237,151,65,14,192,32,8,4,165,241,159,125,96,31,
73,175,158,154,186,210,162,237,112,50,65,72,70,89,68,43,251,81,190,96,91,249,136,1,2,8,32,128,172,101,85,136,241,102,109,130,191,221,163,250,41,45,64,102,211,136,139,122,233,137,239,210,4,93,235,102,151,
82,226,123,115,112,35,145,39,231,217,32,38,8,252,170,164,124,84,220,17,47,251,148,93,173,6,223,240,168,152,229,248,95,15,141,41,98,126,27,36,237,32,234,3,218,72,233,106,76,191,128,0,2,8,32,128,0,2,8,32,
128,0,2,8,32,9,95,93,27,244,71,229,160,180,0,1,4,144,133,237,4,21,216,25,128,2,145,240,223,0,0,0,0,73,69,78,68,174,66,96,130,0,0};

const char* movingChannels::bluechannel_png = (const char*) resource_movingChannels_bluechannel_png;
const int movingChannels::bluechannel_pngSize = 273;

// JUCER_RESOURCE: bluespeaker_png, 315, "../resources/bluespeaker.png"
static const unsigned char resource_movingChannels_bluespeaker_png[] = { 137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,50,0,0,0,48,8,6,0,0,0,83,247,41,186,0,0,0,6,98,75,71,68,0,255,0,255,0,255,160,
189,167,147,0,0,0,9,112,72,89,115,0,0,11,19,0,0,11,19,1,0,154,156,24,0,0,0,7,116,73,77,69,7,222,2,26,10,24,57,113,13,166,227,0,0,0,200,73,68,65,84,104,222,237,151,49,14,197,32,12,67,113,197,61,123,192,
30,50,127,253,3,84,42,13,152,164,206,132,24,16,15,199,73,64,57,175,146,33,142,146,36,4,34,16,129,8,68,32,148,168,155,222,203,254,214,136,8,98,209,21,177,12,30,105,65,32,146,34,46,0,76,144,94,26,225,205,
161,53,58,192,40,136,57,130,193,243,149,24,169,133,25,135,30,25,32,52,107,173,126,109,41,50,168,134,49,85,145,34,13,53,10,83,149,154,193,232,30,32,104,116,123,116,246,195,121,196,34,122,196,118,129,81,
213,250,58,136,205,74,187,167,85,11,78,94,114,239,55,171,20,65,227,226,174,234,236,240,31,113,1,98,152,29,51,128,152,85,235,14,40,100,249,237,249,39,108,31,65,52,143,140,164,219,178,49,158,174,142,70,
20,129,8,68,32,2,161,196,15,208,73,29,134,198,84,75,73,0,0,0,0,73,69,78,68,174,66,96,130,0,0};

const char* movingChannels::bluespeaker_png = (const char*) resource_movingChannels_bluespeaker_png;
const int movingChannels::bluespeaker_pngSize = 315;


//[EndFile] You can add extra defines here...
//[/EndFile]
