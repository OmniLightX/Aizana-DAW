#pragma once
#include <algorithm>
#include <vector>

// View-only selection, held by stable track IDs across component rebuilds.
template <typename ID>
class TrackHeaderSelection
{
public:
    void select (ID id, bool toggle)
    {
        if (! toggle) { ids = { id }; return; }
        auto it = std::find (ids.begin(), ids.end(), id);
        if (it == ids.end()) ids.push_back (id);
        else ids.erase (it);
    }
    void clear() { ids.clear(); }
    bool contains (ID id) const { return std::find (ids.begin(), ids.end(), id) != ids.end(); }
    const std::vector<ID>& values() const { return ids; }
    void retain (const std::vector<ID>& live)
    {
        ids.erase (std::remove_if (ids.begin(), ids.end(), [&] (ID id) {
            return std::find (live.begin(), live.end(), id) == live.end();
        }), ids.end());
    }
private:
    std::vector<ID> ids;
};
