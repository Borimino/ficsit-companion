#include "node.hpp"
#include "pin.hpp"
#include "recipe.hpp"

#include <imgui_node_editor.h>

Node::Node(const ax::NodeEditor::NodeId id) : id(id)
{

}

Node::~Node()
{

}

bool Node::IsCraft() const
{
    return false;
}

bool Node::IsOrganizer() const
{
    return false;
}

bool Node::IsMerger() const
{
    return false;
}

bool Node::IsSplitter() const
{
    return false;
}

Json::Value Node::Serialize() const
{
    Json::Value node;
    node["kind"] = static_cast<int>(GetKind());
    node["pos"] = {
        { "x", pos.x },
        { "y", pos.y }
    };

    return node;
}

bool Node::Deserialize(const Json::Value& v)
{
    pos.x = v["pos"]["x"].get<float>();
    pos.y = v["pos"]["y"].get<float>();

    return true;
}

CraftNode::CraftNode(const ax::NodeEditor::NodeId id, const Recipe* recipe, const std::function<unsigned long long int()>& id_generator) :
    Node(id), recipe(recipe), current_rate(1, 1)
{
    for (const auto& input : recipe->ins)
    {
        ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, input.item, input.quantity));
    }
    for (const auto& output : recipe->outs)
    {
        outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, output.item, output.quantity));
    }
}

CraftNode::~CraftNode()
{

}

bool CraftNode::IsCraft() const
{
    return true;
}

Json::Value CraftNode::Serialize() const
{
    Json::Value node = Node::Serialize();
    node["rate"] = {
        { "num", current_rate.GetNumerator()},
        { "den", current_rate.GetDenominator()}
    };
    node["recipe"] = recipe->name;

    return node;
}

bool CraftNode::Deserialize(const Json::Value& v)
{
    Node::Deserialize(v);

    if (recipe->name != v["recipe"].get_string())
    {
        return false;
    }

    current_rate = FractionalNumber(v["rate"]["num"].get<long long int>(), v["rate"]["den"].get<long long int>());
    for (auto& p : ins)
    {
        p->current_rate = p->base_rate * current_rate;
    }
    for (auto& p : outs)
    {
        p->current_rate = p->base_rate * current_rate;
    }

    return true;
}

Node::Kind CraftNode::GetKind() const
{
    return Node::Kind::Craft;
}

OrganizerNode::OrganizerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item) : Node(id)
{
    ChangeItem(item);
}

OrganizerNode::~OrganizerNode()
{

}

bool OrganizerNode::IsOrganizer() const
{
    return true;
}

Json::Value OrganizerNode::Serialize() const
{
    Json::Value serialized = Node::Serialize();

    serialized["item"] = item == nullptr ? "" : item->name;

    Json::Array ins_array;
    ins_array.reserve(ins.size());
    for (auto& i : ins)
    {
        ins_array.push_back({
            { "num", i->current_rate.GetNumerator() },
            { "den", i->current_rate.GetDenominator() },
        });
    }
    serialized["ins"] = ins_array;

    Json::Array outs_array;
    outs_array.reserve(outs.size());
    for (auto& o : outs)
    {
        outs_array.push_back({
            { "num", o->current_rate.GetNumerator() },
            { "den", o->current_rate.GetDenominator() },
        });
    }
    serialized["outs"] = outs_array;

    return serialized;
}

bool OrganizerNode::Deserialize(const Json::Value& v)
{
    Node::Deserialize(v);

    if (item->name != v["item"].get_string())
    {
        return false;
    }

    for (int i = 0; i < v["ins"].size(); ++i)
    {
        if (i >= ins.size())
        {
            break;
        }
        ins[i]->current_rate = FractionalNumber(v["ins"][i]["num"].get<long long int>(), v["ins"][i]["den"].get<long long int>());
    }
    for (int i = 0; i < v["outs"].size(); ++i)
    {
        if (i >= outs.size())
        {
            break;
        }
        outs[i]->current_rate = FractionalNumber(v["outs"][i]["num"].get<long long int>(), v["outs"][i]["den"].get<long long int>());
    }

    return true;
}

void OrganizerNode::ChangeItem(const Item* item)
{
    this->item = item;
    for (auto& p : ins)
    {
        p->item = item;
    }
    for (auto& p : outs)
    {
        p->item = item;
    }
}

void OrganizerNode::RemoveItemIfNotForced()
{
    if (item == nullptr)
    {
        return;
    }

    for (auto& p : ins)
    {
        if (p->link != nullptr)
        {
            return;
        }
    }

    for (auto& p : outs)
    {
        if (p->link != nullptr)
        {
            return;
        }
    }

    ChangeItem(nullptr);
}

bool OrganizerNode::IsBalanced() const
{
    FractionalNumber input_sum(0, 1);
    for (const auto& p : ins)
    {
        input_sum += p->current_rate;
    }
    FractionalNumber output_sum(0, 1);
    for (const auto& p : outs)
    {
        output_sum += p->current_rate;
    }

    return input_sum == output_sum;
}

SplitterNode::SplitterNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item) : OrganizerNode(id, id_generator, item)
{
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
}

SplitterNode::~SplitterNode()
{
}

bool SplitterNode::IsSplitter() const
{
    return true;
}

Node::Kind SplitterNode::GetKind() const
{
    return Node::Kind::Splitter;
}

MergerNode::MergerNode(const ax::NodeEditor::NodeId id, const std::function<unsigned long long int()>& id_generator, const Item* item) : OrganizerNode(id, id_generator, item)
{
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    ins.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Input, this, nullptr));
    outs.emplace_back(std::make_unique<Pin>(id_generator(), ax::NodeEditor::PinKind::Output, this, nullptr));
}

MergerNode::~MergerNode()
{

}

bool MergerNode::IsMerger() const
{
    return true;
}

Node::Kind MergerNode::GetKind() const
{
    return Node::Kind::Merger;
}
