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

#ifndef __JUCE_HEADER_8D88EEA208686E70__
#define __JUCE_HEADER_8D88EEA208686E70__

//[Headers]     -- You can add your own extra header files here --
#include "JuceHeader.h"
#include "parts/ChannelTreeItem.h"
//[/Headers]



//==============================================================================
/**
                                                                    //[Comments]
    An auto-generated component, created by the Introjucer.

    Describe your class and how it works here!
                                                                    //[/Comments]
*/
class movingChannels  : public Component,
                        public DragAndDropContainer,
                        public ButtonListener
{
public:
    //==============================================================================
    movingChannels ();
    ~movingChannels();

    //==============================================================================
    //[UserMethods]     -- You can add your own custom methods in this section.
    static ValueTree createRootValueTree();
    static ValueTree createTree(const String& desc, ITEM_TYPE type, ValueTree * parent = NULL);
    void deleteSelectedItems();
    //[/UserMethods]

    void paint (Graphics& g);
    void resized();
    void buttonClicked (Button* buttonThatWasClicked);
    bool keyPressed (const KeyPress& key);

    // Binary resources:
    static const char* bluechannel_png;
    static const int bluechannel_pngSize;
    static const char* bluespeaker_png;
    static const int bluespeaker_pngSize;


private:
    //[UserVariables]   -- You can add your own custom variables in this section.
    ScopedPointer<channelTreeItem> rootItem;
    //[/UserVariables]

    //==============================================================================
    ScopedPointer<Label> labelTitle;
    ScopedPointer<Label> label;
    ScopedPointer<TreeView> treeView;
    ScopedPointer<TextButton> buttonAddSound;
    ScopedPointer<TextButton> buttonAddChannel;


    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (movingChannels)
};

//[EndFile] You can add extra defines here...

//[/EndFile]

#endif   // __JUCE_HEADER_8D88EEA208686E70__
