#pragma once

#include "Theme.h"

namespace rig
{
/** Selection ids for the IN and OUT terminals (not nodes: they open the I/O panel). */
inline const juce::String kInputId { "__input" }, kOutputId { "__output" };

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
    virtual void showReplaceMenu (Node&, juce::Component* anchor) = 0;
    /** Step to the previous/next .nam or IR file in the same folder. */
    virtual void browseFile (Node&, int delta) = 0;
    virtual void showBranchMenu (Split&, int branch, juce::Component* anchor) = 0;
    virtual bool loadFileInto (Node* target, Chain* chain, int index, const juce::File&) = 0;
    virtual bool insertPlugin (const juce::String& identifier, Chain& chain, int index) = 0;
    virtual bool insertBuiltIn (NodeType, Chain& chain, int index) = 0;

    enum RandomWhat { RandomSettings = 0, RandomMutate, RandomType };
    virtual void randomise (Node&, RandomWhat) = 0;
    virtual void showRandomMenu (Node&, juce::Component* anchor) = 0;
    virtual void showBlockPresetMenu (Node&, juce::Component* anchor) = 0;
    /** Something dropped onto the middle of a block: a file, plug-in or another block ("node:id" swaps). */
    virtual void replaceWith (Node& target, const juce::String& dragDescription) = 0;
    virtual void toggleLock (Node&) = 0;
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
                  public juce::DragAndDropTarget,
                  public juce::TooltipClient
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
    void mouseMove (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    // Drop onto the middle of a card to replace it (or swap with a dragged block);
    // near its left/right edge to insert before/after it instead.
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** -1 = insert before, 0 = replace, 1 = insert after */
    int dropZone (juce::Point<int> local) const;
    bool acceptsFile (const juce::File&) const;

private:
    juce::Rectangle<float> footswitchArea() const;
    juce::Rectangle<float> diceArea() const;
    bool hasDice() const;
    ChainView* parentChain() const;
    GraphHost& host;
    Node& node;
    bool dragging = false, pressedSwitch = false;
    juce::String dropLabel;   // shown while something hovers over the middle
};

//==============================================================================
class AddButton : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    /** compact: the small + that sits on the wire between two blocks. */
    AddButton (GraphHost&, Chain&, int index, bool compact = false);
    void paint (juce::Graphics&) override;
    bool isCompact() const noexcept { return compact; }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    GraphHost& host;
    Chain& chain;
    int index;
    bool compact;
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
    int indexOfItem (const juce::Component* c) const { for (int i = 0; i < items.size(); ++i) if (items[i] == c) return i; return -1; }
    void setDropIndex (int i) { if (i != dropIndex) { dropIndex = i; repaint(); } }
    /** Handles a drag description ("node:", "file:", "plugin:") dropped at a position in this chain. */
    void dropAt (const juce::String& description, int index);

    static constexpr int gap = 46;
    static constexpr int miniSize = 22;

private:
    GraphHost& host;
    Chain& chain;
    juce::Colour wireColour;
    std::function<float()> level;
    juce::OwnedArray<GraphItem> items;
    std::unique_ptr<AddButton> addButton;
    juce::OwnedArray<AddButton> gapButtons;   // a small + before every block
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

    struct Levels { float inL = 0, inR = 0, outL = 0, outR = 0, grDb = 0; bool limiting = false; };
    void setMeters (const Levels& l) { levels = l; }

    /** Drop a file from the OS: loads into the card under the point or inserts a new block. */
    bool dropFile (const juce::File&, juce::Point<int> localPoint);

    std::function<void()> onBackgroundClick;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverTerminal = 0; repaint(); }

    /** A normal scroll wheel pans sideways along a long rig. */
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            const bool canScrollV = getHeight() > vp->getViewHeight();
            if (! canScrollV || e.mods.isShiftDown())
            {
                const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
                vp->setViewPosition (vp->getViewPositionX() - juce::roundToInt (d * 500.0f), vp->getViewPositionY());
                return;
            }
        }
        Component::mouseWheelMove (e, w);
    }

private:
    GraphHost& host;
    std::unique_ptr<ChainView> main;
    juce::Rectangle<int> inBox, outBox;
    Levels levels;
    int hoverTerminal = 0;   // 1 in, 2 out
    juce::Rectangle<int> viewArea;
};

} // namespace rig
