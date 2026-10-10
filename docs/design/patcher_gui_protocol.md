# Patcher GUI value protocol

How a host reads an object's control state and how a preset writes it back. User-facing behaviour is
in [documentation/source/patcher/gui.rst](../../documentation/source/patcher/gui.rst). Moved out of
PROJECT_OVERVIEW.md by issue #890.

**GUI value protocol.** How a host reads an object's control state, and how a preset writes it back
(issue [#551](https://github.com/yvanvds/yse-soundengine/issues/551)). GUI state is a **list of
string cells**: `pObject::GetGuiValue()` is the whole state as one string — the value for a scalar
control, every cell space-separated in index order for a structured one — while `GetGuiValueCount()`
/ `GetGuiValueAt(i)` are the same state cell by cell. A scalar control is the one-cell case and
inherits it, so the ~30 objects that predate the protocol implement nothing and answer exactly what
they always answered: the default `GetGuiValueAt(0)` *is* `GetGuiValue()`. Cells are strings rather
than a typed payload because the controls that need this share no element type (`.rslider` a float
pair, `.multislider` floats, `.matrixctrl` cell states), and because it keeps the C ABI at one
string-copy call per read. Deliberately not the `guiProperties` map, which is editor decoration
(geometry, colour) serialised under `"gui"` and unsynchronised. **There is no setter**: state goes
*into* an object as an ordinary message on inlet 0 from the control thread (`pHandle::SetListData`),
so `.preset` restores a patch by sending each object its stored string rather than reaching into
another object's fields — an object answering `GuiValueIsSettable()` promises that its own
`GetGuiValue()` output is accepted back verbatim, plus `set <index> <value>` for one cell. The
host-thread / audio-thread contract for all of it (atomic fields, no locks, read-modify-write in one
step, destructive polls, non-atomic multi-cell reads) is stated once in
[patcher/pObject.h](../../YseEngine/patcher/pObject.h) rather than rediscovered per object;
[Tests/patcher/test_patcher_gui_protocol.cpp](../../Tests/patcher/test_patcher_gui_protocol.cpp)
pins the shape and
[Tests/patcher/test_patcher_object_races.cpp](../../Tests/patcher/test_patcher_object_races.cpp)
exercises the threading.
