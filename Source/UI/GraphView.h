#pragma once

#include "Theme.h"

namespace rig
{
/** What the graph needs from the editor that owns it. */
struct GraphHost
{
    virtual ~GraphHost() = default;
    virtual Engine& getEngine() = 0;
    virtual juce::String getSelectedId() const = 0;
    virtual void select (const juce::String& nodeId) = 0;
    virtual void openNode (Node&) = 0;
    virtual void showAddMenu (Chain&, int index, juce::Component* anchor) = 0;
    virtual void showNodeMenu (Node&, juce::Component* anchor) = 0;
    virtual void showBranchMenu (Split&, int branch, juce::Component* anchor) = 0;
    virtual bool loadFileInto (Node* target, Chain* chain, int index, const juce::File&) = 0;
    virtual bool insertPlugin (const juce::String& identifier, Chain& chain, int index) = 0;
};

/** Anything laid out in a chain. */
class GraphItem : public juce::Component
{
public:
    virtual juce::Point<int> preferredSize() const = 0;
    virtual Node* getNode() const = 0;
};

class ChainView;

//==============================================================================
class BlockCard : public GraphItem,
                  public juce::DragAndDropTarget
{
public:
    BlockCard (GraphHost&, Node&);

    juce::Point<int> preferredSize() const override { return { 148, 128 }; }
    Node* getNode() const override { return &node; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    bool acceptsFile (const juce::File&) const;

private:
    juce::Rectangle<float> footswitchArea() const;
    GraphHost& host;
    Node& node;
    bool dragging = false, dropHover = false, pressedSwitch = false;
};

//==============================================================================
class AddButton : public juce::Component
{
public:
    AddButton (GraphHost&, Chain&, int index);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    GraphHost& host;
    Chain& chain;
    int index;
};

//==============================================================================
class ChainView : public juce::Component,
                  public juce::DragAndDropTarget
{
public:
    ChainView (GraphHost&, Chain&, juce::Colour wire, std::function<float()> level);

    juce::Point<int> preferredSize() const;
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    Chain& getChain() const noexcept { return chain; }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    int indexForX (int x) const;

    static constexpr int gap = 30;

private:
    GraphHost& host;
    Chain& chain;
    juce::Colour wireColour;
    std::function<float()> level;
    juce::OwnedArray<GraphItem> items;
    std::unique_ptr<AddButton> addButton;
    int dropIndex = -1;
};

//==============================================================================
class SplitView : public GraphItem
{
public:
    SplitView (GraphHost&, Split&);

    juce::Point<int> preferredSize() const override;
    Node* getNode() const override { return &split; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Hub;
    struct Pill;
    struct Merge;

    GraphHost& host;
    Split& split;
    std::unique_ptr<Hub> hub;
    std::unique_ptr<Merge> merge;
    juce::OwnedArray<Pill> pills;
    juce::OwnedArray<ChainView> rows;
    juce::TextButton addPath { "+ Path" };

    static constexpr int hubW = 112, pillW = 64, mergeW = 34, rowGap = 14, margin = 26;
};

//==============================================================================
/** The whole rig: IN terminal → main chain → OUT terminal. Lives in a Viewport. */
class GraphView : public juce::Component
{
public:
    explicit GraphView (GraphHost&);
    ~GraphView() override;

    void rebuild();
    void fitTo (juce::Rectangle<int> viewportArea);
    void resized() override;
    void paint (juce::Graphics&) override;

    void setMeters (float in, float out) { inLevel = in; outLevel = out; }

    /** Drop a file from the OS: loads into the card under the point or inserts a new block. */
    bool dropFile (const juce::File&, juce::Point<int> localPoint);

private:
    GraphHost& host;
    std::unique_ptr<ChainView> main;
    juce::Rectangle<int> inBox, outBox;
    float inLevel = 0, outLevel = 0;
    juce::Rectangle<int> viewArea;
};

} // namespace rig
