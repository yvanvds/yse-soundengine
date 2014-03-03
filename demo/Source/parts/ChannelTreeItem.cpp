/*
  ==============================================================================

    ChannelTreeItem.cpp
    Created: 25 Feb 2014 8:10:12pm
    Author:  yvan

  ==============================================================================
*/

#include "ChannelTreeItem.h"
#include "../YSEObjects.h"

// JUCER_RESOURCE: bluechannel_png, 273, "../resources/bluechannel.png"
static const unsigned char resource_bluechannel_png[] = { 137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 0, 50, 0, 0, 0, 48, 8, 6, 0, 0, 0, 83, 247, 41, 186, 0, 0, 0, 6, 98, 75, 71, 68, 0, 255, 0, 255, 0, 255, 160,
189, 167, 147, 0, 0, 0, 9, 112, 72, 89, 115, 0, 0, 11, 19, 0, 0, 11, 19, 1, 0, 154, 156, 24, 0, 0, 0, 7, 116, 73, 77, 69, 7, 222, 2, 26, 10, 25, 52, 22, 167, 235, 31, 0, 0, 0, 158, 73, 68, 65, 84, 104, 222, 237, 151, 65, 14, 192, 32, 8, 4, 165, 241, 159, 125, 96, 31,
73, 175, 158, 154, 186, 210, 162, 237, 112, 50, 65, 72, 70, 89, 68, 43, 251, 81, 190, 96, 91, 249, 136, 1, 2, 8, 32, 128, 172, 101, 85, 136, 241, 102, 109, 130, 191, 221, 163, 250, 41, 45, 64, 102, 211, 136, 139, 122, 233, 137, 239, 210, 4, 93, 235, 102, 151,
82, 226, 123, 115, 112, 35, 145, 39, 231, 217, 32, 38, 8, 252, 170, 164, 124, 84, 220, 17, 47, 251, 148, 93, 173, 6, 223, 240, 168, 152, 229, 248, 95, 15, 141, 41, 98, 126, 27, 36, 237, 32, 234, 3, 218, 72, 233, 106, 76, 191, 128, 0, 2, 8, 32, 128, 0, 2, 8, 32,
128, 0, 2, 8, 32, 9, 95, 93, 27, 244, 71, 229, 160, 180, 0, 1, 4, 144, 133, 237, 4, 21, 216, 25, 128, 2, 145, 240, 223, 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130, 0, 0 };

const char* bluechannel_png = (const char*)resource_bluechannel_png;
const int bluechannel_pngSize = 273;

// JUCER_RESOURCE: bluespeaker_png, 315, "../resources/bluespeaker.png"
static const unsigned char resource_bluespeaker_png[] = { 137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 0, 50, 0, 0, 0, 48, 8, 6, 0, 0, 0, 83, 247, 41, 186, 0, 0, 0, 6, 98, 75, 71, 68, 0, 255, 0, 255, 0, 255, 160,
189, 167, 147, 0, 0, 0, 9, 112, 72, 89, 115, 0, 0, 11, 19, 0, 0, 11, 19, 1, 0, 154, 156, 24, 0, 0, 0, 7, 116, 73, 77, 69, 7, 222, 2, 26, 10, 24, 57, 113, 13, 166, 227, 0, 0, 0, 200, 73, 68, 65, 84, 104, 222, 237, 151, 49, 14, 197, 32, 12, 67, 113, 197, 61, 123, 192,
30, 50, 127, 253, 3, 84, 42, 13, 152, 164, 206, 132, 24, 16, 15, 199, 73, 64, 57, 175, 146, 33, 142, 146, 36, 4, 34, 16, 129, 8, 68, 32, 148, 168, 155, 222, 203, 254, 214, 136, 8, 98, 209, 21, 177, 12, 30, 105, 65, 32, 146, 34, 46, 0, 76, 144, 94, 26, 225, 205,
161, 53, 58, 192, 40, 136, 57, 130, 193, 243, 149, 24, 169, 133, 25, 135, 30, 25, 32, 52, 107, 173, 126, 109, 41, 50, 168, 134, 49, 85, 145, 34, 13, 53, 10, 83, 149, 154, 193, 232, 30, 32, 104, 116, 123, 116, 246, 195, 121, 196, 34, 122, 196, 118, 129, 81,
213, 250, 58, 136, 205, 74, 187, 167, 85, 11, 78, 94, 114, 239, 55, 171, 20, 65, 227, 226, 174, 234, 236, 240, 31, 113, 1, 98, 152, 29, 51, 128, 152, 85, 235, 14, 40, 100, 249, 237, 249, 39, 108, 31, 65, 52, 143, 140, 164, 219, 178, 49, 158, 174, 142, 70,
20, 129, 8, 68, 32, 2, 161, 196, 15, 208, 73, 29, 134, 198, 84, 75, 73, 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130, 0, 0 };

