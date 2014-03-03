/*
  ==============================================================================

    ChannelTreeItem.h
    Created: 25 Feb 2014 8:10:12pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELTREEITEM_H_INCLUDED
#define CHANNELTREEITEM_H_INCLUDED

#include "../JuceHeader.h"

enum ITEM_TYPE {
  SOUND,
  USERCHANNEL,
  MAINCHANNEL,
  VOICECHANNEL,
  FXCHANNEL,
  AMBIENTCHANNEL,
  GUICHANNEL,
  MUSICCHANNEL,
};

class channelTreeItem : public TreeViewItem, private ValueTree::Listener {
public:
  channelTreeItem(const ValueTree& v);
  void set(ITEM_TYPE type, const String & file = "");
  String getUniqueName() const override;
  bool mightContainSubItems() override;
  void paintItem(Graphics & g, int width, int height) override;
  void itemOpennessChanged(bool isNowOpen) override;
  var getDragSourceDescription() override;
  bool isInterestedInDragSource(const DragAndDropTarget::SourceDetails & dragSourceDetails) override;
  void itemDropped(const DragAndDropTarget::SourceDetails&, int insertIndex) override;
  static void moveItems(TreeView& treeView, const Array<ValueTree>& items, ValueTree newParent, int insertIndex);
  static Array<ValueTree> getSelectedTreeViewItems(TreeView& treeView);

private:
  ValueTree tree;
  ITEM_TYPE type;
  String file;
  ScopedPointer<ImageComponent> img;


  void refreshSubItems();
  void valueTreePropertyChanged(ValueTree&, const Identifier&) override;
  void valueTreeChildAdded(ValueTree& parentTree, ValueTree&) override    { treeChildrenChanged(parentTree); }
  void valueTreeChildRemoved(ValueTree& parentTree, ValueTree&) override  { treeChildrenChanged(parentTree); }
  void valueTreeChildOrderChanged(ValueTree& parentTree) override         { treeChildrenChanged(parentTree); }
  void valueTreeParentChanged(ValueTree&) override {}

  void treeChildrenChanged(const ValueTree& parentTree);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(channelTreeItem)
};




#endif  // CHANNELTREEITEM_H_INCLUDED
