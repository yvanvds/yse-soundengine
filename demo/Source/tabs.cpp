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
#include "basic.h"
#include "basic3D.h"
#include "cpuLoad.h"
#include "movingChannels.h"
#include "reverbDemo.h"
//[/Headers]

#include "tabs.h"


//[MiscUserDefs] You can add your own user definitions and misc code here...
//[/MiscUserDefs]

//==============================================================================
tabs::tabs ()
{
    setName ("tabs");
    addAndMakeVisible (tabbedComponent = new TabbedComponent (TabbedButtonBar::TabsAtTop));
    tabbedComponent->setTabBarDepth (30);
    tabbedComponent->addTab (TRANS("Basic 2D"), Colours::lightgrey, new basicTab(), true);
    tabbedComponent->addTab (TRANS("Basic 3D"), Colours::lightgrey, new basic3D(), true);
    tabbedComponent->addTab (TRANS("CPU"), Colours::lightgrey, new cpuLoad(), true);
    tabbedComponent->addTab (TRANS("Channels"), Colours::lightgrey, new movingChannels(), true);
    tabbedComponent->addTab (TRANS("Reverb"), Colours::lightgrey, new reverbDemo(), true);
    tabbedComponent->setCurrentTabIndex (0);


    //[UserPreSize]

    //[/UserPreSize]

    setSize (600, 400);


    //[Constructor] You can add your own custom stuff here..
    //[/Constructor]
}

tabs::~tabs()
{
    //[Destructor_pre]. You can add your own custom destruction code here..
    //tabBasic = nullptr;
    tooltipWindow = nullptr;
    //[/Destructor_pre]

    tabbedComponent = nullptr;


    //[Destructor]. You can add your own custom destruction code here..
    //[/Destructor]
}

//==============================================================================
void tabs::paint (Graphics& g)
{
    //[UserPrePaint] Add your own custom painting code here..
    //[/UserPrePaint]

    g.fillAll (Colours::white);

    //[UserPaint] Add your own custom painting code here..
    //[/UserPaint]
}

void tabs::resized()
{
    tabbedComponent->setBounds (0, 0, proportionOfWidth (1.0000f), proportionOfHeight (1.0000f));
    //[UserResized] Add your own custom resize handling here..
    //[/UserResized]
}



//[MiscUserCode] You can add your own definitions of your custom methods or any other code here...
//[/MiscUserCode]


//==============================================================================
#if 0
/*  -- Introjucer information section --

    This is where the Introjucer stores the metadata that describe this GUI layout, so
    make changes in here at your peril!

BEGIN_JUCER_METADATA

<JUCER_COMPONENT documentType="Component" className="tabs" componentName="tabs"
                 parentClasses="public Component" constructorParams="" variableInitialisers=""
                 snapPixels="8" snapActive="1" snapShown="1" overlayOpacity="0.330"
                 fixedSize="0" initialWidth="600" initialHeight="400">
  <BACKGROUND backgroundColour="ffffffff"/>
  <TABBEDCOMPONENT name="new tabbed component" id="507f30caf24dea9d" memberName="tabbedComponent"
                   virtualName="" explicitFocusOrder="0" pos="0 0 100% 100%" orientation="top"
                   tabBarDepth="30" initialTab="0">
    <TAB name="Basic 2D" colour="ffd3d3d3" useJucerComp="0" contentClassName="basicTab"
         constructorParams="" jucerComponentFile=""/>
    <TAB name="Basic 3D" colour="ffd3d3d3" useJucerComp="0" contentClassName="basic3D"
         constructorParams="" jucerComponentFile=""/>
    <TAB name="CPU" colour="ffd3d3d3" useJucerComp="0" contentClassName="cpuLoad"
         constructorParams="" jucerComponentFile=""/>
    <TAB name="Channels" colour="ffd3d3d3" useJucerComp="0" contentClassName="movingChannels"
         constructorParams="" jucerComponentFile=""/>
    <TAB name="Reverb" colour="ffd3d3d3" useJucerComp="0" contentClassName="reverbDemo"
         constructorParams="" jucerComponentFile=""/>
  </TABBEDCOMPONENT>
</JUCER_COMPONENT>

END_JUCER_METADATA
*/
#endif


//[EndFile] You can add extra defines here...
//[/EndFile]