const char* bluespeaker_png = (const char*)resource_bluespeaker_png;
const int bluespeaker_pngSize = 315;

channelTreeItem::channelTreeItem(const ValueTree& v) : tree(v) {
  tree.addListener(this);
  img = new ImageComponent;
}

void channelTreeItem::set(ITEM_TYPE type, const String & file) {
  this->type = type;
  
}



String channelTreeItem::getUniqueName() const {
  return tree["name"].toString();
}

bool channelTreeItem::mightContainSubItems() {
  return tree.getNumChildren() > 0;
}

void channelTreeItem::paintItem(Graphics & g, int width, int height) {

  if (static_cast<ITEM_TYPE>(tree["type"].operator int()) == SOUND) {
    type = SOUND;
    img->setImage(ImageCache::getFromMemory(bluespeaker_png, bluespeaker_pngSize));
  }
  else {
    img->setImage(ImageCache::getFromMemory(bluechannel_png, bluechannel_pngSize));
  }
  tree.setProperty("init", "no", nullptr);
  img->setTopLeftPosition(0, 0);
  img->setSize(height, height);
  img->paint(g);
  if (isSelected()) {
    g.setColour(Colours::aliceblue);
  }
  else {
    g.setColour(Colours::black);
  }
  
  g.setFont(15.f);
  g.drawText(tree["name"].toString(), 4 + height, 0, width - 4 - height, height, Justification::centredLeft, true);
}

void channelTreeItem::itemOpennessChanged(bool isNowOpen) {
  if (isNowOpen && getNumSubItems() == 0) {
    refreshSubItems();
  }
  else {
    clearSubItems();
  }
}

var channelTreeItem::getDragSourceDescription() {
  return "channel demo";
}

bool channelTreeItem::isInterestedInDragSource(const DragAndDropTarget::SourceDetails & dragSourceDetails) {
  return dragSourceDetails.description == "channel demo";
}

void channelTreeItem::itemDropped(const DragAndDropTarget::SourceDetails&, int insertIndex) 
{
  moveItems(*getOwnerView(), getSelectedTreeViewItems(*getOwnerView()), tree, insertIndex);
}

void channelTreeItem::moveItems(TreeView& treeView, const Array<ValueTree>& items, ValueTree newParent, int insertIndex)
{
  if (items.size() > 0)
  {
    ScopedPointer<XmlElement> oldOpenness(treeView.getOpennessState(false));

    for (int i = items.size(); --i >= 0;)
    {
      ValueTree& v = items.getReference(i);

      if (v.getParent().isValid() && newParent != v && !newParent.isAChildOf(v))
      {
        v.getParent().removeChild(v, nullptr);
        newParent.addChild(v, insertIndex, nullptr);
        Sound().move(v);
      }
    }

    if (oldOpenness != nullptr)
      treeView.restoreOpennessState(*oldOpenness, false);
  }
}

Array<ValueTree> channelTreeItem::getSelectedTreeViewItems(TreeView& treeView)
{
  Array<ValueTree> items;

  const int numSelected = treeView.getNumSelectedItems();

  for (int i = 0; i < numSelected; ++i)
  if (const channelTreeItem* vti = dynamic_cast<channelTreeItem*> (treeView.getSelectedItem(i)))
    items.add(vti->tree);

  return items;
}

void channelTreeItem::refreshSubItems()
{
  clearSubItems();

  for (int i = 0; i < tree.getNumChildren(); ++i)
    addSubItem(new channelTreeItem(tree.getChild(i)));
}

void channelTreeItem::valueTreePropertyChanged(ValueTree&, const Identifier&)
{

  repaintItem();

}

void channelTreeItem::treeChildrenChanged(const ValueTree& parentTree)
{
  if (parentTree == tree)
  {
    refreshSubItems();
    treeHasChanged();
    setOpen(true);
  }
}

