#pragma once

#include "Theme.h"

namespace rig
{
/** Selection ids for the IN and OUT terminals (not nodes: they open the I/O panel). */
inline const juce::String kInputId { "__input" }, kOutputId { "__output" };

/** NAM captures and / or IRs being dropped, and whether they came as a whole folder.
    Drag descriptions: "file:<path>", "files:<path>\n<path>...", "folder:<path>". */
struct CaptureDrop
{
    juce::Array<juce::File> nams, irs;
    bool folder = false;
    bool isEmpty() const noexcept { return nams.isEmpty() && irs.isEmpty(); }
    /** The files this block can take (its kind), or none. */
    juce::Array<juce::File> forBlock (const Node&) const;
    static bool isCaptureDescription (const juce::String&);
    static CaptureDrop fromDescription (const juce::String&);
    static CaptureDrop fromPaths (const juce::StringArray&);   // from the OS: files and folders
    static juce::String describe (const juce::Array<juce::File>&);   // -> "file:" / "files:"
};

/** The knobs of a block in the order its panel shows them (gain first, level last; no switches). */
std::vector<int> panelKnobs (const Block&);

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
    /** Step to the previous/next piece of gear in the library (or file in the folder). */
    virtual void browseFile (Node&, int delta) = 0;
    /** Load another capture of the same gear into a NAM / IR block, keeping its settings
        (the capture knobs: no rebuild of the panel, so it can follow a drag). */
    virtual void switchCapture (Node&, const juce::File&) = 0;
    virtual void showBranchMenu (Split&, int branch, juce::Component* anchor) = 0;
    virtual bool loadFileInto (Node* target, Chain* chain, int index, const juce::File&) = 0;
    /** Captures dropped onto a block (target) or into a chain at index. Ask: a block that already
        has captures asks whether to replace them or add these to its sweep (folders just replace). */
    enum class DropMode { Ask, Replace, Add };
    virtual void dropCaptures (Node* target, Chain* chain, int index, const CaptureDrop&, DropMode) = 0;
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

    /** Gear looks like what it is: stompboxes are tall, amp heads are wide with a grille, cabs show their speakers. */
    enum class Form { Pedal, Head, Cab };
    Form form() const;
    juce::Point<int> preferredSize() const override;
    static constexpr int cardHeight = 208;
    /** Repaints only if what the card shows has changed (called a few times a second). */
    void refresh();
    bool needsRelayout() const;
    Node* getNode() const override { return &node; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
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
    bool isOverKnob (juce::Point<int> local) const { return selector().valid() && knobArea().expanded (6.0f).contains (local.toFloat()); }

private:
    juce::Rectangle<float> plateArea() const;
    juce::uint64 signature() const;
    juce::uint64 lastSignature = 0;
    juce::Rectangle<float> footswitchArea() const;
    juce::Rectangle<float> diceArea() const;
    juce::Rectangle<float> lockArea() const;
    bool hasDice() const;
    /** NAM blocks sweeping several captures, and IRs from a pack, get a little selector knob. */
    struct Selector
    {
        int count = 0;
        float pos = 0;                      // 0..count-1 (NAM sweeps blend in between)
        juce::StringArray names, labels, tones;
        Gear gear = Gear::Unknown;
        bool sweep = true;
        bool valid() const noexcept { return count > 1; }
    };
    Selector selector() const;
    void setSelector (const Selector&, float pos);
    juce::Rectangle<float> knobArea() const;
    /** The knobs drawn on the card: how many of the block's own, then the capture selector if it has one. */
    int maxKnobs() const;
    juce::Point<float> knobCentre (int slot, int slots) const;
    float knobRadius (int slots) const;
    int shownSlots() const;
    void paintGrille (juce::Graphics&, juce::Rectangle<float>, bool live) const;
    void paintSpeakers (juce::Graphics&, juce::Rectangle<float>, bool live, juce::Colour) const;
    void stepCapture (int delta);
    void showCaptureMenu();
    /** The block's own knob under a point (its parameter index), or -1: they turn right on the card. */
    int knobParamAt (juce::Point<float>) const;
    int paramDrag = -1, paramHover = -1;
    float paramStart = 0;
    bool knobDragging = false;
    float knobStart = 0;
    int knobMoved = 0;
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
    void refresh();

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

    juce::uint64 signature() const;
    juce::uint64 lastSignature = 0;

    static constexpr int hubW = 116, pillW = 64, mergeW = 34, rowGap = 8, margin = 26;
};

//==============================================================================
/** The whole rig: IN terminal → main chain → OUT terminal. Lives in a Viewport. */
class GraphView;

/** A miniature of the whole rig under the graph (instead of a scrollbar): every block in its
    colour, with the visible part outlined. Click or drag anywhere on it to go there. */
class RigMap : public juce::Component,
               public juce::SettableTooltipClient,
               private juce::Timer
{
public:
    RigMap (juce::Viewport&, GraphView&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { jumpTo (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { jumpTo (e.position); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override { if (isShowing()) repaint(); }
    juce::Rectangle<float> mapArea() const;
    juce::Point<float> toMap (juce::Point<float> graphPoint) const;
    void jumpTo (juce::Point<float> mapPoint);
    juce::Viewport& viewport;
    GraphView& graph;
};

class GraphView : public juce::Component
{
public:
    explicit GraphView (GraphHost&);
    ~GraphView() override;

    void rebuild();
    void fitTo (juce::Rectangle<int> viewportArea);
    /** Size the whole rig needs (with margins). */
    juce::Point<int> contentSize() const;
    juce::Rectangle<int> getInBox() const noexcept { return inBox; }
    juce::Rectangle<int> getOutBox() const noexcept { return outBox; }
    juce::String getSelectedId() const { return host.getSelectedId(); }
    void resized() override;
    void paint (juce::Graphics&) override;

    struct Levels { float inL = 0, inR = 0, outL = 0, outR = 0, grDb = 0; bool limiting = false; };
    void setMeters (const Levels& l);
    /** Call a few times a second: repaints only the cards, splits and meters that changed. */
    void refreshLive();

    /** Files / folders from the OS: into the card under the point (its knob adds to the sweep),
        or new blocks where they land. */
    void dropFiles (const CaptureDrop&, juce::Point<int> localPoint);

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
    juce::String shownSelection;
    float shownGains = 0.0f;
    int liveTicks = 0;
};

} // namespace rig
