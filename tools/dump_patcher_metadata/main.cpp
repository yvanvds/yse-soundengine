// dump_patcher_metadata — walks the patcher registry and emits a JSON
// description of every registered object's in-code documentation
// (description, category, inlet/outlet/parameter docs with ranges and
// accepted message types). The output is the source of truth for the
// auto-generated documentation/source/api/patcher_objects page; see
// issue #103.
//
// Linkage mirrors the test binary: this TU is added to a target that
// links yse_objects directly, bypassing the DLL export boundary so the
// internal pRegistry / pObject getters are reachable without API
// annotations.
//
// Usage:
//     dump_patcher_meta              # writes to stdout
//     dump_patcher_meta out.json     # writes to the named file
//
// nlohmann::json defaults to std::map-backed objects, which serialize
// with keys in lexicographic order. AllNames() iterates a std::map so
// objects are already lexicographic. Arrays (inlets/outlets/params)
// preserve insertion order. Re-running the dumper on an unchanged
// engine therefore yields byte-identical output.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "patcher/pObject.h"
#include "patcher/pRegistry.h"
#include "patcher/pEnums.h"
#include "patcher/parameters.h"
#include "patcher/inlet.h"
#include "patcher/outlet.h"
#include "headers/enums.hpp"

#include "utils/json.hpp"

using nlohmann::json;
using YSE::PATCHER::pObject;
using YSE::PATCHER::pCategory;
using YSE::PATCHER::Register;
using YSE::OUT_TYPE;

namespace {

const char * CategoryName(pCategory c) {
    switch (c) {
        case pCategory::UNSET:   return "UNSET";
        case pCategory::OSC:     return "OSC";
        case pCategory::FILTER:  return "FILTER";
        case pCategory::MATH:    return "MATH";
        case pCategory::GENERIC: return "GENERIC";
        case pCategory::GUI:     return "GUI";
        case pCategory::TIME:    return "TIME";
        case pCategory::MIDI:    return "MIDI";
    }
    return "UNKNOWN";
}

const char * OutTypeName(OUT_TYPE t) {
    switch (t) {
        case OUT_TYPE::INVALID: return "INVALID";
        case OUT_TYPE::BANG:    return "BANG";
        case OUT_TYPE::FLOAT:   return "FLOAT";
        case OUT_TYPE::INT:     return "INT";
        case OUT_TYPE::BUFFER:  return "BUFFER";
        case OUT_TYPE::LIST:    return "LIST";
        case OUT_TYPE::ANY:     return "ANY";
    }
    return "UNKNOWN";
}

// Mirrors the InletType bitmask order. The output array is sorted by
// the enum's natural order (BUFFER, FLOAT, INT, BANG, LIST) so two
// objects with the same accept set serialize identically.
json AcceptedTypes(unsigned int mask) {
    json arr = json::array();
    using YSE::PATCHER::IT_BUFFER;
    using YSE::PATCHER::IT_FLOAT;
    using YSE::PATCHER::IT_INT;
    using YSE::PATCHER::IT_BANG;
    using YSE::PATCHER::IT_LIST;
    if (mask & IT_BUFFER) arr.push_back("BUFFER");
    if (mask & IT_FLOAT)  arr.push_back("FLOAT");
    if (mask & IT_INT)    arr.push_back("INT");
    if (mask & IT_BANG)   arr.push_back("BANG");
    if (mask & IT_LIST)   arr.push_back("LIST");
    return arr;
}

} // namespace

int main(int argc, char ** argv) {
    auto & reg = Register();
    auto names = reg.AllNames();

    json root = json::object();

    for (const auto & name : names) {
        std::unique_ptr<pObject> obj(reg.Get(name));
        if (!obj) {
            std::fprintf(stderr,
                "dump_patcher_meta: registry returned null for %s\n",
                name.c_str());
            return EXIT_FAILURE;
        }

        json entry = json::object();
        entry["name"] = name;
        entry["category"] = CategoryName(obj->GetCategory());
        entry["is_dsp"] = obj->IsDSPObject();
        entry["description"] = obj->GetDescription();

        json inlets = json::array();
        for (int i = 0; i < obj->NumInputs(); ++i) {
            auto * port = obj->GetInlet(i);
            json p = json::object();
            p["index"] = i;
            p["label"] = port->GetDocLabel();
            p["doc"] = port->GetDocDescription();
            p["range"] = port->GetRange();
            p["accepts"] = AcceptedTypes(port->GetAcceptedTypes());
            inlets.push_back(std::move(p));
        }
        entry["inlets"] = std::move(inlets);

        json outlets = json::array();
        for (int i = 0; i < obj->NumOutputs(); ++i) {
            auto * port = obj->GetOutlet(i);
            json p = json::object();
            p["index"] = i;
            p["label"] = port->GetDocLabel();
            p["doc"] = port->GetDocDescription();
            p["range"] = port->GetRange();
            p["type"] = OutTypeName(obj->GetOutputType(i));
            outlets.push_back(std::move(p));
        }
        entry["outlets"] = std::move(outlets);

        json params = json::array();
        for (const auto & pdoc : obj->GetParamDocs()) {
            json p = json::object();
            p["name"] = pdoc.name;
            p["default"] = pdoc.defaultValue;
            p["doc"] = pdoc.doc;
            p["range"] = pdoc.range;
            params.push_back(std::move(p));
        }
        entry["params"] = std::move(params);

        root[name] = std::move(entry);
    }

    // dump(2, ' ') indents two spaces; a trailing newline matches the
    // POSIX text-file convention so the committed snapshot ends cleanly.
    const std::string serialized = root.dump(2);

    if (argc > 1) {
        std::ofstream out(argv[1], std::ios::binary);
        if (!out) {
            std::fprintf(stderr,
                "dump_patcher_meta: cannot open %s for writing\n", argv[1]);
            return EXIT_FAILURE;
        }
        out << serialized << '\n';
    } else {
        std::cout << serialized << '\n';
    }
    return EXIT_SUCCESS;
}
